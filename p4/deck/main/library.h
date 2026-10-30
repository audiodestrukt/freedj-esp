#pragma once
// Track list: the playable files on the SD card, in scan order.
#include <string>
#include <vector>

namespace library {
void scan(const char* root, int maxDepth = 4);
const std::vector<std::string>& tracks();
} // namespace library
