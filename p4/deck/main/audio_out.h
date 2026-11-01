// SPDX-FileCopyrightText: 2026 Dan Newcome
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// ES8311 → NS4150B speaker output. The ES8311 has one DAC, so the speaker is
// mono: callers write interleaved stereo and the codec plays the left slot.
#include <stddef.h>
#include <stdint.h>
#include "driver/i2c_master.h"

namespace audio_out {
bool begin(uint32_t sampleRate, int codecVolume /* 0..100 */);
void write(const int16_t* interleavedLR, size_t frames);  // blocks on DMA
// The board's internal I2C bus (GPIO 7/8), shared with the screen's touch chip.
i2c_master_bus_handle_t i2cBus();
// Times the DAC ran dry and played silence (audio task was late).
uint32_t xruns();
} // namespace audio_out
