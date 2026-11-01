// SPDX-FileCopyrightText: 2026 Dan Newcome
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// USB audio output (UAC1, e.g. Behringer UCA202). When an audio-class device
// with a 16-bit stereo isochronous output at the deck's rate enumerates, the
// deck's audio streams to it. audio_out::write() feeds it and mutes the
// speaker while it's active. Unplugging it brings the speaker back.
//
// Clocking: the I2S DMA still paces the audio loop. USB sends one packet per
// 1 ms frame (44 or 45 frames at 44.1 kHz) from a ring buffer; if the two
// clocks drift, the ring drops or repeats a single frame to stay centred.
#include <stddef.h>
#include <stdint.h>

namespace usb_audio {
// Call after usb_midi::begin() (which installs the USB host) and before
// usb_midi::powerOn().
void begin(uint32_t sampleRate);
// True while a device is streaming.
bool active();
// Queue interleaved stereo frames (audio task). Never blocks: if the ring is
// full the overflow is dropped and counted.
void write(const int16_t* lr, size_t frames);
// Packets sent with the ring empty (silence), and drift corrections.
uint32_t underruns();
uint32_t corrections();
} // namespace usb_audio
