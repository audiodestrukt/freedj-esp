// SPDX-FileCopyrightText: 2026 Dan Newcome
// SPDX-License-Identifier: GPL-3.0-or-later
// USB audio output (see usb_audio.h).

#include "usb_audio.h"

#include <atomic>
#include <initializer_list>
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

enum class Ev : uint8_t { New, Gone, RestartIn };
struct Msg { Ev ev; uint8_t addr; };
static QueueHandle_t s_q;

static usb_device_handle_t s_dev;
static std::atomic<bool> s_gone;
static std::atomic<int> s_inFlight;
static usb_transfer_t* s_xfer[kXfers];
static uint32_t s_acc;                      // fractional frames per packet
static int s_claimed = -1;                  // claimed interface, or -1

struct Out { bool found; uint8_t iface, alt, ep; uint16_t mps; };

// Input (timecode): the first 2+ channel PCM capture stream. Its own ring
// of stereo frames (L | R << 16), filled by the IN transfer callback and
// drained by vinyl.cpp's decoder task.
struct In { bool found; uint8_t iface, alt, ep; uint16_t mps; uint8_t ch, sub; uint32_t rate; };
constexpr uint32_t kInRing = 8192;          // frames, power of two (~170 ms)
static uint32_t* s_inRing;
static std::atomic<uint32_t> s_inWr, s_inRd;
static std::atomic<bool> s_inActive;
static std::atomic<uint32_t> s_inRate, s_inOverflows, s_inPackets, s_inErrors, s_inRestarts;
static std::atomic<int> s_inFlightIn;
static usb_transfer_t* s_inXfer[kXfers];
static In s_in;
static int s_claimedIn = -1;

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

// First AudioStreaming alt setting with an isochronous IN endpoint, 2 or more
// channels, 16/24/32-bit PCM. Rate: the deck's if offered, else 48 or 44.1 kHz,
// else the first listed (the decoder works at any rate).
static uint32_t pickRate(const uint8_t* fmt)
{
    for (uint32_t r : {(uint32_t)s_rate, (uint32_t)48000, (uint32_t)44100}) if (hasRate(fmt, r)) return r;
    const uint8_t* p = fmt + 8;
    return (uint32_t)(p[0] | p[1] << 8 | p[2] << 16);
}

