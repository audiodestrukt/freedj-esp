#pragma once
// A USB stick (mass storage) in one of the USB-A ports, mounted as FAT at
// usb_stick::kMount. Shares the USB host with the DJ2Go (usb_midi), so call
// begin() after usb_midi::begin().
namespace usb_stick {
constexpr const char* kMount = "/usb";
void begin();
// Wait up to `ms` for a stick to mount. Returns true once one is mounted.
bool waitMounted(int ms);
bool mounted();
} // namespace usb_stick
