// SPDX-FileCopyrightText: 2026 Dan Newcome
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// The round deck screen, for any round panel (the 412 px Waveshare 1.46, the puck's 240 px).
// Every size is a fraction of the diameter. From the outside in:
//
//   jog ring     the outer band: touch-and-turn jog on the 1.46, the bearing ring's
//                picture on the puck. Ticks turn with the virtual platter.
//   progress     a thin arc just inside it: how far through the track (red near the end)
//   top          track name, remaining time, then key / BPM / pitch
//   waveform     left to right across the middle, the widest part of the circle;
//                playhead in the centre
//   overview     the whole track, under the waveform
//   buttons      CUE | PLAY, the bottom of the circle split in two (big enough to hit)
//
// draw() renders one band (rgfx::Band), so a device can send the screen in strips.
#include <stddef.h>
#include <stdint.h>
#include "rgfx.h"

namespace round_ui {

struct View {
    const char* title = "";
    float pos = 0, len = 0, cue = 0;      // seconds
    bool  playing = false;
    bool  atCue = false;                   // paused on the cue point
    float bpm = 0, pitchPct = 0;           // effective BPM; pitch fader in percent
    const char* key = "";
    bool  keylock = false;
    // detail waveform: one peak (0..255) and one RGB565 colour per bin
    const uint8_t*  peak = nullptr;
    const uint16_t* color = nullptr;
    size_t bins = 0;
    float  binsPerSec = 0;
    // beat grid: the first beat, in seconds, and the track's BPM (0: no grid)
    float gridStart = 0, gridBpm = 0;
    // jog
    float platterDeg = 0;                  // where the virtual platter is
    bool  jogTouched = false;
};

struct Layout {
    float d, cx, cy;                       // diameter, centre
    float ringOut, ringIn;                 // the jog ring
    float progOut, progIn;                 // the progress arc
    float inner;                           // content stays inside this radius
    float titleY, timeY, infoY;            // baselines
    float waveY, waveH;                    // centre line, half height
    float ovTop, ovH;                      // overview strip
    float btnTop;                          // the buttons are the circle below this
    float secAcross;                       // seconds of track across the waveform
    const rgfx::Font *title, *time, *info, *small, *btn;
};

Layout layout(int diameter);

// Which part of the screen a touch at (x, y) is on.
enum class Zone { None, Jog, Wave, Overview, Cue, Play };
Zone hit(const Layout& l, float x, float y);
float jogAngleDeg(const Layout& l, float x, float y);    // 0 at 12 o'clock, clockwise

void draw(rgfx::Band& b, const Layout& l, const View& v);

// Profiling: if `now` is set (a microsecond clock), draw() adds each part's time to us[].
// Parts: clear, ring, progress, top, wave, overview, buttons.
struct Prof { int64_t (*now)() = nullptr; int64_t us[7] = {}; };
extern Prof prof;

} // namespace round_ui
