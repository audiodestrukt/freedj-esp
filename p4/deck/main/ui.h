#pragma once
// Deck screen: track list, loaded track, time, tempo, and touch buttons.
// The audio task publishes a State snapshot; the LVGL task redraws from it.
// Touches come back as deck::Events (same path as the DJ2Go) or a list pick.
#include <string>
#include <vector>
#include "deck_event.h"

class TrackStream;
namespace rekordbox { struct Grid; }

namespace ui {

struct State {
    int   selected   = 0;      // track list cursor
    bool  loaded     = false;
    bool  loading    = false;
    bool  playing    = false;
    bool  previewing = false;  // CUE held from pause
    bool  keylock    = false;
    bool  sync       = false;  // following the Link tempo master
    bool  hasGrid    = false;  // track has a rekordbox beat grid
    float bpm        = 0;      // our effective tempo (grid BPM x speed), 0 = unknown
    uint8_t masterPlayer = 0;  // Link tempo master, 0 = none
    float masterBpm  = 0;
    // Phase meter. beatPhase/masterPhase are 0..1 through the current beat,
    // -1 when unknown; beatInBar/masterInBar are 1..4 for that beat.
    float beatPhase  = -1;
    uint8_t beatInBar = 0;
    uint8_t barInPhrase = 0;   // 1..4: bar within the 4-bar phrase
    float masterPhase = -1;
    uint8_t masterInBar = 0;
    float pos        = 0;      // seconds
    float len        = 0;      // seconds
    float cue        = 0;      // seconds
    float tempoPct   = 0;      // pitch fader, percent
    size_t frame     = 0;      // playhead, in track frames (for the waveform)
    size_t cueFrame  = 0;
    char  name[64]   = "";
};

using EventFn  = void (*)(const deck::Event&);
using SelectFn = void (*)(int index);
using SeekFn   = void (*)(float fraction);   // needle search: 0..1 through the track

// Starts the screen with library::tracks() as the list. `stream` must
// outlive the UI; the waveform reads its peaks. Returns false if there's
// no screen.
bool begin(const TrackStream& stream, EventFn onEvent, SelectFn onSelect, SeekFn onSeek);

// Cheap and safe from any task.
void publish(const State& s);

// Test hook: 0 normal, 1 draw the waveform but don't send it to the
// screen, 2 don't draw it at all. Returns the new mode.
int cycleWaveTestMode();

// The loaded track's beat grid (or null), drawn on the waveform. The grid
// must stay unchanged while it is set; main.cpp double-buffers it.
void setGrid(const rekordbox::Grid* grid);

// Switch the phase meter between the beat display and the alignment view
// (same as tapping it). Safe from any task.
void togglePhaseView();

// Short text for the status log, e.g. "waveform 24.1 fps" or "list".
const char* waveStats();

} // namespace ui
