// The CDJ-2000's deck: freedj-esp's track stream (WAV/MP3), WSOLA for
// MASTER TEMPO and the rekordbox grid, under a CDJ transport, fed to the
// DSP; the deck's state for the lamps and for Pro DJ Link.  Follows the P4
// deck's behaviour (p4/deck/main/main.cpp), on this board's single loop.
#pragma once
#include <stdint.h>
#include "deck_event.h"

namespace engine {
void begin();
// The stick's analysed tracks (from rekordbox::scan); the first is selected.
void add_track(const char *audio, const char *anlz, float bpm);
int  tracks();
void apply(const deck::Event &e);
// Call every pass: decode ahead (within a time budget), then fill the DSP's
// ring, then tell Link.
void service();

struct Lamps { bool play, cue, loop_in, loop_out, reloop, master_tempo, tempo_reset; };
bool playing();
bool loaded();
uint32_t position_ms();
Lamps lamps();
// One line for the console: track, position, tempo, beat.
void summary(char *out, int n);
} // namespace engine
