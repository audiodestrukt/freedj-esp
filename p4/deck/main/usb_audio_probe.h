#pragma once
// USB audio probe (UAC1, e.g. Behringer UCA202): when an audio-class device
// enumerates, log its audio interfaces, formats and endpoints, then play a
// 2 s 440 Hz tone through its first 16-bit stereo output that takes 48 kHz.
// Answers "do isochronous transfers work on this host, through the hub?"
// before an audio_out backend gets built on them. Nothing else uses it.

namespace usb_audio_probe {
// Call after usb_midi::begin() (which installs the USB host) and before
// usb_midi::powerOn().
void begin();
} // namespace usb_audio_probe
