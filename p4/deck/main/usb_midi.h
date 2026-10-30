#pragma once
// USB MIDI host: claims the first MIDI streaming interface that enumerates
// and hands each 4-byte USB-MIDI event packet to `onPacket` (USB task context).
#include <stdint.h>

namespace usb_midi {
using PacketFn = void (*)(const uint8_t packet[4]);
void begin(PacketFn onPacket);
} // namespace usb_midi
