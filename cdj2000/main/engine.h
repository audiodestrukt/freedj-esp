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
// `id`: its rekordbox ID from the stick's export.pdb (main/pdb.h), 0 = none;
// our status names it, so a peer can find the track on our stick over NFS.
void add_track(const char *audio, const char *anlz, float bpm, uint32_t id);
int  tracks();
void apply(const deck::Event &e);
// Call every pass: decode ahead (within a time budget), then fill the DSP's
// ring, then tell Link.
void service();

struct Lamps { bool play, cue, loop_in, loop_out, reloop, master_tempo, tempo_reset; };
bool playing();
bool loaded();
uint32_t position_ms();

// What the screen shows (main/screen.cpp sends it to the display processor).
struct View {
    bool loaded, playing, keylock, looping, reverse, tempo_reset;
    uint32_t pos_ms, cue_ms, loop_in_ms, loop_out_ms, length_ms, hot_ms[3];
    float speed, tempo, bpm;            // bpm with the tempo; 0 = no grid
    uint8_t beat_in_bar, range_pct;
    uint32_t first_beat_ms, beat_us;    // the grid, as a constant tempo from its first beat
    uint32_t rate, peak_frames, peaks;  // the waveform: a peak per peak_frames frames
    const char *title;
    uint32_t generation;                // goes up with every track loaded
};
View view();
uint8_t peak(uint32_t i);
uint32_t peaks_decoded();               // peaks below this have been decoded
Lamps lamps();
// One line for the console: track, position, tempo, beat.
void summary(char *out, int n);
} // namespace engine
