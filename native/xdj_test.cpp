// SPDX-FileCopyrightText: 2026 Dan Newcome
// SPDX-License-Identifier: GPL-3.0-or-later
// XDJ-1000MK2 USB-MIDI decoder test: make xdj_test && ./xdj_test
#include <stdio.h>
#include "../src/xdj_midi.h"
using namespace deck;
static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main()
{
    xdj::Decoder d; Event e[2];
    CHECK(d.decode(0x90, 0x00, 127, e, 2) == 1 && e[0].type == Type::PlayPause && e[0].pressed);
    CHECK(d.decode(0x90, 0x00, 0, e, 2) == 1 && !e[0].pressed);
    CHECK(d.decode(0x90, 0x01, 127, e, 2) == 1 && e[0].type == Type::Cue);
    CHECK(d.decode(0x90, 0x18, 127, e, 2) == 1 && e[0].type == Type::HotCue && e[0].delta == 1);
    CHECK(d.decode(0x90, 0x17, 127, e, 2) == 1 && e[0].type == Type::HotCue && e[0].delta == 8);
    CHECK(d.decode(0x90, 0x27, 127, e, 2) == 1 && e[0].type == Type::BeatLoop && e[0].position == 0.5f);
    CHECK(d.decode(0x90, 0x2C, 127, e, 2) == 1 && e[0].position == 16.0f);
    CHECK(d.decode(0x90, 0x45, 127, e, 2) == 1 && e[0].type == Type::BeatJump && e[0].delta == -32);
    CHECK(d.decode(0x90, 0x20, 127, e, 2) == 0);                    // jog touch: no event of its own
    CHECK(d.decode(0xB0, 0x1D, 127, e, 2) == 1 && e[0].type == Type::TempoFader && e[0].position == 1.0f);
    CHECK(d.decode(0xB0, 0x1D, 64, e, 2) == 1 && e[0].position == 0.5f);
    CHECK(d.decode(0xB0, 0x4F, 3, e, 2) == 1 && e[0].type == Type::Browse && e[0].delta == 3);
    CHECK(d.decode(0xB0, 0x4F, 126, e, 2) == 1 && e[0].delta == -2);
    // jog at 4X forward: 4 * 18 ms = 72 ms of track = 7 ticks, 0.2 carried
    CHECK(d.decode(0xB0, 0x10, 127, e, 2) == 1 && e[0].type == Type::Jog && e[0].delta == 7);
    CHECK(d.decode(0xB0, 0x10, 64, e, 2) == 0);                     // stop drops the carry
    // slow reverse: 0.06X per message is ~0.11 ticks; nine messages make one tick
    int sum = 0;
    for (int i = 0; i < 9; i++) if (d.decode(0xB0, 0x10, 63, e, 2)) sum += e[0].delta;
    CHECK(sum == -1);
    CHECK(d.decode(0xB0, 0x30, 127, e, 2) == 1 && e[0].delta == 7); // ring 4X
    // USB packet wrapper
    const uint8_t pkt[4] = {0x09, 0x90, 0x01, 0x7F};
    CHECK(d.decodeUsbPacket(pkt, e, 2) == 1 && e[0].type == Type::Cue);
    printf(fails ? "%d failed\n" : "xdj_test: all passed\n", fails);
    return fails != 0;
}
