# Deck event types and the DJ2Go decoder are shared with the A1S firmware (../../../src).
idf_component_register(
    SRCS "main.cpp" "usb_midi.cpp" "audio_out.cpp" "sd.cpp" "library.cpp" "stream.cpp"
         "../../../src/wsola.cpp"
         "../../../src/dj2go.cpp" "../../../src/deck_event.cpp"
    INCLUDE_DIRS "." "../../../src"
    REQUIRES usb esp_timer esp_driver_i2s esp_driver_i2c esp_driver_gpio esp_driver_sdmmc fatfs sdmmc)
# wsola.cpp builds without Arduino/audio-tools in this mode.
target_compile_definitions(${COMPONENT_LIB} PRIVATE WSOLA_NATIVE)
