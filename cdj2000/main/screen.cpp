// The link to the display processor, the main board's end: its transmit block
// at 0xFF500000 (as the emulators model it, display_link.c: the buffer's
// address at +0x1008, its length at +0x1010 in 16-byte units, bit 0 of
// +0x1028 sends; a unit-done status at +0x1188, acknowledged a unit at a
// time).  One 512-byte packet at a time (screen_link.h).
#include "screen.h"
#include "screen_link.h"
#include "engine.h"
#include "tmu.h"
#include "con.h"
#include <string.h>

namespace screen {
namespace {
constexpr uint32_t kTx = 0xFF500000u;
inline volatile uint32_t &reg(uint32_t o) { return *reinterpret_cast<volatile uint32_t *>(kTx + o); }
constexpr uint32_t ADDR = 0x1008, LEN = 0x1010, CTRL = 0x1028, STATUS = 0x1188;

alignas(32) uint8_t pkt[SL_PACKET];
uint16_t seq;
uint32_t generation, peaks_sent;
int64_t next_state;
int64_t quiet_until;                    // the link didn't answer: leave it until then
bool said_quiet;

bool send(uint16_t type, const void *body, uint16_t len)
{
    const int64_t now = tmu_us64();
    if (now < quiet_until) return false;
    // The last packet still going: a packet takes 1.4 ms on the wire.  Not
    // done in 5 ms, there is no display processor taking them (an emulator
    // run without its flash): try again in 2 s, and don't hold the deck up.
    while (reg(CTRL) & 1) {
        if (tmu_us64() - now > 5000) {
            quiet_until = tmu_us64() + 2000000;
            if (!said_quiet) { said_quiet = true; con_printf("screen: the display link is not answering\n"); }
            return false;
        }
    }
    for (int i = 0; i < SL_PACKET / 16 && (reg(STATUS) & 0x10); i++) reg(STATUS) = 0x10;   // its units, acknowledged
    memset(pkt, 0, sizeof pkt);
    sl_header h{SL_MAGIC, type, seq++, len};
    memcpy(pkt, &h, sizeof h);
    memcpy(pkt + sizeof h, body, len);
    reg(ADDR) = reinterpret_cast<uint32_t>(pkt) & 0x1FFFFFFFu;
    reg(LEN) = SL_PACKET;
    reg(CTRL) = 1;
    return true;
}
} // namespace

void service()
{
    const engine::View v = engine::view();
    if (!v.loaded) return;                                  // the display keeps its splash
    if (v.generation != generation) {                       // a new track: its title, length and grid first
        sl_track t{};
        t.length_ms = v.length_ms;
        t.first_beat_ms = v.first_beat_ms;
        t.beat_us = v.beat_us;
        t.peak_frames = v.peak_frames;
        t.rate = v.rate;
        t.peaks = v.peaks;
        strncpy(t.title, v.title ? v.title : "", sizeof t.title - 1);
        if (!send(SL_TRACK, &t, sizeof t)) return;
        generation = v.generation;
        peaks_sent = 0;
        next_state = 0;
    }
    const int64_t now = tmu_us64();
    if (now >= next_state) {
        next_state = now + 33333;
        sl_state s{};
        s.pos_ms = v.pos_ms;
        s.cue_ms = v.cue_ms;
        s.loop_in_ms = v.loop_in_ms;
        s.loop_out_ms = v.loop_out_ms;
        s.speed_ppm = static_cast<int32_t>(v.speed * 1e6f);
        s.tempo_c = static_cast<int16_t>((v.tempo - 1.0f) * 10000.0f + (v.tempo >= 1.0f ? 0.5f : -0.5f));
        s.bpm_c = static_cast<uint16_t>(v.bpm * 100.0f + 0.5f);
        s.flags = static_cast<uint16_t>(SL_LOADED | (v.playing ? SL_PLAYING : 0) | (v.keylock ? SL_KEYLOCK : 0) |
                                        (v.looping ? SL_LOOPING : 0) | (v.reverse ? SL_REVERSE : 0) |
                                        (v.tempo_reset ? SL_RESET : 0));
        s.beat_in_bar = v.beat_in_bar;
        s.range = v.range_pct;
        for (int k = 0; k < 3; k++) s.hot_ms[k] = v.hot_ms[k];
        send(SL_STATE, &s, sizeof s);
    }
    // The waveform's peaks as the stream decodes them, a few packets a pass
    // (the link carries ~700 packets a second; the display drains 16 at a time).
    uint32_t ready = engine::peaks_decoded();
    if (ready > v.peaks) ready = v.peaks;
    for (int k = 0; k < 3 && peaks_sent < ready; k++) {
        static sl_peaks p;
        p.first = peaks_sent;
        p.count = static_cast<uint16_t>(ready - peaks_sent > SL_PEAKS_MAX ? SL_PEAKS_MAX : ready - peaks_sent);
        for (uint32_t i = 0; i < p.count; i++) p.v[i] = engine::peak(peaks_sent + i);
        if (!send(SL_PEAKS, &p, static_cast<uint16_t>(8 + p.count))) break;
        peaks_sent += p.count;
    }
}
} // namespace screen
