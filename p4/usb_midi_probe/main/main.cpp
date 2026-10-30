// USB MIDI probe for the ESP32-P4: logs every device that enumerates, claims
// the first MIDI streaming interface it finds, and prints each USB-MIDI event
// packet decoded to deck events by the shared DJ2Go decoder (src/dj2go.cpp).

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "usb/usb_host.h"
#include "dj2go.h"

static const char *TAG = "midi";

typedef struct {
    usb_host_client_handle_t client;
    usb_device_handle_t      dev;   // the MIDI device, if any
    usb_transfer_t          *xfer;
    uint8_t                  iface;
    bool                     gone;
} probe_t;

static probe_t P;

static dj2go::Decoder DECODER;

static void print_packet(const uint8_t *p)
{
    if ((p[0] & 0x0F) == 0) return;  // padding
    deck::Event ev[2];
    int n = DECODER.decodeUsbPacket(p, ev, 2);
    if (n == 0) { printf("%02X %02X %02X  (unmapped)\n", p[1], p[2], p[3]); return; }
    for (int i = 0; i < n; i++) {
        const deck::Event &e = ev[i];
        const char *dk = e.deck == deck::DeckA ? "A" : e.deck == deck::DeckB ? "B" : "-";
        printf("%02X %02X %02X  %s %-11s", p[1], p[2], p[3], dk, deck::typeName(e.type));
        switch (e.type) {
        case deck::Type::Jog: case deck::Type::Browse: printf(" %+d\n", e.delta); break;
        case deck::Type::TempoFader: case deck::Type::Volume: case deck::Type::Crossfader:
        case deck::Type::MasterVolume: case deck::Type::PhonesVolume: printf(" %.3f\n", e.position); break;
        default: printf(" %s\n", e.pressed ? "down" : "up");
        }
    }
}

static void xfer_cb(usb_transfer_t *t)
{
    if (t->status == USB_TRANSFER_STATUS_COMPLETED) {
        for (int i = 0; i + 3 < t->actual_num_bytes; i += 4) print_packet(t->data_buffer + i);
        if (!P.gone && usb_host_transfer_submit(t) == ESP_OK) return;
    } else {
        ESP_LOGW(TAG, "transfer ended, status %d", t->status);
    }
    usb_host_transfer_free(t);
    P.xfer = NULL;
}

// Walk the config descriptor for an Audio(1)/MIDI-streaming(3) interface and
// its bulk IN endpoint.
static bool find_midi_in(const usb_config_desc_t *cfg, uint8_t *iface, uint8_t *alt,
                         uint8_t *ep, uint16_t *mps)
{
    const uint8_t *b = (const uint8_t *)cfg;
    bool in_midi = false;
    for (int off = 0; off < cfg->wTotalLength && b[off]; off += b[off]) {
        const uint8_t *d = b + off;
        if (d[1] == USB_B_DESCRIPTOR_TYPE_INTERFACE) {
            in_midi = d[5] == 0x01 && d[6] == 0x03;
            if (in_midi) { *iface = d[2]; *alt = d[3]; }
        } else if (in_midi && d[1] == USB_B_DESCRIPTOR_TYPE_ENDPOINT &&
                   (d[2] & 0x80) && (d[3] & 0x03) == USB_BM_ATTRIBUTES_XFER_BULK) {
            *ep = d[2];
            *mps = d[4] | (d[5] << 8);
            return true;
        }
    }
    return false;
}

static void print_str(const char *what, const usb_str_desc_t *s)
{
    if (!s) return;
    char buf[64]; int n = 0;
    for (int i = 0; i < (s->bLength - 2) / 2 && n < (int)sizeof buf - 1; i++)
        buf[n++] = s->wData[i] < 128 ? (char)s->wData[i] : '?';
    buf[n] = 0;
    ESP_LOGI(TAG, "  %s: %s", what, buf);
}

static void on_new_dev(uint8_t addr)
{
    usb_device_handle_t dev;
    if (usb_host_device_open(P.client, addr, &dev) != ESP_OK) {
        ESP_LOGW(TAG, "addr %d: open failed", addr);
        return;
    }
    const usb_device_desc_t *dd; usb_device_info_t info;
    usb_host_get_device_descriptor(dev, &dd);
    usb_host_device_info(dev, &info);
    ESP_LOGI(TAG, "device @%d  %04X:%04X  class %02X  %s-speed", addr,
             dd->idVendor, dd->idProduct, dd->bDeviceClass,
             info.speed == USB_SPEED_HIGH ? "high" : info.speed == USB_SPEED_FULL ? "full" : "low");
    print_str("maker", info.str_desc_manufacturer);
    print_str("product", info.str_desc_product);

    const usb_config_desc_t *cfg;
    uint8_t iface, alt, ep; uint16_t mps;
    if (P.dev || usb_host_get_active_config_descriptor(dev, &cfg) != ESP_OK ||
        !find_midi_in(cfg, &iface, &alt, &ep, &mps)) {
        usb_host_device_close(P.client, dev);
        return;
    }
    ESP_LOGI(TAG, "  MIDI streaming: interface %d, bulk IN 0x%02X, %d-byte packets", iface, ep, mps);
    if (usb_host_interface_claim(P.client, dev, iface, alt) != ESP_OK) {
        ESP_LOGE(TAG, "  claim failed");
        usb_host_device_close(P.client, dev);
        return;
    }
    P.dev = dev; P.iface = iface; P.gone = false;
    usb_host_transfer_alloc(mps, 0, &P.xfer);
    P.xfer->device_handle = dev;
    P.xfer->bEndpointAddress = ep;
    P.xfer->callback = xfer_cb;
    P.xfer->num_bytes = mps;
    ESP_ERROR_CHECK(usb_host_transfer_submit(P.xfer));
    ESP_LOGI(TAG, "  listening — move some controls");
}

static void client_cb(const usb_host_client_event_msg_t *msg, void *arg)
{
    if (msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
        on_new_dev(msg->new_dev.address);
    } else if (msg->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
        ESP_LOGI(TAG, "device removed");
        if (msg->dev_gone.dev_hdl == P.dev) {
            P.gone = true;
            usb_host_interface_release(P.client, P.dev, P.iface);
            usb_host_device_close(P.client, P.dev);
            P.dev = NULL;
        }
    }
}

static void client_task(void *arg)
{
    usb_host_client_config_t cc = {};
    cc.is_synchronous = false;
    cc.max_num_event_msg = 8;
    cc.async.client_event_callback = client_cb;
    ESP_ERROR_CHECK(usb_host_client_register(&cc, &P.client));
    for (;;) usb_host_client_handle_events(P.client, portMAX_DELAY);
}

extern "C" void app_main(void)
{
    usb_host_config_t hc = {};
    hc.intr_flags = ESP_INTR_FLAG_LEVEL1;
    ESP_ERROR_CHECK(usb_host_install(&hc));
    ESP_LOGI(TAG, "USB host up (high-speed controller); plug in the DJ2Go");
    xTaskCreate(client_task, "usb_client", 4096, NULL, 5, NULL);
    for (;;) {
        uint32_t flags;
        usb_host_lib_handle_events(portMAX_DELAY, &flags);
    }
}
