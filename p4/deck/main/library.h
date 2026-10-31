#pragma once
// Track list: rekordbox-analysed tracks on the USB stick first (they have
// beat grids), then the playable files on the SD card.
#include <string>
#include <vector>

namespace library {
// Rescans both. Call before the UI starts; the lists don't change after.
void scan(const char* sdRoot, const char* usbRoot);
const std::vector<std::string>& tracks();   // absolute paths
// The track's rekordbox ANLZ file, or "" if it has no analysis.
const std::string& anlz(int index);
float bpm(int index);                        // grid BPM, 0 = none
// What to show in the list: the file name for rekordbox tracks (their paths
// are deep), the path below the mount for SD files.
std::string displayName(int index);
} // namespace library
