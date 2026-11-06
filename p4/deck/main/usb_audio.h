// SPDX-FileCopyrightText: 2026 Dan Newcome
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// USB audio output and input (UAC1, e.g. Behringer UCA202). When an audio-class device
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

// USB audio input (UAC1 capture, e.g. the UCA202's line in), for timecode
// vinyl. Runs alongside the output when the device has both. 16/24/32-bit
// samples arrive as 16-bit stereo (the first two channels).
bool inputActive();
uint32_t inputRate();      // Hz, 0 when no input
// Take up to maxFrames interleaved stereo frames (one reader). Never blocks.
size_t read(int16_t* lr, size_t maxFrames);
// Frames queued for read().
size_t inputFill();
struct InputStats { uint32_t packets, errors, overflows, restarts; };
InputStats inputStats();
} // namespace usb_audio
