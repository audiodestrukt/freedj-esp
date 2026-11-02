// SPDX-FileCopyrightText: 2026 Dan Newcome
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// AS5600 jog encoder. Over I2C (0x36 on the codec/touch bus, header pins 3/5)
// when it answers at boot, else through its analog OUT pin on header pin 7
// (GPIO 23). A task on core 0 samples it at 500 Hz and turns rotation into
// deck::Type::Jog events, the same ones the DJ2Go's jog sends.
#include "deck_event.h"
#include "driver/i2c_master.h"

namespace as5600 {
using EventFn = void (*)(const deck::Event&);
void begin(EventFn onEvent, i2c_master_bus_handle_t bus);

struct Stats {
    int raw;          // last reading (I2C angle or averaged ADC), 0..4095
    int lo, hi;       // range seen so far (auto-calibrates the 0..1 turn mapping)
    int ticks;        // net ticks sent since boot
    int readUs;       // time for one reading
    int lost;         // samples skipped: magnet not seen, or I2C error
    bool active;      // false if neither I2C nor the ADC started
    bool i2c;
};
Stats stats();

// Records the next n raw samples (500 Hz; -1 = lost) and returns how many, with
// *out pointing at them. Blocks until done. For checking the sensor's signal.
constexpr int kCaptureMax = 5000;
int capture(int n, const int16_t** out);

// One-off diagnostic over I2C (address 0x36) on the given bus: logs the magnet
// status (detected / too weak / too strong), AGC, magnitude, raw angle and the
// output config. Needs the board's SDA/SCL on header pins 3/5. Not called from
// the audio task.
void probe(i2c_master_bus_handle_t bus);
} // namespace as5600
