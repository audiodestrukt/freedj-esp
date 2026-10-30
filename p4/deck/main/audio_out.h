#pragma once
// ES8311 → NS4150B speaker output. The ES8311 has one DAC, so the speaker is
// mono: callers write interleaved stereo and the codec plays the left slot.
#include <stddef.h>
#include <stdint.h>

namespace audio_out {
bool begin(uint32_t sampleRate, int codecVolume /* 0..100 */);
void write(const int16_t* interleavedLR, size_t frames);  // blocks on DMA
} // namespace audio_out
