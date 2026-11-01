// USB audio probe (see usb_audio_probe.h).

#include "usb_audio_probe.h"

#include <atomic>
#include <math.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "usb/usb_host.h"

namespace usb_audio_probe {

static const char* TAG = "uac";

constexpr uint32_t kRate = 48000;           // a whole number of frames per 1 ms USB frame
constexpr int kPktBytes = kRate / 1000 * 4; // 48 stereo 16-bit frames = 192 bytes
constexpr int kPktsPerXfer = 8;             // 8 ms per transfer
constexpr int kXfers = 3;                   // in flight: ~24 ms queued
constexpr int kToneMs = 2000;

static usb_host_client_handle_t s_client;
static QueueHandle_t s_newDev;              // device addresses, from the client callback
static SemaphoreHandle_t s_ctrlDone;
static usb_device_handle_t s_dev;           // the device being probed
static volatile bool s_gone;

// The output we'll test: first AudioStreaming alt setting with an isochronous
// OUT endpoint, 2 channels, 16-bit PCM, 48 kHz in its rate list.
struct Out {
    bool found;
    uint8_t iface, alt, ep;
    uint16_t mps;
};

// ---- descriptor dump ---------------------------------------------------------

static const char* terminalName(uint16_t t)
{
    switch (t) {
    case 0x0101: return "USB streaming";
    case 0x0201: return "microphone";
    case 0x0301: return "speaker";
    case 0x0302: return "headphones";
    case 0x0601: return "analog connector";
    case 0x0602: return "digital interface";
    case 0x0603: return "line connector";
    case 0x0605: return "S/PDIF";
    default:     return "other";
    }
}

static const char* syncName(uint8_t attr)
{
    static const char* n[] = {"no sync", "async", "adaptive", "sync"};
    return n[(attr >> 2) & 3];
}

static bool hasRate(const uint8_t* fmt, uint32_t rate)
{
    const int n = fmt[7];
    auto f = [&](int i) { const uint8_t* p = fmt + 8 + 3 * i; return p[0] | p[1] << 8 | p[2] << 16; };
    if (n == 0) return (uint32_t)f(0) <= rate && rate <= (uint32_t)f(1);   // continuous range
    for (int i = 0; i < n; i++) if ((uint32_t)f(i) == rate) return true;
    return false;
}

static Out dump(const usb_config_desc_t* cfg)
{
    Out out = {};
    const uint8_t* b = (const uint8_t*)cfg;
    uint8_t cls = 0, sub = 0, iface = 0, alt = 0;
    bool fmtOk = false;   // current alt is 2 ch / 16-bit / has 48 kHz
    for (int off = 0; off < cfg->wTotalLength && b[off]; off += b[off]) {
        const uint8_t* d = b + off;
        if (d[1] == USB_B_DESCRIPTOR_TYPE_INTERFACE) {
            iface = d[2]; alt = d[3]; cls = d[5]; sub = d[6]; fmtOk = false;
            if (cls == 0x01)
                ESP_LOGI(TAG, "  interface %d alt %d: audio %s, %d endpoint(s)", iface, alt,
                         sub == 1 ? "control" : sub == 2 ? "streaming" : sub == 3 ? "MIDI" : "?", d[4]);
        } else if (cls != 0x01) {
            continue;
        } else if (d[1] == 0x24 && sub == 1) {          // class-specific AudioControl
            if (d[2] == 2 || d[2] == 3)
                ESP_LOGI(TAG, "    %s terminal %d: %s", d[2] == 2 ? "input" : "output", d[3],
                         terminalName(d[4] | d[5] << 8));
            else if (d[2] == 6)
                ESP_LOGI(TAG, "    feature unit %d (source %d), controls 0x%02X", d[3], d[4], d[6]);
        } else if (d[1] == 0x24 && sub == 2) {          // class-specific AudioStreaming
            if (d[2] == 1) {
                ESP_LOGI(TAG, "    linked to terminal %d, format tag 0x%04X%s", d[3], d[5] | d[6] << 8,
                         (d[5] | d[6] << 8) == 1 ? " (PCM)" : "");
            } else if (d[2] == 2 && d[3] == 1) {
                char rates[96]; int n = 0;
                if (d[7] == 0) {
                    n = snprintf(rates, sizeof rates, "%d-%d", d[8] | d[9] << 8 | d[10] << 16,
                                 d[11] | d[12] << 8 | d[13] << 16);
                } else {
                    for (int i = 0; i < d[7] && n < (int)sizeof rates - 12; i++) {
                        const uint8_t* p = d + 8 + 3 * i;
                        n += snprintf(rates + n, sizeof rates - n, "%s%d", i ? "/" : "", p[0] | p[1] << 8 | p[2] << 16);
                    }
                }
                ESP_LOGI(TAG, "    %d ch, %d-bit in %d-byte slots, %s Hz", d[4], d[6], d[5], rates);
                fmtOk = d[4] == 2 && d[5] == 2 && d[6] == 16 && hasRate(d, kRate);
            }
        } else if (d[1] == USB_B_DESCRIPTOR_TYPE_ENDPOINT && sub == 2) {
            const uint16_t mps = d[4] | d[5] << 8;
            const bool in = d[2] & 0x80;
            ESP_LOGI(TAG, "    endpoint 0x%02X %s, %s, %s, max %d bytes", d[2], in ? "IN" : "OUT",
                     (d[3] & 3) == 1 ? "isochronous" : "not iso", syncName(d[3]), mps);
            if (!out.found && fmtOk && !in && (d[3] & 3) == 1 && mps >= kPktBytes)
                out = {true, iface, alt, d[2], mps};
        }
    }
    return out;
}

// ---- control requests ---------------------------------------------------------

static void ctrlCb(usb_transfer_t* t) { xSemaphoreGive(s_ctrlDone); }

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
    bool ok = usb_host_transfer_submit_control(s_client, t) == ESP_OK &&
              xSemaphoreTake(s_ctrlDone, pdMS_TO_TICKS(1000)) == pdTRUE &&
              t->status == USB_TRANSFER_STATUS_COMPLETED;
    if (!ok) ESP_LOGW(TAG, "  %s failed (status %d)", what, t->status);
    usb_host_transfer_free(t);
    return ok;
}

