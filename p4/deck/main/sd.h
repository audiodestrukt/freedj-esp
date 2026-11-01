// SPDX-FileCopyrightText: 2026 Dan Newcome
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// microSD on the board's 4-bit SDMMC slot, mounted as FAT at sd::kMount.
namespace sd {
constexpr const char* kMount = "/sdcard";
bool begin();
void listAudio(int maxDepth = 3);  // log audio files under kMount
} // namespace sd
