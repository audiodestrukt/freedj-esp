// SPDX-FileCopyrightText: 2026 Dan Newcome
// SPDX-License-Identifier: GPL-3.0-or-later
#include "xdj_midi.h"

namespace xdj {

using deck::Event;
using deck::Type;
using deck::DeckA;

namespace {

// Buttons: Note On 127 on press, 0 on release. `arg` goes in delta (or, for
// beat loops, position = beats * 2 so 1/2 fits).
struct NoteMap { uint8_t note; Type type; int8_t arg; };
const NoteMap kNotes[] = {
    {0x00, Type::PlayPause, 0},   {0x01, Type::Cue, 0},
    {0x06, Type::LoopIn, 0},      {0x07, Type::LoopOut, 0},     {0x08, Type::ReloopExit, 0},
    {0x09, Type::Quantize, 0},
    {0x0A, Type::MemoryAdd, 0},   {0x0B, Type::CueCall, +1},    {0x0C, Type::CueCall, -1},
    {0x0D, Type::MemoryDelete, 0},
    {0x10, Type::TempoRange, 0},  {0x11, Type::KeyLock, 0},     // MASTER TEMPO
    {0x18, Type::HotCue, 1}, {0x19, Type::HotCue, 2}, {0x1A, Type::HotCue, 3}, {0x1B, Type::HotCue, 4},
    {0x14, Type::HotCue, 5}, {0x15, Type::HotCue, 6}, {0x16, Type::HotCue, 7}, {0x17, Type::HotCue, 8},
    {0x1F, Type::Sync, 0},
    {0x21, Type::Reverse, 0},     // DIRECTION REV
    {0x27, Type::BeatLoop, 1}, {0x28, Type::BeatLoop, 2}, {0x29, Type::BeatLoop, 4},
    {0x2A, Type::BeatLoop, 8}, {0x2B, Type::BeatLoop, 16}, {0x2C, Type::BeatLoop, 32},
    {0x32, Type::Back, 0},        {0x33, Type::Load, 0},        // ROTARY SELECTOR PUSH
    {0x3A, Type::Slip, 0},
    {0x38, Type::BeatJump, 1},  {0x39, Type::BeatJump, -1},
    {0x3C, Type::BeatJump, 2},  {0x3D, Type::BeatJump, -2},
    {0x3E, Type::BeatJump, 4},  {0x3F, Type::BeatJump, -4},
    {0x40, Type::BeatJump, 8},  {0x41, Type::BeatJump, -8},
    {0x42, Type::BeatJump, 16}, {0x43, Type::BeatJump, -16},
    {0x44, Type::BeatJump, 32}, {0x45, Type::BeatJump, -32},
};

constexpr uint8_t kJogTouch = 0x10;   // CC: speed while the top is touched, 0.06X..4X
constexpr uint8_t kJogRing  = 0x30;   // CC: speed of the rim, 0.5X..4X
constexpr uint8_t kTempo    = 0x1D;   // CC: 0 = - side .. 127 = + side
constexpr uint8_t kRotary   = 0x4F;   // CC: count since last, 1..30 / 98..127 (-30..-1)

// Speed in "X" (1 = normal play) from a jog CC, sign = direction.
float speedOf(uint8_t cc, uint8_t v)
{
    if (v == 64) return 0;
    const bool fwd = v > 64;
    const int step = fwd ? v - 64 : 64 - v;          // 1..63 (or 64 at 0)
    float x = cc == kJogTouch ? step * (4.0f / 63.0f)   // 0.06X .. 4X
                              : 0.5f + (step - 1) * (3.5f / 62.0f);   // 0.5X .. 4X
    if (x > 4.0f) x = 4.0f;
    return fwd ? x : -x;
}

} // namespace

int Decoder::decode(uint8_t status, uint8_t d1, uint8_t d2, Event* out, int max)
{
    if (max < 1) return 0;
    const uint8_t kind = status & 0xF0;
    if (kind == 0x90 || kind == 0x80) {
        const bool pressed = kind == 0x90 && d2 > 0;
        for (const NoteMap& m : kNotes) {
            if (m.note != d1) continue;
            Event e; e.type = m.type; e.deck = DeckA; e.pressed = pressed;
            if (m.type == Type::BeatLoop) e.position = m.arg / 2.0f;
            else e.delta = m.arg;
            out[0] = e;
            return 1;
        }
        return 0;
    }
    if (kind != 0xB0) return 0;
    Event e; e.deck = DeckA;
    switch (d1) {
    case kJogTouch:
    case kJogRing: {
        carry_ += speedOf(d1, d2) * kMsgMs / kTickMs;
        const int ticks = (int)carry_;               // toward zero; the rest carries
        carry_ -= ticks;
        if (d2 == 64) carry_ = 0;                    // stopped: drop the remainder
        if (!ticks) return 0;
        e.type = Type::Jog;
        e.delta = (int16_t)ticks;
        break;
    }
    case kTempo:
        e.type = Type::TempoFader;
        e.position = (d2 == 63 || d2 == 64) ? 0.5f : d2 / 127.0f;   // + side = faster, as the deck expects
        break;
    case kRotary:
        e.type = Type::Browse;
        e.deck = deck::DeckAny;
        e.delta = d2 >= 64 ? (int16_t)(d2 - 128) : (int16_t)d2;
        break;
    default:
        return 0;
    }
    out[0] = e;
    return 1;
}

int Decoder::decodeUsbPacket(const uint8_t p[4], Event* out, int max)
{
    const uint8_t cin = p[0] & 0x0F;
    if (cin != 0x8 && cin != 0x9 && cin != 0xB) return 0;
    return decode(p[1], p[2], p[3], out, max);
}

} // namespace xdj
