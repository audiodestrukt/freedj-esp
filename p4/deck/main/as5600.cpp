// SPDX-FileCopyrightText: 2026 Dan Newcome
// SPDX-License-Identifier: GPL-3.0-or-later
// AS5600 jog encoder. See as5600.h.
//
// I2C (preferred): RAW ANGLE, 12 bits over the turn, no ADC noise. Read at
// 500 Hz from this core-0 task; the bus driver serialises it with the codec
// and touch (never touched from the audio task).
// Analog (fallback, when nothing answers at 0x36): the OUT pin is 0..VCC over
// a turn. The P4's ADC wanders ~±40 counts, so that path is smoothed, and its
// range is learned from what's seen (lo/hi), so the first turn calibrates it.
#include "as5600.h"
#include "board.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace as5600 {
namespace {
const char* TAG = "as5600";

// Ticks per revolution. At kScrubFrames (10 ms of track per tick) 180 ticks
// is 1.8 s of track per turn, about a record at 33 1/3 rpm.
constexpr float kTicksPerRev = 180.0f;
constexpr int   kOversample  = 2;      // ADC reads averaged per sample (~140 us each on the P4)
constexpr int   kPeriodMs    = 2;      // 500 Hz: ~6 % of core 0
// Smoothing on the unwrapped position, per sample: the ADC wanders ~±40 counts
// (~±2 ticks) at rest. 0.15 at 500 Hz is a ~13 ms time constant.
constexpr float kSmooth      = 0.15f;
constexpr int   kMinSpan     = 1500;   // don't trust the learned range below this

EventFn s_onEvent;
i2c_master_dev_handle_t s_dev;     // set: I2C mode
adc_oneshot_unit_handle_t s_adc;
adc_channel_t s_ch;
volatile int s_raw, s_lo = 4095, s_hi = 0, s_ticks;
volatile bool s_active;
volatile int s_readUs;    // time for one reading
volatile int s_lost;      // samples skipped: no magnet / I2C error
int16_t* s_cap;           // capture(): raw samples, -1 = lost
volatile int s_capN, s_capI;

int readAvg()
{
    int sum = 0, n = 0;
    for (int i = 0; i < kOversample; i++) {
        int v;
        if (adc_oneshot_read(s_adc, s_ch, &v) == ESP_OK) { sum += v; n++; }
    }
    return n ? sum / n : -1;
}

// I2C: the 12-bit raw angle, or -1 if the read fails or the magnet isn't seen.
int readI2c()
{
    static int n;
    uint8_t reg = 0x0B, st = 0x20, b[2];
    if (++n % 50 == 0 && (i2c_master_transmit_receive(s_dev, &reg, 1, &st, 1, 5) != ESP_OK || !(st & 0x20)))
        return -1;                     // status every 100 ms: no magnet, hold still
    reg = 0x0C;
    if (i2c_master_transmit_receive(s_dev, &reg, 1, b, 2, 5) != ESP_OK) return -1;
    return ((b[0] & 0x0F) << 8) | b[1];
}

int sample() { return s_dev ? readI2c() : readAvg(); }

void task(void*)
{
    const bool i2c = s_dev != nullptr;
    const float a = i2c ? 1.0f : kSmooth;   // I2C is clean enough to use as read
    int prev;
    while ((prev = sample()) < 0) vTaskDelay(pdMS_TO_TICKS(100));
    float pos = 0;          // ticks, continuous
    float smooth = 0;       // pos, low-passed
    bool reseed = false;
    int emitted = 0;        // ticks sent; pos must move a whole tick past it to send another
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(kPeriodMs));   // always yield: the ADC reads aren't free, and core 0's idle task needs time
        const int64_t t0 = esp_timer_get_time();
        const int raw = sample();
        s_readUs = (int)(esp_timer_get_time() - t0);
        if (s_capI < s_capN) s_cap[s_capI++] = (int16_t)raw;
        if (raw < 0) { s_lost++; reseed = true; continue; }
        if (reseed) { prev = raw; reseed = false; }   // magnet back: no jump from where it was
        s_raw = raw;
        if (raw < s_lo) s_lo = raw;
        if (raw > s_hi) s_hi = raw;
        const int span = i2c ? 4096 : (s_hi - s_lo) > kMinSpan ? (s_hi - s_lo) : 4095;

        int d = raw - prev;
        prev = raw;
        // Crossing the 0/1 turn point jumps the whole range: unwrap it.
        if (d > span / 2) d -= span;
        else if (d < -span / 2) d += span;
        pos += d * kTicksPerRev / span;
        smooth += a * (pos - smooth);

        // Whole-tick hysteresis: ADC noise around a boundary can't chatter.
        int out = 0;
        while (smooth >= emitted + 1) { emitted++; out++; }
        while (smooth <= emitted - 1) { emitted--; out--; }
        if (out) {
            s_ticks += out;
            deck::Event e;
            e.type = deck::Type::Jog;
            e.deck = deck::DeckA;
            e.delta = (int16_t)out;
            s_onEvent(e);
        }
    }
}
} // namespace

