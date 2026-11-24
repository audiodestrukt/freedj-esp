// The CDJ-2000's front panel: the link to its microcontroller (docs/panel.md)
// and a decoder from its reports to freedj-esp's deck events.
#pragma once
#include <stdint.h>
#include "deck_event.h"
#include "board.h"

namespace cdj {

constexpr int kFrame = BOARD_PANEL_FRAME;   // bytes each way per exchange (24 on the CDJ-2000, 32 on the XDJ-1000MK2)
constexpr int kPayload = kFrame - 2;        // then a checksum and the sync byte
constexpr uint8_t kSync = 0x8F;

struct Frame { uint8_t b[kPayload] = {}; };

// The panel's SCIF set up as the stock firmware sets it.
void panel_init();
// One exchange: send `lamps`, take the panel's reply.  False (and `report`
// untouched) on a timeout, a bad checksum or a missing sync byte.
bool panel_exchange(const Frame &lamps, Frame &report);

// Lamp bits in the main board's frame.
struct Lamp { uint8_t off, mask; };
constexpr Lamp kLampPlay{0x01, 0x20}, kLampCue{0x01, 0x10}, kLampLoopIn{0x01, 0x02},
    kLampLoopOut{0x01, 0x04}, kLampReloop{0x01, 0x40}, kLampTempoReset{0x01, 0x80},
    kLampMasterTempo{0x02, 0x20}, kLampDisc{0x03, 0x02}, kLampSd{0x03, 0x08}, kLampLink{0x03, 0x80};
inline void set_lamp(Frame &f, Lamp l, bool on) { f.b[l.off] = on ? (f.b[l.off] | l.mask) : (f.b[l.off] & ~l.mask); }

// Turns successive reports into deck events: key edges, jog and select-knob
// motion, the tempo slider, the DIRECTION lever.  A key with no deck event
// yet comes back as `raw_off`/`raw_mask` with type None.
class PanelDecoder {
public:
    struct Out { deck::Event ev; uint8_t raw_off = 0, raw_mask = 0; };
    // Up to `max` events from `report`; returns how many.
    int feed(const Frame &report, Out *out, int max);
private:
    Frame last_{};
    bool have_last_ = false;
    float tempo_ = -1.0f;
};

} // namespace cdj
