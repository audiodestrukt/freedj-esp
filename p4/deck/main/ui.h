#pragma once
// Deck screen: track list, loaded track, time, tempo, and touch buttons.
// The audio task publishes a State snapshot; the LVGL task redraws from it.
// Touches come back as deck::Events (same path as the DJ2Go) or a list pick.
#include <string>
#include <vector>
#include "deck_event.h"

namespace ui {

struct State {
    int   selected   = 0;      // track list cursor
    bool  loaded     = false;
    bool  loading    = false;
    bool  playing    = false;
    bool  previewing = false;  // CUE held from pause
    bool  keylock    = false;
    float pos        = 0;      // seconds
    float len        = 0;      // seconds
    float cue        = 0;      // seconds
    float tempoPct   = 0;      // pitch fader, percent
    char  name[64]   = "";
};

using EventFn  = void (*)(const deck::Event&);
using SelectFn = void (*)(int index);

// Starts the screen. `tracks` must outlive the UI; `prefix` is stripped
// from each path for display. Returns false if there's no screen.
bool begin(const std::vector<std::string>& tracks, const char* prefix, EventFn onEvent, SelectFn onSelect);

// Cheap and safe from any task.
void publish(const State& s);

} // namespace ui
