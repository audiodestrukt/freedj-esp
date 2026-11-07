# SPDX-FileCopyrightText: 2026 Dan Newcome
# SPDX-License-Identifier: GPL-3.0-or-later
# Deck event types and the DJ2Go decoder are shared with the A1S firmware (../../../src).
set(srcs "main.cpp" "audio_out.cpp" "sd.cpp" "library.cpp" "stream.cpp" "display.cpp" "ui.cpp" "mp3_impl.c"
         "lvgl_mem.c" "rekordbox.cpp" "vinyl.cpp" "as5600.cpp" "panel_gpio.cpp"
         "../../../src/wsola.cpp" "../../../src/timecode.cpp"
         "../../../src/dj2go.cpp" "../../../src/xdj_midi.cpp" "../../../src/deck_event.cpp")
set(requires esp_timer esp_adc esp_driver_i2s esp_driver_i2c esp_driver_gpio esp_driver_sdmmc esp_driver_spi
             esp_driver_uart esp_lcd fatfs sdmmc esp_event esp_app_format)
if(IDF_TARGET STREQUAL "esp32")
    # ESP32-A1S Audio Kit (sdkconfig.defaults.a1s): no USB host, no Ethernet.
    list(APPEND srcs "a1s_stubs.cpp")
else()
    list(APPEND srcs "usb_midi.cpp" "net.cpp" "prolink.cpp" "usb_stick.cpp" "usb_audio.cpp")
    list(APPEND requires usb esp_eth esp_netif lwip)
endif()
idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "." "../../../src" "third_party/minimp3"
    REQUIRES ${requires}
    # lvgl_mem.c only defines symbols LVGL calls; keep it in the link.
    WHOLE_ARCHIVE)
# wsola.cpp builds without Arduino/audio-tools in this mode.
target_compile_definitions(${COMPONENT_LIB} PRIVATE WSOLA_NATIVE)
# minimp3: MP3 only, no stdio/mmap file helpers (we stream via callbacks).
target_compile_definitions(${COMPONENT_LIB} PRIVATE MINIMP3_ONLY_MP3 MINIMP3_NO_STDIO)
