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
    uint32_t libGen  = 0;      // library generation `selected` refers to
    bool  loaded     = false;
    bool  loading    = false;
    bool  playing    = false;
    bool  previewing = false;  // CUE held from pause
    bool  keylock    = false;
    bool  emergency  = false;  // media pulled mid-play: looping from memory
    bool  quantize   = true;
    int   rangePct   = 10;     // pitch range: 6, 10, 16, 100 (WIDE)
    bool  tempoReset = false;
    bool  slip       = false;
    bool  reverse    = false;
    size_t slipFrame = 0;      // slip's shadow playhead
    bool  looping    = false;
    size_t loopStart = 0, loopEnd = 0;     // frames (active loop)
    uint8_t hotSet = 0, hotLoop = 0;       // bit i = hot cue A+i is set / is a loop
    size_t hotFrame[8] = {};
    static constexpr int kMaxMem = 16;
    uint8_t nMem = 0;
    size_t memFrame[kMaxMem] = {};
    // XDJ "Bars" readouts: bars.beats to the next memory cue (orange) and
    // the next hot cue (blue); -1 = none ahead.
    int   barsToMem  = -1, barsToHot = -1;     // in beats
    bool  sync       = false;  // following the Link tempo master
    bool  hasGrid    = false;  // track has a rekordbox beat grid
    float bpm        = 0;      // our effective tempo (grid BPM x speed), 0 = unknown
    uint8_t masterPlayer = 0;  // Link tempo master, 0 = none
    bool  weMaster   = false;  // we are the tempo master
    bool  onAir      = false;  // the mixer has our channel on air
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
// A list pick: the row index in list generation `gen` (the list can change
// under a touch when a stick comes or goes).
using SelectFn = void (*)(int index, uint32_t gen);
using SeekFn   = void (*)(float fraction);   // needle search: 0..1 through the track
using Fn       = void (*)();

// Starts the screen. The list follows library::get() as it changes. `stream` must
// outlive the UI; the waveform reads its peaks. Returns false if there's
// no screen.
// onMaster: long press on the SYNC chip (the XDJ's MASTER button).
bool begin(const TrackStream& stream, EventFn onEvent, SelectFn onSelect, SeekFn onSeek, Fn onMaster);

// Boot splash: brings the screen up (display::begin) and shows the deck's
// name and a status line, as early as possible. splashStatus() updates the
// line; begin() replaces the splash with the deck screen.
bool splash();
void splashStatus(const char* text);

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
