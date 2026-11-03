// SPDX-FileCopyrightText: 2026 Dan Newcome
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Panel controls wired straight to the P4 (release 1, no control board):
// CUE and PLAY buttons to GND on two GPIOs with internal pull-ups, and the
// pitch slide pot's wiper on an ADC pin. A task on core 0 turns them into the
// same deck::Events the DJ2Go sends (Cue, PlayPause, TempoFader).
#include "deck_event.h"

namespace panel_gpio {
using EventFn = void (*)(const deck::Event&);
void begin(EventFn onEvent);

struct Stats {
    bool cue, play;        // pressed now (after debounce)
    int  cuePresses, playPresses;
    int  raw;              // last pot reading, 0..4095 (4x oversampled)
    float filtered;        // after the IIR filter
    int  lo, hi;           // pot range seen so far
    float position;        // 0..1 as last sent
    int  sent;             // TempoFader events sent
    bool adc;              // false if the ADC didn't start
};
Stats stats();
} // namespace panel_gpio
