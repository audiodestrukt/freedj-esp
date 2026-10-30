// Host test for the DJ2Go MIDI decoder (src/dj2go.cpp).
// Messages are the ones the P4 USB probe logged from the real controller,
// plus the documented map in freedj-3000 docs/reference/dj2go-midi-map.md.
#include "dj2go.h"
#include <cmath>
#include <cstdio>

using deck::Event;
using deck::Type;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static int run(dj2go::Decoder& d, uint8_t s, uint8_t a, uint8_t b, Event* out)
{
    return d.decode(s, a, b, out, 2);
}

int main()
{
    dj2go::Decoder d;
    Event e[2];

    // Startup dump captured on the P4: pitch A centre, crossfader full right.
    CHECK(run(d, 0xB0, 0x0D, 0x40, e) == 1);
    CHECK(e[0].type == Type::TempoFader && e[0].deck == deck::DeckA);
    CHECK(e[0].position == 0.5f);                      // centre detent
    CHECK(run(d, 0xB0, 0x0E, 0x3F, e) == 1 && e[0].position == 0.5f);  // B centres at 63
    CHECK(run(d, 0xB0, 0x0D, 0x00, e) == 1 && e[0].position == 1.0f);  // inverted
    CHECK(run(d, 0xB0, 0x0D, 0x41, e) == 1 && std::fabs(e[0].position - (1.0f - 65 / 127.0f)) < 1e-6f);
    CHECK(run(d, 0xB0, 0x0A, 0x7F, e) == 1);
    CHECK(e[0].type == Type::Crossfader && e[0].deck == deck::DeckAny && e[0].position == 1.0f);
    CHECK(run(d, 0xB0, 0x0E, 0x7F, e) == 1);
    CHECK(e[0].type == Type::TempoFader && e[0].deck == deck::DeckB && e[0].position == 0.0f);

    // Jog / browse are two's complement relative.
    CHECK(run(d, 0xB0, 0x19, 0x01, e) == 1 && e[0].type == Type::Jog && e[0].deck == deck::DeckA && e[0].delta == 1);
    CHECK(run(d, 0xB0, 0x18, 0x7F, e) == 1 && e[0].type == Type::Jog && e[0].deck == deck::DeckB && e[0].delta == -1);
    CHECK(run(d, 0xB0, 0x1A, 0x7D, e) == 1 && e[0].type == Type::Browse && e[0].delta == -3);

    // Play / cue: press and release (Note Off and Note On vel 0 both release).
    CHECK(run(d, 0x90, 0x3B, 0x7F, e) == 1 && e[0].type == Type::PlayPause && e[0].deck == deck::DeckA && e[0].pressed);
    CHECK(run(d, 0x80, 0x3B, 0x00, e) == 1 && e[0].type == Type::PlayPause && !e[0].pressed);
    CHECK(run(d, 0x90, 0x3C, 0x7F, e) == 1 && e[0].type == Type::Cue && e[0].deck == deck::DeckB && e[0].pressed);
    CHECK(run(d, 0x90, 0x3C, 0x00, e) == 1 && e[0].type == Type::Cue && !e[0].pressed);

    // LOAD tapped on its own: shift down, then shift up + load.
    CHECK(run(d, 0x90, 0x4B, 0x7F, e) == 1 && e[0].type == Type::Shift && e[0].pressed);
    CHECK(run(d, 0x80, 0x4B, 0x00, e) == 2);
    CHECK(e[0].type == Type::Shift && !e[0].pressed);
    CHECK(e[1].type == Type::Load && e[1].deck == deck::DeckA);

    // LOAD held as shift while jogging: release must NOT load.
    CHECK(run(d, 0x90, 0x34, 0x7F, e) == 1 && e[0].type == Type::Shift && e[0].deck == deck::DeckB);
    CHECK(run(d, 0xB0, 0x18, 0x02, e) == 1);
    CHECK(run(d, 0x80, 0x34, 0x00, e) == 1 && e[0].type == Type::Shift && !e[0].pressed);

    // Shift on deck A is not consumed by a deck B control.
    CHECK(run(d, 0x90, 0x4B, 0x7F, e) == 1);
    CHECK(run(d, 0x90, 0x42, 0x7F, e) == 1 && e[0].deck == deck::DeckB);
    CHECK(run(d, 0x80, 0x4B, 0x00, e) == 2 && e[1].type == Type::Load);

    // BACK / ENTER fire on Note Off only.
    CHECK(run(d, 0x90, 0x59, 0x7F, e) == 0);
    CHECK(run(d, 0x80, 0x59, 0x00, e) == 1 && e[0].type == Type::Back);
    CHECK(run(d, 0x80, 0x5A, 0x00, e) == 1 && e[0].type == Type::Load && e[0].deck == deck::DeckAny);

    // Unmapped messages produce nothing.
    CHECK(run(d, 0xB0, 0x7B, 0x00, e) == 0);
    CHECK(run(d, 0x90, 0x10, 0x7F, e) == 0);

    // USB-MIDI packet wrapper: CIN B = control change; CIN 0 = padding.
    const uint8_t pkt[4] = {0x0B, 0xB0, 0x19, 0x7E};
    CHECK(d.decodeUsbPacket(pkt, e, 2) == 1 && e[0].type == Type::Jog && e[0].delta == -2);
    const uint8_t pad[4] = {0, 0, 0, 0};
    CHECK(d.decodeUsbPacket(pad, e, 2) == 0);

    if (failures) { std::printf("dj2go_test: %d failure(s)\n", failures); return 1; }
    std::printf("dj2go_test: OK\n");
    return 0;
}
