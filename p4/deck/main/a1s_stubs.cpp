// SPDX-FileCopyrightText: 2026 Dan Newcome
// SPDX-License-Identifier: GPL-3.0-or-later
// ESP32-A1S build: the classic ESP32 has no USB host and no Ethernet MAC wired
// on the Audio Kit, so the USB (MIDI, stick, audio), network and Pro DJ Link
// modules are replaced by these no-ops. main.cpp calls them as on
// the P4 and simply never hears from them. Only built for IDF_TARGET esp32.
#include <stdio.h>
#include "net.h"
#include "prolink.h"
#include "usb_audio.h"
#include "usb_midi.h"
#include "usb_stick.h"

namespace usb_midi {
void begin(PacketFn) {}
uint16_t vendor() { return 0; }
void powerOn() {}
} // namespace usb_midi

namespace usb_stick {
void begin(Fn, Fn) {}
bool mounted() { return false; }
} // namespace usb_stick

namespace usb_audio {
void begin(uint32_t) {}
bool active() { return false; }
void write(const int16_t*, size_t) {}
uint32_t underruns() { return 0; }
uint32_t corrections() { return 0; }
bool inputActive() { return false; }
uint32_t inputRate() { return 0; }
size_t read(int16_t*, size_t) { return 0; }
size_t inputFill() { return 0; }
InputStats inputStats() { return {}; }
} // namespace usb_audio

namespace net {
void begin() {}
uint32_t ip() { return 0; }
uint32_t netmask() { return 0; }
void mac(uint8_t out[6]) { for (int i = 0; i < 6; i++) out[i] = 0; }
bool linkUp() { return false; }
} // namespace net

namespace prolink {
void begin(uint8_t, const char*) {}
bool master(Master& out) { out = Master{}; return false; }
void publish(const Ours&) {}
void summary(char* out, int n) { snprintf(out, n, "none (A1S: no network)"); }
bool isMaster() { return false; }
void requestMaster() {}
int takeRemoteSync() { return 0; }
int takeFaderStart() { return 0; }
bool onAir() { return false; }
} // namespace prolink
