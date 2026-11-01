// SPDX-FileCopyrightText: 2026 Dan Newcome
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// A USB stick (mass storage) in one of the USB-A ports, mounted as FAT at
// usb_stick::kMount. Shares the USB host with the DJ2Go (usb_midi), so call
// begin() after usb_midi::begin() and before usb_midi::powerOn().
// Sticks can come and go while the deck runs.
namespace usb_stick {
constexpr const char* kMount = "/usb";
// onMounted: a stick is mounted at kMount.
// onRemoved: the stick has been pulled. Must not return until nothing has a
// file open under kMount; the stick is unmounted after it returns.
using Fn = void (*)();
void begin(Fn onMounted, Fn onRemoved);
bool mounted();
} // namespace usb_stick
