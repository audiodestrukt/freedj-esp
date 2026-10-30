# Deck event types and the DJ2Go decoder are shared with the A1S firmware (../../../src).
idf_component_register(
    SRCS "main.cpp" "usb_midi.cpp" "audio_out.cpp" "sd.cpp" "library.cpp" "stream.cpp" "mp3_impl.c"
         "../../../src/wsola.cpp"
         "../../../src/dj2go.cpp" "../../../src/deck_event.cpp"
    INCLUDE_DIRS "." "../../../src" "third_party/minimp3"
    REQUIRES usb esp_timer esp_driver_i2s esp_driver_i2c esp_driver_gpio esp_driver_sdmmc fatfs sdmmc)
# wsola.cpp builds without Arduino/audio-tools in this mode.
target_compile_definitions(${COMPONENT_LIB} PRIVATE WSOLA_NATIVE)
# minimp3: MP3 only, no stdio/mmap file helpers (we stream via callbacks).
target_compile_definitions(${COMPONENT_LIB} PRIVATE MINIMP3_ONLY_MP3 MINIMP3_NO_STDIO)
