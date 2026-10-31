#pragma once
// Deck screen: track list, loaded track, time, tempo, and touch buttons.
// The audio task publishes a State snapshot; the LVGL task redraws from it.
// Touches come back as deck::Events (same path as the DJ2Go) or a list pick.
#include <string>
#include <vector>
#include "deck_event.h"

class TrackStream;

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

// Starts the screen with library::tracks() as the list. `stream` must
// outlive the UI; the waveform reads its peaks. Returns false if there's
// no screen.
bool begin(const TrackStream& stream, EventFn onEvent, SelectFn onSelect);

// Cheap and safe from any task.
void publish(const State& s);

// Test hook: 0 normal, 1 draw the waveform but don't send it to the
// screen, 2 don't draw it at all. Returns the new mode.
int cycleWaveTestMode();

// Short text for the status log, e.g. "waveform 24.1 fps" or "list".
const char* waveStats();

} // namespace ui
