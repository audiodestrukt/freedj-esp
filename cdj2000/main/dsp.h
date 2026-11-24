// The CDJ-2000's DSP (a TI C6727) from the main board: boot our program
// (dsp/audio.s) through the host port, and feed it PCM.  See docs/audio.md.
#pragma once
#include <stdint.h>

namespace dsp {
bool boot();                                    // true once the program says it runs
int  room();                                    // stereo frames the ring can take now
void push(const int16_t *lr, int frames);       // interleaved 16-bit stereo
void flush();                                   // drop what is queued (a cue)
uint32_t halves();                              // 16-frame halves the DAC has been sent
} // namespace dsp
