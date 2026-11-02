// SPDX-FileCopyrightText: 2026 Dan Newcome
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Pioneer XDJ-1000MK2 in USB-MIDI control mode → deck events.
// Map: the "List of MIDI Messages" in the XDJ-1000MK2 operating instructions
// (p. 37), checked against the hardware 2026-11-02. On the player: connect USB,
// then [CONTROL MODE] → [USB MIDI]. Any MIDI channel is accepted.
// Transport-agnostic, like dj2go::Decoder.

#include "deck_event.h"

namespace xdj {

class Decoder {
public:
    // Decode one MIDI message. Writes up to `max` events and returns how many.
    int decode(uint8_t status, uint8_t d1, uint8_t d2, deck::Event* out, int max);
    int decodeUsbPacket(const uint8_t p[4], deck::Event* out, int max);

    // The jog sends a speed (64 = stopped) about every kMsgMs while it moves,
    // not a count. Speed x time becomes track time, which becomes jog ticks
    // (deck ticks are 10 ms of track), with the remainder carried over.
    static constexpr float kMsgMs  = 18.0f;
    static constexpr float kTickMs = 10.0f;

private:
    float carry_ = 0;     // fractional ticks not yet sent
};

} // namespace xdj
