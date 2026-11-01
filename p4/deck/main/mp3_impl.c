// SPDX-FileCopyrightText: 2026 Dan Newcome
// SPDX-License-Identifier: GPL-3.0-or-later
// minimp3 implementation unit. MINIMP3_ONLY_MP3 / MINIMP3_NO_STDIO are set
// for the whole component in CMakeLists.txt (no mmap/stdio on ESP-IDF; we
// stream through callbacks).
#define MINIMP3_IMPLEMENTATION
#include "minimp3_ex.h"
