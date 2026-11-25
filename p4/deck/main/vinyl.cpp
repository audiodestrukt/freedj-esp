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
#include "vinyl_rules.h"
#include "usb_audio.h"

namespace vinyl {
namespace {
const char* TAG = "vinyl";

// The arm / lift / settle rules are src/vinyl_rules.h (host-tested).
constexpr int     kWindowMs  = 10;
// Our Serato Control Vinyl's first groove reads ~24.0-24.3 s of timecode
// (three needle drops on the bench, 2026-11-05), so that's track 0:00, as in
// Serato's absolute mode. Serial Z re-measures it for another record.
constexpr int32_t kStartMs   = 24000;

timecode::Decoder s_dec;
uint32_t s_rate;
uint32_t s_flags = 0;            // USB interfaces: L/R as recorded (the A1S codec needed SWITCH_PRIMARY)
volatile uint32_t s_wantFlags = 0xFFFFFFFF;
volatile bool s_enabled = true;
volatile bool s_absolute = true;
volatile bool s_thru = false;
volatile int32_t s_startMs = kStartMs;
portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
State s_state;

void task(void*)
{
    static int16_t buf[512 * 2];
    int64_t windowUs = 0;
    int16_t windowPeak = 0, shownPeak = 0;
    int peakAgeMs = 0;
    vinyl_rules::Window win;
    // The position table (3 MB in PSRAM): built here, before any input, or
    // the first decode builds it and the input ring overflows meanwhile.
    timecode::prebuildLut(timecode::Format::SeratoControlVinyl);
    while (usb_audio::read(buf, 512) > 0) {}   // drop anything that queued up
    for (;;) {
        const uint32_t rate = usb_audio::inputRate();
        if (!usb_audio::inputActive() || rate == 0) {
            if (s_rate) { ESP_LOGI(TAG, "input gone"); s_rate = 0; }
            win.forget();
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
                win.forget();
                ESP_LOGI(TAG, "decoding Serato Control Vinyl at %lu Hz (flags %lu)", (unsigned long)rate, (unsigned long)s_flags);
            }
            s_dec.begin((int)rate, timecode::Format::SeratoControlVinyl);
            s_dec.setFlags(s_flags);
        }
        if (s_thru) {
            // The audio task reads the input now; leave it alone. Coming back,
            // the gap reads as a needle lift: the decoder starts fresh.
            portENTER_CRITICAL(&s_mux);
            s_state.input = true;
            s_state.signal = false;
            s_state.speed = 0;
            s_state.settling = false;
            s_state.posMs = s_state.rawMs = -1;
            portEXIT_CRITICAL(&s_mux);
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
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
            const vinyl_rules::Record& r = win.step(now, windowPeak, s_dec, s_absolute);
            if (r.placed) ESP_LOGI(TAG, "needle down: bit lock in %d ms", r.settleMs);
            if (r.gaveUp) ESP_LOGW(TAG, "needle down: no bit lock in %d ms, following speed only", r.settleMs);
            if (r.armedNow) ESP_LOGI(TAG, "carrier: the record drives the deck now (peak %d, speed %.3f)", windowPeak, s_dec.speed());
            peakAgeMs += kWindowMs;
            if (windowPeak > shownPeak || peakAgeMs >= 100) { shownPeak = windowPeak; peakAgeMs = 0; }
            windowPeak = 0;
            portENTER_CRITICAL(&s_mux);
            s_state.input = true;
            s_state.armed = r.armed;
            s_state.signal = r.signal;
            s_state.speed = r.speed;
            s_state.settling = r.settling;
            s_state.drops = r.drops;
            s_state.locked = s_dec.locked();
            s_state.peak = shownPeak;
            s_state.rate = rate;
            s_state.flags = s_flags;
            s_state.rawMs = r.signal && s_dec.locked() && s_dec.position() >= 0
                                ? (int32_t)((int64_t)s_dec.position() * 1000 / s_dec.resolutionHz()) : -1;
            s_state.posMs = s_state.rawMs >= s_startMs ? s_state.rawMs - s_startMs : -1;
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
    if (!s_enabled || s_thru) s.armed = false;
    s.absolute = s_absolute;
    return s;
}

void setEnabled(bool on) { s_enabled = on; }
void setAbsolute(bool on) { s_absolute = on; }
int32_t startMs() { return s_startMs; }
void setStartHere()
{
    const State s = state();
    if (s.rawMs >= 0) s_startMs = s.rawMs;
    ESP_LOGI(TAG, "record start %s: %ld ms of timecode", s.rawMs >= 0 ? "set" : "unchanged (no bit lock)", (long)s_startMs);
}
bool absolute() { return s_absolute; }
Mode mode() { return s_thru ? Mode::Thru : !s_enabled ? Mode::Int : s_absolute ? Mode::Abs : Mode::Rel; }
void cycleMode()
{
    switch (mode()) {
    case Mode::Abs: s_enabled = true;  s_absolute = false; break;   // -> REL
    case Mode::Rel: s_enabled = false; break;                       // -> INT
    case Mode::Int: s_thru = true;     break;                       // -> THRU
    case Mode::Thru: s_thru = false; s_enabled = true; s_absolute = true; break;   // -> ABS
    }
    static const char* kName[] = {"ABS", "REL", "INT", "THRU"};
    ESP_LOGI(TAG, "mode %s", kName[(int)mode()]);
}
void toggleThru()
{
    s_thru = !s_thru;   // the mode underneath is kept
    static const char* kName[] = {"ABS", "REL", "INT", "THRU"};
    ESP_LOGI(TAG, "mode %s", kName[(int)mode()]);
}
bool enabled() { return s_enabled; }

uint32_t cycleFlags()
{
    const uint32_t f = (s_flags + 1) & 7;
    s_wantFlags = f;
    return f;
}
} // namespace vinyl
