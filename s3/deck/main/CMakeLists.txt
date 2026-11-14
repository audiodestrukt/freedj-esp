# SPDX-FileCopyrightText: 2026 Dan Newcome
# SPDX-License-Identifier: GPL-3.0-or-later
# ui/ is plain C++ (no ESP-IDF): host/ builds the same files into PNG previews.
# fake_track.bin: a made-up track's waveform for the demo (make -C host fake_track.bin).
idf_component_register(
    SRCS "main.cpp" "board.cpp" "ui/rgfx.cpp" "ui/round_ui.cpp" "ui/fonts.cpp"
    INCLUDE_DIRS "." "ui"
    EMBED_FILES "../host/fake_track.bin"
    REQUIRES esp_timer esp_driver_i2s esp_driver_i2c esp_driver_gpio esp_driver_spi esp_driver_ledc esp_lcd)
# The UI's drawing is float-heavy and the S3 has no hardware divide or sqrt: let the compiler
# use reciprocals and the FPU's approximations there (only these files; never the audio path).
set_source_files_properties("ui/rgfx.cpp" "ui/round_ui.cpp" PROPERTIES COMPILE_OPTIONS "-ffast-math")
