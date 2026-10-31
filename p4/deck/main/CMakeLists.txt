# Deck event types and the DJ2Go decoder are shared with the A1S firmware (../../../src).
idf_component_register(
    SRCS "main.cpp" "usb_midi.cpp" "audio_out.cpp" "sd.cpp" "library.cpp" "stream.cpp" "display.cpp" "ui.cpp" "mp3_impl.c"
         "lvgl_mem.c" "net.cpp" "prolink.cpp" "rekordbox.cpp" "usb_stick.cpp"
         "../../../src/wsola.cpp"
         "../../../src/dj2go.cpp" "../../../src/deck_event.cpp"
    INCLUDE_DIRS "." "../../../src" "third_party/minimp3"
    REQUIRES usb esp_timer esp_driver_i2s esp_driver_i2c esp_driver_gpio esp_driver_sdmmc esp_driver_spi esp_driver_uart esp_lcd fatfs sdmmc
             esp_eth esp_netif esp_event lwip
    # lvgl_mem.c only defines symbols LVGL calls; keep it in the link.
    WHOLE_ARCHIVE)
# wsola.cpp builds without Arduino/audio-tools in this mode.
target_compile_definitions(${COMPONENT_LIB} PRIVATE WSOLA_NATIVE)
# minimp3: MP3 only, no stdio/mmap file helpers (we stream via callbacks).
target_compile_definitions(${COMPONENT_LIB} PRIVATE MINIMP3_ONLY_MP3 MINIMP3_NO_STDIO)
