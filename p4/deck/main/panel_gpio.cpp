// SPDX-FileCopyrightText: 2026 Dan Newcome
// SPDX-License-Identifier: GPL-3.0-or-later
// Panel controls straight on P4 GPIOs. See panel_gpio.h.
#include "panel_gpio.h"
#include <math.h>
#include <stdlib.h>
#include <initializer_list>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "board.h"

namespace panel_gpio {
namespace {
const char* TAG = "panel";

// Loop period. Buttons are sampled each pass; a new level must hold for
// kDebounce passes (6 ms) to count.
constexpr int kPeriodMs = 2;
constexpr int kDebounce = 3;
// Pot: read every 20 ms (50 Hz), 8 readings averaged, then a low-pass that
// is slow at rest (~0.5 Hz, hides the ADC's +-60 count noise) and fast while
// the fader moves (~6 Hz). (16 reads every 10 ms cost as much CPU as LVGL.)
constexpr int   kPotEvery  = 10;         // passes
constexpr int   kPotOver   = 8;
constexpr float kAlphaRest = 0.06f;
constexpr float kAlphaMove = 0.55f;
constexpr float kMoveAt    = 40.0f;      // raw this far from the filtered value...
constexpr int   kMoveReads = 3;          // ...this many reads in a row = moving (a lone spike isn't)
// Send a new fader position only when the filtered reading moves this far
// (12-bit counts, about 0.3 % of the travel), so a fader at rest sends nothing.
constexpr float kPotDeadband = 12.0f;
// Ignore the pot until it has moved this far since boot: an unwired, floating
// ADC pin must not set the tempo, and the DJ2Go's fader keeps working until
// the panel's own fader is touched.
constexpr int kPotArm = 200;
// Starting range, deliberately narrow; widened to whatever the pot actually
// reaches (the test fader's top end read 3292, not ~4095), so both ends of the
// travel give 0 and 1 after one full sweep.
constexpr int kPotLo = 300, kPotHi = 3000;

EventFn s_onEvent;
adc_oneshot_unit_handle_t s_adc;
adc_channel_t s_ch;
Stats s_st;

struct Button {
    gpio_num_t pin;
    deck::Type type;
    bool stable = false;   // pressed
    int  count = 0;
};

void send(deck::Type t, bool pressed, float position = 0)
{
    deck::Event e;
    e.type = t;
    e.deck = deck::DeckA;
    e.pressed = pressed;
    e.position = position;
    s_onEvent(e);
}

void task(void*)
{
    Button btn[2] = {{board::PANEL_CUE, deck::Type::Cue}, {board::PANEL_PLAY, deck::Type::PlayPause}};
    float filt = -1;
    float sentAt = -1000;
    int first = -1;
    bool armed = false;
    int lo = kPotLo, hi = kPotHi;
    int far = 0;
    for (int pass = 0;; pass++) {
        for (Button& b : btn) {
            const bool down = gpio_get_level(b.pin) == 0;   // to GND when pressed
            if (down == b.stable) { b.count = 0; continue; }
            if (++b.count < kDebounce) continue;
            b.stable = down;
            b.count = 0;
            send(b.type, down);
            if (down) (b.type == deck::Type::Cue ? s_st.cuePresses : s_st.playPresses)++;
        }
        s_st.cue = btn[0].stable;
        s_st.play = btn[1].stable;

        if (s_adc && pass % kPotEvery == 0) {
            int sum = 0, n = 0;
            for (int i = 0; i < kPotOver; i++) {
                int v;
                if (adc_oneshot_read(s_adc, s_ch, &v) == ESP_OK) { sum += v; n++; }
            }
            if (n) {
                const int raw = sum / n;
                s_st.raw = raw;
                far = filt >= 0 && fabsf(raw - filt) > kMoveAt ? far + 1 : 0;
                const float a = far >= kMoveReads ? kAlphaMove : kAlphaRest;
                filt = filt < 0 ? raw : filt + a * (raw - filt);
                s_st.filtered = filt;
                if (first < 0) first = raw;
                if (!armed && abs(raw - first) > kPotArm) {
                    armed = true;
                    ESP_LOGI(TAG, "pitch fader moved: following it");
                }
                if (filt < lo) lo = (int)filt;
                if (filt > hi) hi = (int)filt;
                s_st.lo = lo;
                s_st.hi = hi;
                if (armed && fabsf(filt - sentAt) >= kPotDeadband) {
                    sentAt = filt;
                    float p = (filt - lo) / (float)(hi - lo);
                    p = p < 0 ? 0 : (p > 1 ? 1 : p);
                    s_st.position = p;
                    s_st.sent++;
                    send(deck::Type::TempoFader, false, p);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(kPeriodMs));
    }
}
} // namespace

void begin(EventFn onEvent)
{
    s_onEvent = onEvent;
    // GPIO 24/25 are the USB serial/JTAG pins; setting them as GPIO turns its pad off.
    for (gpio_num_t p : {board::PANEL_CUE, board::PANEL_PLAY}) {
        gpio_reset_pin(p);
        gpio_config_t c = {};
        c.pin_bit_mask = 1ULL << p;
        c.mode = GPIO_MODE_INPUT;
        c.pull_up_en = GPIO_PULLUP_ENABLE;
        gpio_config(&c);
    }
    adc_unit_t unit = ADC_UNIT_1;
    if (adc_oneshot_io_to_channel(board::PANEL_PITCH, &unit, &s_ch) == ESP_OK) {
        adc_oneshot_unit_init_cfg_t ucfg = {};
        ucfg.unit_id = unit;
        adc_oneshot_chan_cfg_t ccfg = {};
        ccfg.atten = ADC_ATTEN_DB_12;      // full 0..~3.1 V
        ccfg.bitwidth = ADC_BITWIDTH_12;
        if (adc_oneshot_new_unit(&ucfg, &s_adc) == ESP_OK &&
            adc_oneshot_config_channel(s_adc, s_ch, &ccfg) == ESP_OK) {
            s_st.adc = true;
        } else {
            s_adc = nullptr;
            ESP_LOGE(TAG, "pitch ADC on GPIO %d didn't start (ADC unit in use?)", board::PANEL_PITCH);
        }
    } else {
        ESP_LOGE(TAG, "GPIO %d has no ADC channel", board::PANEL_PITCH);
    }
    xTaskCreatePinnedToCore(task, "panel", 3072, nullptr, 4, nullptr, 0);
    ESP_LOGI(TAG, "CUE GPIO %d, PLAY GPIO %d, pitch GPIO %d (ADC%d ch %d)", board::PANEL_CUE, board::PANEL_PLAY,
             board::PANEL_PITCH, (int)unit + 1, (int)s_ch);
}

Stats stats() { return s_st; }
} // namespace panel_gpio
