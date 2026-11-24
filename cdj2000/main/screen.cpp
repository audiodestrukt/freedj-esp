// The link to the display processor, the main board's end: its transmit block
// at 0xFF500000 (as the emulators model it, display_link.c: the buffer's
// address at +0x1008, its length at +0x1010 in 16-byte units, bit 0 of
// +0x1028 sends; a unit-done status at +0x1188, acknowledged a unit at a
// time).  One 512-byte packet at a time (screen_link.h).
#include "screen.h"
#include "screen_link.h"
#include "screen_state.h"
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
        const sl_track t = track_of(v);
        if (!send(SL_TRACK, &t, sizeof t)) return;
        generation = v.generation;
        peaks_sent = 0;
        next_state = 0;
    }
    const int64_t now = tmu_us64();
    if (now >= next_state) {
        next_state = now + 33333;
        const sl_state s = state_of(v);
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