static bool setInterface(uint8_t iface, uint8_t alt)
{
    return control(0x01, 0x0B, alt, iface, nullptr, 0, "SET_INTERFACE");
}

// ---- tone ----------------------------------------------------------------------

static float s_phase;
static std::atomic<int> s_inFlight, s_pktsLeft, s_pktErrs, s_xferErrs;

static void fill(usb_transfer_t* t)
{
    auto* pcm = (int16_t*)t->data_buffer;
    const float step = 2.0f * (float)M_PI * 440.0f / kRate;
    for (int i = 0; i < kPktsPerXfer * kPktBytes / 4; i++) {
        const int16_t v = (int16_t)(8192.0f * sinf(s_phase));   // -12 dBFS
        pcm[2 * i] = pcm[2 * i + 1] = v;
        s_phase += step;
        if (s_phase > 2.0f * (float)M_PI) s_phase -= 2.0f * (float)M_PI;
    }
    for (int i = 0; i < kPktsPerXfer; i++) t->isoc_packet_desc[i].num_bytes = kPktBytes;
    t->num_bytes = kPktsPerXfer * kPktBytes;
}

static void isoCb(usb_transfer_t* t)
{
    if (t->status != USB_TRANSFER_STATUS_COMPLETED) s_xferErrs++;
    for (int i = 0; i < kPktsPerXfer; i++)
        if (t->isoc_packet_desc[i].status != USB_TRANSFER_STATUS_COMPLETED) s_pktErrs++;
    s_pktsLeft -= kPktsPerXfer;
    if (!s_gone && s_pktsLeft > 0 && t->status == USB_TRANSFER_STATUS_COMPLETED) {
        fill(t);
        if (usb_host_transfer_submit(t) == ESP_OK) return;
    }
    s_inFlight--;
}

