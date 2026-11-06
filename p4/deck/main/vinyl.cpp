// SPDX-FileCopyrightText: 2026 Dan Newcome
// SPDX-License-Identifier: GPL-3.0-or-later
// Timecode vinyl from USB audio input. See vinyl.h.
#include "vinyl.h"

#include <math.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "timecode.h"
#include "usb_audio.h"

namespace vinyl {
namespace {
const char* TAG = "vinyl";

// Carrier test, per ~10 ms window: loud enough and the decoder's speed in
// a turntable's range.
constexpr int16_t kMinPeak   = 600;       // about -35 dBFS
constexpr float   kMaxSpeed  = 3.0f;
constexpr float   kMinSpeed  = 0.02f;
constexpr int     kArmMs     = 300;       // steady carrier near 1x this long arms it
constexpr int     kDropMs    = 40;        // no carrier this long = record stopped
constexpr int     kWindowMs  = 10;

timecode::Decoder s_dec;
uint32_t s_rate;
uint32_t s_flags = 0;            // USB interfaces: L/R as recorded (the A1S codec needed SWITCH_PRIMARY)
volatile uint32_t s_wantFlags = 0xFFFFFFFF;
volatile bool s_enabled = true;
volatile bool s_absolute = true;
portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
State s_state;

void task(void*)
{
    static int16_t buf[512 * 2];
    int64_t lastCarrierUs = 0, steadySinceUs = 0, windowUs = 0;
    int16_t windowPeak = 0, shownPeak = 0;
    int peakAgeMs = 0;
    bool armed = false, signal = false;
    // The position table (3 MB in PSRAM): built here, before any input, or
    // the first decode builds it and the input ring overflows meanwhile.
    timecode::prebuildLut(timecode::Format::SeratoControlVinyl);
    while (usb_audio::read(buf, 512) > 0) {}   // drop anything that queued up
    for (;;) {
        const uint32_t rate = usb_audio::inputRate();
        if (!usb_audio::inputActive() || rate == 0) {
            if (s_rate) { ESP_LOGI(TAG, "input gone"); s_rate = 0; }
            armed = signal = false;
            portENTER_CRITICAL(&s_mux);
            s_state = State{};
            s_state.flags = s_flags;
            portEXIT_CRITICAL(&s_mux);
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        if (rate != s_rate || s_wantFlags != 0xFFFFFFFF) {
            if (s_wantFlags != 0xFFFFFFFF) { s_flags = s_wantFlags; s_wantFlags = 0xFFFFFFFF; }
            if (rate != s_rate) {
                s_rate = rate;
                armed = false;
                ESP_LOGI(TAG, "decoding Serato Control Vinyl at %lu Hz (flags %lu)", (unsigned long)rate, (unsigned long)s_flags);
            }
            s_dec.begin((int)rate, timecode::Format::SeratoControlVinyl);
            s_dec.setFlags(s_flags);
        }
        const size_t n = usb_audio::read(buf, 512);
        for (size_t i = 0; i < n * 2; i++) {
            const int16_t a = (int16_t)abs(buf[i]);
            if (a > windowPeak) windowPeak = a;
        }
        if (n) s_dec.pushFrames(buf, (int)n);

        const int64_t now = esp_timer_get_time();
        if (now - windowUs >= kWindowMs * 1000) {
            windowUs = now;
            const float sp = s_dec.speed();
            const float m = fabsf(sp);
            const bool carrier = windowPeak >= kMinPeak && m >= kMinSpeed && m <= kMaxSpeed;
            if (carrier) lastCarrierUs = now;
            signal = now - lastCarrierUs < kDropMs * 1000;
            // Arm on a steady, roughly normal-speed carrier (a real record, not music).
            if (carrier && m > 0.6f && m < 1.5f) {   // 33 or 45 rpm
                if (!steadySinceUs) steadySinceUs = now;
                if (!armed && now - steadySinceUs >= kArmMs * 1000) {
                    armed = true;
                    ESP_LOGI(TAG, "carrier: the record drives the deck now (peak %d, speed %.3f)", windowPeak, sp);
                }
            } else {
                steadySinceUs = 0;
            }
            peakAgeMs += kWindowMs;
            if (windowPeak > shownPeak || peakAgeMs >= 100) { shownPeak = windowPeak; peakAgeMs = 0; }
            windowPeak = 0;
            portENTER_CRITICAL(&s_mux);
            s_state.input = true;
            s_state.armed = armed;
            s_state.signal = signal;
            s_state.speed = signal ? sp : 0.0f;
            s_state.locked = s_dec.locked();
            s_state.peak = shownPeak;
            s_state.rate = rate;
            s_state.flags = s_flags;
            s_state.posMs = signal && s_dec.locked() && s_dec.position() >= 0
                                ? (int32_t)((int64_t)s_dec.position() * 1000 / s_dec.resolutionHz()) : -1;
            portEXIT_CRITICAL(&s_mux);
        }
        if (n < 512) vTaskDelay(pdMS_TO_TICKS(2));
    }
}
} // namespace

void begin()
{
    xTaskCreatePinnedToCore(task, "vinyl", 4096, nullptr, 5, nullptr, 0);
}

State state()
{
    portENTER_CRITICAL(&s_mux);
    State s = s_state;
    portEXIT_CRITICAL(&s_mux);
    if (!s_enabled) s.armed = false;
    s.absolute = s_absolute;
    return s;
}

void setEnabled(bool on) { s_enabled = on; }
void setAbsolute(bool on) { s_absolute = on; }
bool absolute() { return s_absolute; }
bool enabled() { return s_enabled; }

uint32_t cycleFlags()
{
    const uint32_t f = (s_flags + 1) & 7;
    s_wantFlags = f;
    return f;
}
} // namespace vinyl
