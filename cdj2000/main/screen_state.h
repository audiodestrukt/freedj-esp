// The deck as the screen sees it (common/screen_link.h's structs), from the
// engine's view: shared by the Blackfin link (screen.cpp) and the XDJ's own
// LCDC drawing (screen_lcdc.cpp).
#pragma once
#include "screen_link.h"
#include "engine.h"
#include <string.h>

namespace screen {
inline sl_track track_of(const engine::View &v)
{
    sl_track t{};
    t.length_ms = v.length_ms;
    t.first_beat_ms = v.first_beat_ms;
    t.beat_us = v.beat_us;
    t.peak_frames = v.peak_frames;
    t.rate = v.rate;
    t.peaks = v.peaks;
    strncpy(t.title, v.title ? v.title : "", sizeof t.title - 1);
    return t;
}

inline sl_state state_of(const engine::View &v)
{
    sl_state s{};
    s.pos_ms = v.pos_ms;
    s.cue_ms = v.cue_ms;
    s.loop_in_ms = v.loop_in_ms;
    s.loop_out_ms = v.loop_out_ms;
    s.speed_ppm = static_cast<int32_t>(v.speed * 1e6f);
    s.tempo_c = static_cast<int16_t>((v.tempo - 1.0f) * 10000.0f + (v.tempo >= 1.0f ? 0.5f : -0.5f));
    s.bpm_c = static_cast<uint16_t>(v.bpm * 100.0f + 0.5f);
    s.flags = static_cast<uint16_t>((v.loaded ? SL_LOADED : 0) | (v.playing ? SL_PLAYING : 0) | (v.keylock ? SL_KEYLOCK : 0) |
                                    (v.looping ? SL_LOOPING : 0) | (v.reverse ? SL_REVERSE : 0) |
                                    (v.tempo_reset ? SL_RESET : 0));
    s.beat_in_bar = v.beat_in_bar;
    s.range = v.range_pct;
    for (int k = 0; k < 3; k++) s.hot_ms[k] = v.hot_ms[k];
    return s;
}
} // namespace screen
