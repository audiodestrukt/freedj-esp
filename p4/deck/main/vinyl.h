// SPDX-FileCopyrightText: 2026 Dan Newcome
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Timecode vinyl on the P4: Serato Control Vinyl from a USB audio
// interface's line in (usb_audio input), decoded by the shared decoder in
// ../../src/timecode.cpp. The record sets the playback speed and direction.
// Absolute mode (the default) also follows its position: a needle drop
// jumps the track to the same place (track 0:00 = the record's start).
// Relative mode ignores position.
//
// It takes over only after it has heard a steady carrier (armed), so an
// interface plugged in just for its output, or playing music into its
// input, never stops the deck. Once armed, no carrier = stopped record.
#include <stdint.h>

namespace vinyl {
struct State {
    bool     input;      // a USB audio input is streaming
    bool     armed;      // heard a steady carrier since the input appeared
    bool     signal;     // carrier present right now
    float    speed;      // signed, 1.0 = 33 1/3 at 0 %; 0 with no signal
    bool     locked;     // decoder bit lock (position-grade; speed doesn't need it)
    int16_t  peak;       // max |sample| over the last ~100 ms
    uint32_t rate;       // input Hz
    uint32_t flags;      // decoder SWITCH_* flags
    int32_t  posMs = -1; // place on the record (ms at 33 1/3), -1 without bit lock
    bool     absolute;   // follow posMs (needle drops seek)
};

void begin();
State state();
// Off: the deck ignores the record (serial V).
void setEnabled(bool on);
bool enabled();
// True when the record should drive the deck now.
inline bool driving(const State& s) { return s.input && s.armed; }
// Cycle the decoder's SWITCH_PHASE / PRIMARY / POLARITY flags (serial f):
// for a backwards record or channels swapped in the cabling.
uint32_t cycleFlags();
// Absolute / relative (serial A).
void setAbsolute(bool on);
bool absolute();
} // namespace vinyl
