# SPDX-FileCopyrightText: 2026 Dan Newcome
# SPDX-License-Identifier: GPL-3.0-or-later
# freedj-esp on the ESP32-S3: a sibling of p4/deck. One project, a board header per S3 board
# (board_ws146.h: Waveshare ESP32-S3-Touch-LCD-1.46, round 412x412).
cmake_minimum_required(VERSION 3.16)
# The deck engine is freedj-nano's portable core (the C6 deck's), from the sibling checkout:
# audiodestrukt/freedj-nano next to audiodestrukt/freedj-esp. Build with both mounted.
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../../freedj-nano/components/nano")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(deck_s3)
