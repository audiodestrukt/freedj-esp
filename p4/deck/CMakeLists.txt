# SPDX-FileCopyrightText: 2026 Dan Newcome
# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# The ESP32-A1S build (sdkconfig.defaults.a1s) resolves its own components;
# keep its lock apart so it never rewrites the P4's dependencies.lock.
if(IDF_TARGET STREQUAL "esp32")
    idf_build_set_property(DEPENDENCIES_LOCK dependencies.lock.esp32)
endif()
project(deck)