static In findIn(const usb_config_desc_t* cfg)
{
    const uint8_t* b = (const uint8_t*)cfg;
    uint8_t cls = 0, sub = 0, proto = 0, iface = 0, alt = 0;
    In cand = {}, best = {};
    bool fmtOk = false, uac2 = false;
    for (int off = 0; off < cfg->wTotalLength && b[off]; off += b[off]) {
        const uint8_t* d = b + off;
        if (d[1] == USB_B_DESCRIPTOR_TYPE_INTERFACE) {
            iface = d[2]; alt = d[3]; cls = d[5]; sub = d[6]; proto = d[7]; fmtOk = false;
            if (cls == 0x01 && proto == 0x20) uac2 = true;
        } else if (cls == 0x01 && sub == 2 && d[1] == 0x24 && d[2] == 2 && d[3] == 1 && proto == 0) {
            fmtOk = d[4] >= 2 && (d[5] == 2 || d[5] == 3 || d[5] == 4);
            if (fmtOk) { cand.ch = d[4]; cand.sub = d[5]; cand.rate = pickRate(d); }
            if (fmtOk) ESP_LOGI(TAG, "  AS iface %d alt %d: %d ch, %d-bit in %d-byte slots, %d rate(s), first %lu Hz",
                     iface, alt, d[4], d[6], d[5], d[7], (unsigned long)(d[8] | d[9] << 8 | d[10] << 16));
        } else if (cls == 0x01 && sub == 2 && d[1] == USB_B_DESCRIPTOR_TYPE_ENDPOINT && fmtOk &&
                   (d[2] & 0x80) && (d[3] & 3) == 1) {
            const uint16_t mps = (d[4] | d[5] << 8) & 0x7FF;
            const uint16_t need = (cand.rate + 999) / 1000 * cand.ch * cand.sub;
            if (mps >= need) {
                cand.found = true; cand.iface = iface; cand.alt = alt; cand.ep = d[2]; cand.mps = mps;
                if (cand.rate == s_rate) return cand;    // same rate as the output: one clock
                if (!best.found) best = cand;
                fmtOk = false;
                continue;
            }
            ESP_LOGW(TAG, "  IN 0x%02X max packet %d < %d needed at %lu Hz", d[2], mps, need, (unsigned long)cand.rate);
        }
    }
    if (!best.found && uac2) ESP_LOGW(TAG, "  USB Audio Class 2 interface: not supported (full-speed host speaks UAC1 only)");
    return best;
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

// ---- input streaming ------------------------------------------------------------

static inline int16_t sampleAt(const uint8_t* p, int sub)
{
    return (int16_t)(p[sub - 2] | p[sub - 1] << 8);   // top 16 bits, little-endian
}

static void isoInCb(usb_transfer_t* t)
{
    if (!s_gone && t->status == USB_TRANSFER_STATUS_COMPLETED) {
        const int frameBytes = s_in.ch * s_in.sub;
        uint32_t wr = s_inWr.load(std::memory_order_relaxed);
        const uint32_t rd = s_inRd.load(std::memory_order_acquire);
        for (int i = 0; i < kPktsPerXfer; i++) {
            const auto& pd = t->isoc_packet_desc[i];
            if (pd.status != USB_TRANSFER_STATUS_COMPLETED) { s_inErrors++; continue; }
            s_inPackets++;
            const uint8_t* p = t->data_buffer + i * s_in.mps;
            const int n = pd.actual_num_bytes / frameBytes;
            for (int f = 0; f < n; f++, p += frameBytes) {
                if (wr - rd >= kInRing) { s_inOverflows++; break; }
                s_inRing[wr & (kInRing - 1)] = (uint16_t)sampleAt(p, s_in.sub) |
                                               (uint32_t)(uint16_t)sampleAt(p + s_in.sub, s_in.sub) << 16;
                wr++;
            }
        }
        s_inWr.store(wr, std::memory_order_release);
        for (int i = 0; i < kPktsPerXfer; i++) t->isoc_packet_desc[i].num_bytes = s_in.mps;
        t->num_bytes = kPktsPerXfer * s_in.mps;
        if (usb_host_transfer_submit(t) == ESP_OK) return;
    }
    // A transfer error halts the endpoint (the PCM2902 overflowed one after
    // ~220 s). When the last one is back, the device task clears it and
    // restarts the input; the decoder keeps its state meanwhile.
    s_inErrors++;
    if (--s_inFlightIn == 0 && !s_gone && t->status != USB_TRANSFER_STATUS_NO_DEVICE && s_inActive) {
        Msg m{Ev::RestartIn, (uint8_t)t->status};
        xQueueSend(s_q, &m, 0);
    }
}

static void restartIn(int status)
{
    if (!s_dev || s_inFlightIn > 0) return;
    usb_host_endpoint_flush(s_dev, s_in.ep);
    usb_host_endpoint_clear(s_dev, s_in.ep);
    for (usb_transfer_t* t : s_inXfer) {
        if (!t) continue;
        t->num_bytes = kPktsPerXfer * s_in.mps;
        for (int k = 0; k < kPktsPerXfer; k++) t->isoc_packet_desc[k].num_bytes = s_in.mps;
        if (usb_host_transfer_submit(t) == ESP_OK) s_inFlightIn++;
    }
    s_inRestarts++;
    if (s_inFlightIn == 0) { s_inActive = false; ESP_LOGE(TAG, "input stopped (status %d); restart failed", status); }
    else ESP_LOGW(TAG, "input restarted after transfer status %d (%lu restarts)", status, (unsigned long)s_inRestarts.load());
}

static bool startIn(const In& in)
{
    if (usb_host_interface_claim(s_client, s_dev, in.iface, in.alt) != ESP_OK) {
        ESP_LOGW(TAG, "claim of input interface %d alt %d failed", in.iface, in.alt);
        return false;
    }
    s_claimedIn = in.iface;
    if (!setInterface(in.iface, in.alt)) return false;
    const uint8_t r[3] = {(uint8_t)in.rate, (uint8_t)(in.rate >> 8), (uint8_t)(in.rate >> 16)};
    control(0x22, 0x01, 0x0100, in.ep, r, 3, "SET_CUR input sample rate (optional)");
    s_in = in;
    s_inRd.store(s_inWr.load());
    s_inRate = in.rate;
    for (int i = 0; i < kXfers; i++) {
        if (usb_host_transfer_alloc(kPktsPerXfer * in.mps, kPktsPerXfer, &s_inXfer[i]) != ESP_OK) break;
        usb_transfer_t* t = s_inXfer[i];
        t->device_handle = s_dev;
        t->bEndpointAddress = in.ep;
        t->callback = isoInCb;
        t->num_bytes = kPktsPerXfer * in.mps;
        for (int k = 0; k < kPktsPerXfer; k++) t->isoc_packet_desc[k].num_bytes = in.mps;
        if (usb_host_transfer_submit(t) != ESP_OK) break;
        s_inFlightIn++;
    }
    if (s_inFlightIn == 0) return false;
    s_inActive = true;
    ESP_LOGI(TAG, "input: %lu Hz, %d ch x %d bytes from interface %d alt %d, IN 0x%02X (max packet %d)",
             (unsigned long)in.rate, in.ch, in.sub, in.iface, in.alt, in.ep, in.mps);
    return true;
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
    s_inActive = false;
    // Transfers to a gone device complete with an error; wait for them all.
    for (int i = 0; (s_inFlight > 0 || s_inFlightIn > 0) && i < 100; i++) vTaskDelay(pdMS_TO_TICKS(10));
    if (s_inFlight > 0 || s_inFlightIn > 0)
        ESP_LOGE(TAG, "%d transfer(s) never returned; leaking them", s_inFlight.load() + s_inFlightIn.load());
    else {
        for (auto*& t : s_xfer) if (t) { usb_host_transfer_free(t); t = nullptr; }
        for (auto*& t : s_inXfer) if (t) { usb_host_transfer_free(t); t = nullptr; }
    }
    for (auto*& t : s_xfer) t = nullptr;
    for (auto*& t : s_inXfer) t = nullptr;
    if (s_claimed >= 0) usb_host_interface_release(s_client, s_dev, s_claimed);
    if (s_claimedIn >= 0) usb_host_interface_release(s_client, s_dev, s_claimedIn);
    s_claimed = s_claimedIn = -1;
    s_inRate = 0;
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
    In in = {};
    if (usb_host_get_active_config_descriptor(s_dev, &cfg) == ESP_OK) { o = findOut(cfg); in = findIn(cfg); }
    bool ok = o.found && start(o);
    if (o.found && !ok) ESP_LOGW(TAG, "audio device @%d: couldn't start output", addr);
    const bool inOk = in.found && startIn(in);
    if (in.found && !inOk) ESP_LOGW(TAG, "audio device @%d: couldn't start input", addr);
    if (ok || inOk) return;
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
        else if (m.ev == Ev::RestartIn) restartIn(m.addr);
        else if (s_dev) close(true);
    }
}