void begin(EventFn onEvent, i2c_master_bus_handle_t bus)
{
    s_onEvent = onEvent;
    if (bus) {
        i2c_device_config_t dc = {};
        dc.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        dc.device_address = 0x36;
        dc.scl_speed_hz = 400000;
        uint8_t reg = 0x0B, st;
        if (i2c_master_bus_add_device(bus, &dc, &s_dev) == ESP_OK &&
            i2c_master_transmit_receive(s_dev, &reg, 1, &st, 1, 20) == ESP_OK) {
            s_active = true;
            xTaskCreatePinnedToCore(task, "jog", 3072, nullptr, 5, nullptr, 0);
            ESP_LOGI(TAG, "jog on I2C 0x36, magnet %s, %.0f ticks/rev", (st & 0x20) ? "detected" : "NOT detected", kTicksPerRev);
            return;
        }
        if (s_dev) { i2c_master_bus_rm_device(s_dev); s_dev = nullptr; }
        ESP_LOGI(TAG, "no AS5600 on I2C, trying analog on GPIO %d", board::JOG_ADC);
    }
    adc_unit_t unit;
    if (adc_oneshot_io_to_channel(board::JOG_ADC, &unit, &s_ch) != ESP_OK) {
        ESP_LOGE(TAG, "GPIO %d has no ADC channel", board::JOG_ADC);
        return;
    }
    adc_oneshot_unit_init_cfg_t ucfg = {};
    ucfg.unit_id = unit;
    if (adc_oneshot_new_unit(&ucfg, &s_adc) != ESP_OK) { ESP_LOGE(TAG, "ADC unit init failed"); return; }
    adc_oneshot_chan_cfg_t ccfg = {};
    ccfg.atten = ADC_ATTEN_DB_12;          // full 0..~3.1 V
    ccfg.bitwidth = ADC_BITWIDTH_12;
    if (adc_oneshot_config_channel(s_adc, s_ch, &ccfg) != ESP_OK) { ESP_LOGE(TAG, "ADC channel config failed"); return; }
    s_active = true;
    xTaskCreatePinnedToCore(task, "jog", 3072, nullptr, 5, nullptr, 0);
    ESP_LOGI(TAG, "jog on GPIO %d (ADC%d ch %d), %.0f ticks/rev", board::JOG_ADC, (int)unit + 1, (int)s_ch, kTicksPerRev);
}

void probe(i2c_master_bus_handle_t bus)
{
    i2c_device_config_t dc = {};
    dc.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dc.device_address = 0x36;
    dc.scl_speed_hz = 100000;
    i2c_master_dev_handle_t dev;
    if (i2c_master_bus_add_device(bus, &dc, &dev) != ESP_OK) { ESP_LOGE(TAG, "probe: add device failed"); return; }
    auto rd = [&](uint8_t reg, uint8_t* out, size_t n) {
        return i2c_master_transmit_receive(dev, &reg, 1, out, n, 50);
    };
    uint8_t st = 0, agc = 0, mag[2] = {}, raw[2] = {}, conf[2] = {}, zmco = 0;
    if (rd(0x0B, &st, 1) != ESP_OK) {
        ESP_LOGE(TAG, "probe: no answer at 0x36 (SDA on pin 3, SCL on pin 5?)");
    } else {
        rd(0x00, &zmco, 1); rd(0x07, conf, 2); rd(0x0C, raw, 2); rd(0x1A, &agc, 1); rd(0x1B, mag, 2);
        ESP_LOGI(TAG, "probe: magnet %s%s%s (status 0x%02x), AGC %u (lower = stronger field), magnitude %u, raw angle %u/4095",
                 (st & 0x20) ? "DETECTED" : "NOT detected", (st & 0x10) ? ", TOO WEAK" : "", (st & 0x08) ? ", TOO STRONG" : "",
                 st, agc, ((mag[0] & 0x0F) << 8) | mag[1], ((raw[0] & 0x0F) << 8) | raw[1]);
        const unsigned outs = (conf[1] >> 4) & 3;
        ESP_LOGI(TAG, "probe: CONF 0x%02x%02x, output %s, burned %u times",
                 conf[0], conf[1], outs == 0 ? "analog 0-100%" : outs == 1 ? "analog 10-90%" : "PWM", zmco & 3);
    }
    i2c_master_bus_rm_device(dev);
}

int capture(int n, const int16_t** out)
{
    if (!s_active) return 0;
    if (!s_cap) s_cap = (int16_t*)heap_caps_malloc(kCaptureMax * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (!s_cap) return 0;
    if (n > kCaptureMax) n = kCaptureMax;
    s_capI = 0;
    s_capN = n;
    while (s_capI < n) vTaskDelay(pdMS_TO_TICKS(20));
    s_capN = 0;
    *out = s_cap;
    return n;
}

Stats stats() { return {s_raw, s_lo, s_hi, s_ticks, s_readUs, s_lost, s_active, s_dev != nullptr}; }
} // namespace as5600