static void playTone(const Out& o)
{
    if (usb_host_interface_claim(s_client, s_dev, o.iface, o.alt) != ESP_OK) {
        ESP_LOGW(TAG, "  claim of interface %d alt %d failed", o.iface, o.alt);
        return;
    }
    if (!setInterface(o.iface, o.alt)) goto release;
    {
        const uint8_t rate[3] = {kRate & 0xFF, (kRate >> 8) & 0xFF, kRate >> 16};
        // SET_CUR sampling frequency on the endpoint. Optional: a single-rate
        // device may stall it, which is fine.
        control(0x22, 0x01, 0x0100, o.ep, rate, 3, "SET_CUR sample rate");

        usb_transfer_t* x[kXfers] = {};
        s_phase = 0; s_pktErrs = 0; s_xferErrs = 0;
        s_pktsLeft = kToneMs;   // one packet per 1 ms frame
        s_inFlight = 0;
        ESP_LOGI(TAG, "  playing 440 Hz for %d ms: interface %d alt %d, OUT 0x%02X, %d-byte packets",
                 kToneMs, o.iface, o.alt, o.ep, kPktBytes);
        for (int i = 0; i < kXfers; i++) {
            if (usb_host_transfer_alloc(kPktsPerXfer * kPktBytes, kPktsPerXfer, &x[i]) != ESP_OK) break;
            x[i]->device_handle = s_dev;
            x[i]->bEndpointAddress = o.ep;
            x[i]->callback = isoCb;
            fill(x[i]);
            if (usb_host_transfer_submit(x[i]) != ESP_OK) {
                ESP_LOGW(TAG, "  isochronous submit failed");
                break;
            }
            s_inFlight++;
        }
        const TickType_t start = xTaskGetTickCount();
        while (s_inFlight > 0 && xTaskGetTickCount() - start < pdMS_TO_TICKS(kToneMs + 1000))
            vTaskDelay(pdMS_TO_TICKS(10));
        const bool hung = s_inFlight > 0;
        ESP_LOGI(TAG, "  tone done: %d packet error(s), %d transfer error(s)%s", s_pktErrs.load(), s_xferErrs.load(),
                 hung ? ", transfers still in flight (not freed)" : "");
        if (!s_gone) setInterface(o.iface, 0);
        // A transfer still owned by the host stack can't be freed; leak it
        // rather than crash. Only happens if the host stopped calling back.
        if (!hung) for (auto* t : x) if (t) usb_host_transfer_free(t);
    }
release:
    if (!s_gone) usb_host_interface_release(s_client, s_dev, o.iface);
}

// ---- device handling -------------------------------------------------------------

static void probe(uint8_t addr)
{
    if (usb_host_device_open(s_client, addr, &s_dev) != ESP_OK) return;
    s_gone = false;
    const usb_config_desc_t* cfg;
    if (usb_host_get_active_config_descriptor(s_dev, &cfg) == ESP_OK) {
        // Only devices with an AudioStreaming interface; MIDI-only gear (the
        // DJ2Go) also has an AudioControl interface but nothing to play into.
        const uint8_t* b = (const uint8_t*)cfg;
        bool streaming = false;
        for (int off = 0; off < cfg->wTotalLength && b[off]; off += b[off])
            if (b[off + 1] == USB_B_DESCRIPTOR_TYPE_INTERFACE && b[off + 5] == 0x01 && b[off + 6] == 0x02)
                streaming = true;
        if (streaming) {
            ESP_LOGI(TAG, "audio device @%d:", addr);
            const Out o = dump(cfg);
            if (o.found) playTone(o);
            else ESP_LOGW(TAG, "  no 16-bit stereo 48 kHz isochronous output; not playing");
        }
    }
    usb_host_device_close(s_client, s_dev);
    s_dev = nullptr;
}

static void clientCb(const usb_host_client_event_msg_t* msg, void*)
{
    if (msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
        const uint8_t a = msg->new_dev.address;
        xQueueSend(s_newDev, &a, 0);
    } else if (msg->event == USB_HOST_CLIENT_EVENT_DEV_GONE && msg->dev_gone.dev_hdl == s_dev) {
        s_gone = true;
    }
}

// Transfer callbacks run in here, so the probe itself runs in another task.
static void clientTask(void*)
{
    for (;;) usb_host_client_handle_events(s_client, portMAX_DELAY);
}

static void probeTask(void*)
{
    for (;;) {
        uint8_t a;
        xQueueReceive(s_newDev, &a, portMAX_DELAY);
        probe(a);
    }
}

void begin()
{
    s_newDev = xQueueCreate(4, 1);
    s_ctrlDone = xSemaphoreCreateBinary();
    usb_host_client_config_t cc = {};
    cc.is_synchronous = false;
    cc.max_num_event_msg = 8;
    cc.async.client_event_callback = clientCb;
    ESP_ERROR_CHECK(usb_host_client_register(&cc, &s_client));
    xTaskCreatePinnedToCore(clientTask, "uac_client", 4096, nullptr, 5, nullptr, 0);
    xTaskCreatePinnedToCore(probeTask, "uac_probe", 4096, nullptr, 4, nullptr, 0);
}

} // namespace usb_audio_probe
