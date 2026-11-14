# SPDX-FileCopyrightText: 2026 Dan Newcome
# SPDX-License-Identifier: GPL-3.0-or-later
# freedj-esp on the ESP32-S3: a sibling of p4/deck. One project, a board header per S3 board
# (board_ws146.h: Waveshare ESP32-S3-Touch-LCD-1.46, round 412x412).
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(deck_s3)
