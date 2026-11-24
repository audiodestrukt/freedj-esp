// The deck's screen, from the main board's side: what the display processor
// is sent over the link (common/screen_link.h, docs/screen.md).
#pragma once

namespace screen {
void service();         // call every pass: a state ~30 times a second, a track's title and peaks as they come
} // namespace screen
