// A board whose DSP program isn't ported yet (the XDJ-1000MK2's C6747):
// dsp:: as a sink that takes PCM at the DAC's rate, 44.1 kHz by the clock,
// through a ring as deep as the C6727 program's, so the deck engine runs
// (and Link reports its playhead) as it does with sound.  Silent.
#include "dsp.h"
#include "tmu.h"
#include "con.h"

namespace dsp {
namespace {
constexpr int kRing = 4096;                     // stereo frames, as dsp/audio.s's ring
constexpr int kRate = 44100;
int64_t t0;
uint64_t pushed, flushed;                       // frames ever pushed, frames dropped by flush()

uint64_t played()                               // frames the DAC would have taken by now
{
    const uint64_t by_clock = static_cast<uint64_t>((tmu_us64() - t0) * kRate / 1000000);
    const uint64_t have = pushed - flushed;
    return by_clock < have ? by_clock : have;
}
} // namespace

bool boot()
{
    t0 = tmu_us64();
    con_printf("dsp: none yet on this board: a silent sink at 44.1 kHz\n");
    return false;
}

int queued() { return static_cast<int>(pushed - flushed - played()); }
int room() { return kRing - queued(); }

void push(const int16_t *, int frames)
{
    if (queued() == 0) {                        // the ring ran dry (or is new): the clock restarts with this block
        t0 = tmu_us64();
        flushed = pushed;
    }
    pushed += static_cast<uint64_t>(frames);
}

void flush() { flushed = pushed; t0 = tmu_us64(); }

uint32_t halves() { return static_cast<uint32_t>((pushed - flushed) / 16); }
} // namespace dsp
