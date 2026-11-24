// A stand-in player until freedj-esp's engine is brought over: a 16-bit
// 44.1 kHz stereo WAV from the stick, PLAY/PAUSE and CUE (back to the
// start), into the DSP.
#pragma once
#include <stdint.h>

namespace player {
bool load(const char *path);    // a WAV on /usb; false if it can't be played
void play(bool on);
void cue();                     // stop, back to the start
bool playing();
bool loaded();
void pump();                    // keep the DSP's ring full; call often
uint32_t position_ms();
} // namespace player
