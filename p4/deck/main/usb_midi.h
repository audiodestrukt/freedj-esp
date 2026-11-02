// SPDX-FileCopyrightText: 2026 Dan Newcome
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// USB MIDI host: claims the first MIDI streaming interface that enumerates
// and hands each 4-byte USB-MIDI event packet to `onPacket` (USB task context).
#include <stdint.h>

namespace usb_midi {
using PacketFn = void (*)(const uint8_t packet[4]);
void begin(PacketFn onPacket);
// USB vendor ID of the MIDI device in use (0 if none), so the caller can pick
// the right mapping: 0x2B73 is Pioneer DJ.
uint16_t vendor();
// Power the root port once every USB class driver has registered.
void powerOn();
} // namespace usb_midi
