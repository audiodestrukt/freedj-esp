// SPDX-FileCopyrightText: 2026 Dan Newcome
// SPDX-License-Identifier: GPL-3.0-or-later
// USB audio output (see usb_audio.h).

#include "usb_audio.h"

#include <atomic>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "usb/usb_host.h"

namespace usb_audio {

static const char* TAG = "uac";

static uint32_t s_rate;
constexpr int kPktsPerXfer = 4;             // 4 ms per transfer
constexpr int kXfers = 3;                   // ~12 ms in flight

// Ring of stereo frames, one writer (audio task) and one reader (USB client
// task). The reader keeps it near kTarget so the audio loop's bursty writes
// (one 256-frame block at a time) never empty or overflow it.
constexpr uint32_t kRing = 4096;            // frames, power of two
constexpr uint32_t kTarget = 1024;          // ~23 ms at 44.1 kHz
constexpr uint32_t kSlack = 512;            // drift band either side of kTarget
static uint32_t* s_ring;                    // L | R << 16
static std::atomic<uint32_t> s_wr, s_rd;    // free-running frame counters

static std::atomic<bool> s_active, s_primed;
static std::atomic<uint32_t> s_underruns, s_corrections, s_overflows;

static usb_host_client_handle_t s_client;
static SemaphoreHandle_t s_ctrlDone;

enum class Ev : uint8_t { New, Gone };
struct Msg { Ev ev; uint8_t addr; };
static QueueHandle_t s_q;

static usb_device_handle_t s_dev;
static std::atomic<bool> s_gone;
static std::atomic<int> s_inFlight;
static usb_transfer_t* s_xfer[kXfers];
static uint32_t s_acc;                      // fractional frames per packet
static int s_claimed = -1;                  // claimed interface, or -1

struct Out { bool found; uint8_t iface, alt, ep; uint16_t mps; };

// ---- descriptors ----------------------------------------------------------------

static bool hasRate(const uint8_t* fmt, uint32_t rate)
{
    const int n = fmt[7];
    auto f = [&](int i) { const uint8_t* p = fmt + 8 + 3 * i; return (uint32_t)(p[0] | p[1] << 8 | p[2] << 16); };
    if (n == 0) return f(0) <= rate && rate <= f(1);    // continuous range
    for (int i = 0; i < n; i++) if (f(i) == rate) return true;
    return false;
}

// First AudioStreaming alt setting with an isochronous OUT endpoint, 2 ch,
// 16-bit PCM, s_rate in its list, and room for the biggest packet.
static Out findOut(const usb_config_desc_t* cfg)
{
    const uint8_t* b = (const uint8_t*)cfg;
    const uint16_t need = (s_rate + 999) / 1000 * 4;
    uint8_t cls = 0, sub = 0, iface = 0, alt = 0;
    bool fmtOk = false;
    for (int off = 0; off < cfg->wTotalLength && b[off]; off += b[off]) {
        const uint8_t* d = b + off;
        if (d[1] == USB_B_DESCRIPTOR_TYPE_INTERFACE) {
            iface = d[2]; alt = d[3]; cls = d[5]; sub = d[6]; fmtOk = false;
        } else if (cls == 0x01 && sub == 2 && d[1] == 0x24 && d[2] == 2 && d[3] == 1) {
            fmtOk = d[4] == 2 && d[5] == 2 && d[6] == 16 && hasRate(d, s_rate);
        } else if (cls == 0x01 && sub == 2 && d[1] == USB_B_DESCRIPTOR_TYPE_ENDPOINT && fmtOk &&
                   !(d[2] & 0x80) && (d[3] & 3) == 1) {
            const uint16_t mps = d[4] | d[5] << 8;
            if (mps >= need) return {true, iface, alt, d[2], mps};
        }
    }
    return {};
}

// ---- control requests ------------------------------------------------------------

static void ctrlCb(usb_transfer_t*) { xSemaphoreGive(s_ctrlDone); }

static bool control(uint8_t reqType, uint8_t req, uint16_t value, uint16_t index,
                    const uint8_t* data, uint16_t len, const char* what)
{
    usb_transfer_t* t;
    if (usb_host_transfer_alloc(64, 0, &t) != ESP_OK) return false;
    auto* s = (usb_setup_packet_t*)t->data_buffer;
    s->bmRequestType = reqType;
    s->bRequest = req;
    s->wValue = value;
    s->wIndex = index;
    s->wLength = len;
    if (len) memcpy(t->data_buffer + sizeof(usb_setup_packet_t), data, len);
    t->num_bytes = sizeof(usb_setup_packet_t) + len;
    t->device_handle = s_dev;
    t->bEndpointAddress = 0;
    t->callback = ctrlCb;
    const bool ok = usb_host_transfer_submit_control(s_client, t) == ESP_OK &&
                    xSemaphoreTake(s_ctrlDone, pdMS_TO_TICKS(1000)) == pdTRUE &&
                    t->status == USB_TRANSFER_STATUS_COMPLETED;
    if (!ok) ESP_LOGW(TAG, "%s failed (status %d)", what, t->status);
    usb_host_transfer_free(t);
    return ok;
}

static bool setInterface(uint8_t iface, uint8_t alt)
{
    return control(0x01, 0x0B, alt, iface, nullptr, 0, "SET_INTERFACE");
}

// ---- streaming --------------------------------------------------------------------

// One packet's worth of frames from the ring: 44 or 45 at 44.1 kHz.
static int fillPacket(uint32_t* dst)
{
    s_acc += s_rate;
    const int n = s_acc / 1000;
    s_acc %= 1000;

    uint32_t rd = s_rd.load(std::memory_order_relaxed);
    const uint32_t level = s_wr.load(std::memory_order_acquire) - rd;
    if (!s_primed) {
        if (level < kTarget) { memset(dst, 0, n * 4); return n; }
        s_primed = true;
    }
    if (level < (uint32_t)n) {               // ran dry: silence, re-prime
        s_underruns++;
        s_primed = false;
        memset(dst, 0, n * 4);
        return n;
    }
    // Drift: past the band, take one frame more or one less this packet.
    int take = n;
    if (level > kTarget + kSlack) { take = n + 1; s_corrections++; }
    else if (level < kTarget - kSlack && level > (uint32_t)n) { take = n - 1; s_corrections++; }
    for (int i = 0; i < n; i++) {
        const int src = take == n ? i : take > n ? (i < n - 1 ? i : i + 1) : (i < n - 1 ? i : n - 2);
        dst[i] = s_ring[(rd + src) & (kRing - 1)];
    }
    s_rd.store(rd + take, std::memory_order_release);
    return n;
}

static void fillXfer(usb_transfer_t* t)
{
    auto* p = (uint32_t*)t->data_buffer;
    int total = 0;
    for (int i = 0; i < kPktsPerXfer; i++) {
        const int n = fillPacket(p + total);
        t->isoc_packet_desc[i].num_bytes = n * 4;
        total += n;
    }
    t->num_bytes = total * 4;
}

static void isoCb(usb_transfer_t* t)
{
    if (!s_gone && t->status == USB_TRANSFER_STATUS_COMPLETED) {
        fillXfer(t);
        if (usb_host_transfer_submit(t) == ESP_OK) return;
    }
    if (!s_gone) ESP_LOGW(TAG, "stream stopped (transfer status %d)", t->status);
    s_active = false;
    s_inFlight--;
}

static bool start(const Out& o)
{
    if (usb_host_interface_claim(s_client, s_dev, o.iface, o.alt) != ESP_OK) {
        ESP_LOGW(TAG, "claim of interface %d alt %d failed", o.iface, o.alt);
        return false;
    }
    s_claimed = o.iface;
    if (!setInterface(o.iface, o.alt)) return false;
    // Optional: the UCA202 stalls this and follows the packet rate instead.
    const uint8_t r[3] = {(uint8_t)s_rate, (uint8_t)(s_rate >> 8), (uint8_t)(s_rate >> 16)};
    control(0x22, 0x01, 0x0100, o.ep, r, 3, "SET_CUR sample rate (optional)");

    s_rd.store(s_wr.load());                 // empty ring
    s_primed = false;
    s_acc = 0;
    s_active = true;                         // audio_out starts feeding (and mutes the speaker)
    const int maxPkt = (s_rate + 999) / 1000 * 4;
    for (int i = 0; i < kXfers; i++) {
        if (usb_host_transfer_alloc(kPktsPerXfer * maxPkt, kPktsPerXfer, &s_xfer[i]) != ESP_OK) break;
        s_xfer[i]->device_handle = s_dev;
        s_xfer[i]->bEndpointAddress = o.ep;
        s_xfer[i]->callback = isoCb;
        fillXfer(s_xfer[i]);
        if (usb_host_transfer_submit(s_xfer[i]) != ESP_OK) break;
        s_inFlight++;
    }
    if (s_inFlight == 0) { s_active = false; return false; }
    ESP_LOGI(TAG, "streaming: %lu Hz stereo to interface %d alt %d, OUT 0x%02X; speaker muted",
             (unsigned long)s_rate, o.iface, o.alt, o.ep);
    return true;
}

// ---- device handling ----------------------------------------------------------------

static void close(bool log)
{
    s_active = false;
    // Transfers to a gone device complete with an error; wait for them all.
    for (int i = 0; s_inFlight > 0 && i < 100; i++) vTaskDelay(pdMS_TO_TICKS(10));
    if (s_inFlight > 0) ESP_LOGE(TAG, "%d transfer(s) never returned; leaking them", s_inFlight.load());
    else for (auto*& t : s_xfer) if (t) { usb_host_transfer_free(t); t = nullptr; }
    for (auto*& t : s_xfer) t = nullptr;
    if (s_claimed >= 0) usb_host_interface_release(s_client, s_dev, s_claimed);
    s_claimed = -1;
    usb_host_device_close(s_client, s_dev);
    s_dev = nullptr;
    if (log) ESP_LOGI(TAG, "USB audio gone; speaker back");
}

static void open(uint8_t addr)
{
    if (s_dev || usb_host_device_open(s_client, addr, &s_dev) != ESP_OK) return;
    s_gone = false;
    const usb_config_desc_t* cfg;
    Out o = {};
    if (usb_host_get_active_config_descriptor(s_dev, &cfg) == ESP_OK) o = findOut(cfg);
    if (o.found && start(o)) return;
    if (o.found) ESP_LOGW(TAG, "audio device @%d: couldn't start streaming", addr);
    // Not ours (or failed): let go.
    close(false);
}

static void clientCb(const usb_host_client_event_msg_t* msg, void*)
{
    Msg m{};
    if (msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
        m = {Ev::New, msg->new_dev.address};
    } else if (msg->event == USB_HOST_CLIENT_EVENT_DEV_GONE && msg->dev_gone.dev_hdl == s_dev) {
        s_gone = true;
        s_active = false;
        m = {Ev::Gone, 0};
    } else {
        return;
    }
    xQueueSend(s_q, &m, 0);
}

// Transfer callbacks run in here, so device setup runs in another task.
static void clientTask(void*)
{
    for (;;) usb_host_client_handle_events(s_client, portMAX_DELAY);
}

static void devTask(void*)
{
    for (;;) {
        Msg m;
        xQueueReceive(s_q, &m, portMAX_DELAY);
        if (m.ev == Ev::New) open(m.addr);
        else if (s_dev) close(true);
    }
}

// ---- public -------------------------------------------------------------------------

void begin(uint32_t sampleRate)
{
    s_rate = sampleRate;
    s_ring = (uint32_t*)heap_caps_calloc(kRing, 4, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    s_q = xQueueCreate(8, sizeof(Msg));
    s_ctrlDone = xSemaphoreCreateBinary();
    usb_host_client_config_t cc = {};
    cc.is_synchronous = false;
    cc.max_num_event_msg = 8;
    cc.async.client_event_callback = clientCb;
    ESP_ERROR_CHECK(usb_host_client_register(&cc, &s_client));
    xTaskCreatePinnedToCore(clientTask, "uac_client", 4096, nullptr, 5, nullptr, 0);
    xTaskCreatePinnedToCore(devTask, "uac_dev", 4096, nullptr, 4, nullptr, 0);
}

bool active() { return s_active; }

void write(const int16_t* lr, size_t frames)
{
    uint32_t wr = s_wr.load(std::memory_order_relaxed);
    const uint32_t free = kRing - (wr - s_rd.load(std::memory_order_acquire));
    if (frames > free) { s_overflows++; frames = free; }
    for (size_t i = 0; i < frames; i++)
        s_ring[(wr + i) & (kRing - 1)] = (uint16_t)lr[2 * i] | (uint32_t)(uint16_t)lr[2 * i + 1] << 16;
    s_wr.store(wr + frames, std::memory_order_release);
}

uint32_t underruns() { return s_underruns; }
uint32_t corrections() { return s_corrections; }

} // namespace usb_audio
