#pragma once
// Deck control events: the one vocabulary every input source speaks
// (DJ2Go MIDI, the A1S buttons/encoder, serial keys, a future I2C panel).
// Names follow freedj-3000's ControlEvent so a control panel can drive both.
// Plain C++ — no Arduino or ESP-IDF headers — so it builds on host and target.

#include <stdint.h>

namespace deck {

enum class Type : uint8_t {
    None,
    // Buttons — `pressed` is true on press, false on release.
    PlayPause,
    Cue,          // momentary: hold-to-preview semantics live in the player
    Sync,
    LoopIn,
    LoopOut,
    Load,         // deck A/B: load selection to that deck; Any: to the active deck
    Back,         // browser up a level
    Shift,
    Pfl,          // headphone cue select
    // Relative — `delta` is signed detents/ticks since the last event.
    Jog,
    Browse,
    // Absolute — `position` is 0.0..1.0.
    TempoFader,   // 0.5 = centre (0 %); which end is "faster" is the player's call
    Volume,
    Crossfader,   // 0 = full A, 1 = full B
    MasterVolume,
    PhonesVolume,
    // Buttons, added later (kept last so existing values do not move).
    KeyLock,
};

enum : uint8_t { DeckA = 0, DeckB = 1, DeckAny = 0xFF };

struct Event {
    Type    type     = Type::None;
    uint8_t deck     = DeckAny;
    bool    pressed  = false;
    int16_t delta    = 0;
    float   position = 0.0f;
};

const char* typeName(Type t);

} // namespace deck
