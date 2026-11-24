// The XDJ-1000MK2's screen: the SH7724's LCD controller (LCDC, 0xFE940000)
// scans an 800x480 RGB565 framebuffer out of DRAM onto the 7" panel, and the
// main board draws the deck into it itself (common/deck_draw.c, the same
// design the CDJ-2000's display processor draws).
//
// Two framebuffers: we draw the one not on screen, put its address in the
// second start-address register and flip bit 1 of LDRCNTR, which swaps the
// scan-out at the next frame, as cdj-nxs2-qemu's LCDC model describes the
// controller (LDSA1R 0x430 and its second at 0x2430, LDMLSR 0x438 the line
// stride, LDCNTR 0x474 bit 0 display on, LDRCNTR 0x478).
//
// ⚠ Only what the emulator reads is set.  A real panel also needs the
// LCDC's clock, its pins, the panel's timings and its data format (LDMT1R
// and the rest, from the SH7724 manual and the panel's sheet), set before
// display on: to do on a deck.  The framebuffers are written through the
// uncached P2 alias: slow on a deck, to change for cached writes and a
// cache flush before each flip.
#include "screen.h"
#include "screen_state.h"
#include "deck_draw.h"
#include "engine.h"
#include "tmu.h"
#include "con.h"

namespace screen {
namespace {
constexpr uint32_t kLcdc = 0xFE940000u;
inline volatile uint32_t &reg(uint32_t o) { return *reinterpret_cast<volatile uint32_t *>(kLcdc + o); }
constexpr uint32_t LDSA1R = 0x430, LDSA1R_2 = 0x2430, LDMLSR = 0x438, LDCNTR = 0x474, LDRCNTR = 0x478;
constexpr int W = 800, H = 480;
// Above the heap (board.h's BOARD_HEAP_TOP), below the stack's megabyte.
uint16_t *const fbs[2] = {reinterpret_cast<uint16_t *>(0xB7000000u), reinterpret_cast<uint16_t *>(0xB7100000u)};
inline uint32_t bus(const void *p) { return reinterpret_cast<uint32_t>(p) & 0x1FFFFFFFu; }

constexpr uint32_t kPeaksMax = 1u << 20;     // 50 min at 128 frames a peak
uint8_t peaks[kPeaksMax];
uint32_t peaks_have, generation;
sl_track tr;
bool started, have_track;
int shown;
int64_t next_frame;

void start()
{
    dd_frame f{fbs[0], &dd_xdj1000mk2, nullptr, nullptr, peaks, kPeaksMax, 0};
    static const sl_state idle{};
    f.st = &idle; f.tr = &tr;
    dd_draw(&f, 0);                          // "no track", until one loads
    reg(LDSA1R) = bus(fbs[0]);
    reg(LDSA1R_2) = bus(fbs[1]);
    reg(LDMLSR) = W * 2;
    reg(LDRCNTR) = 0;
    reg(LDCNTR) = 1;                         // display on
    shown = 0;
    started = true;
    con_printf("screen: LCDC on, %dx%d RGB565\n", W, H);
}
} // namespace

void service()
{
    if (!started) start();
    const engine::View v = engine::view();
    if (v.loaded && v.generation != generation) {   // a new track: its title, length and grid; its peaks from 0
        tr = track_of(v);
        generation = v.generation;
        peaks_have = 0;
        have_track = true;
    }
    if (have_track) {                               // the peaks as the stream decodes them
        uint32_t ready = engine::peaks_decoded();
        if (ready > v.peaks) ready = v.peaks;
        if (ready > kPeaksMax) ready = kPeaksMax;
        for (; peaks_have < ready; peaks_have++) peaks[peaks_have] = engine::peak(peaks_have);
    }
    const int64_t now = tmu_us64();
    if (now < next_frame) return;
    next_frame = now + 33333;                       // 30 frames a second
    const sl_state st = state_of(v);
    const int next = shown ^ 1;
    const dd_frame f{fbs[next], &dd_xdj1000mk2, &st, &tr, peaks, peaks_have, have_track};
    dd_draw(&f, v.pos_ms);
    reg(next ? LDSA1R_2 : LDSA1R) = bus(fbs[next]);
    reg(LDRCNTR) = next ? 2u : 0u;                  // scan out the one just drawn
    shown = next;
}
} // namespace screen