// ---- public -------------------------------------------------------------------------

void begin(uint32_t sampleRate)
{
    s_rate = sampleRate;
    s_ring = (uint32_t*)heap_caps_calloc(kRing, 4, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    s_inRing = (uint32_t*)heap_caps_calloc(kInRing, 4, MALLOC_CAP_SPIRAM);
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

bool inputActive() { return s_inActive; }
uint32_t inputRate() { return s_inRate; }

size_t read(int16_t* lr, size_t maxFrames)
{
    const uint32_t rd = s_inRd.load(std::memory_order_relaxed);
    uint32_t n = s_inWr.load(std::memory_order_acquire) - rd;
    if (n > maxFrames) n = maxFrames;
    for (uint32_t i = 0; i < n; i++) {
        const uint32_t v = s_inRing[(rd + i) & (kInRing - 1)];
        lr[2 * i] = (int16_t)(v & 0xFFFF);
        lr[2 * i + 1] = (int16_t)(v >> 16);
    }
    s_inRd.store(rd + n, std::memory_order_release);
    return n;
}

size_t inputFill() { return s_inWr.load(std::memory_order_acquire) - s_inRd.load(std::memory_order_relaxed); }

InputStats inputStats() { return {s_inPackets, s_inErrors, s_inOverflows, s_inRestarts}; }
uint32_t corrections() { return s_corrections; }

} // namespace usb_audio
