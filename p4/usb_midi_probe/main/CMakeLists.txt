# The DJ2Go decoder is shared with the A1S firmware and host tests (../../../src).
idf_component_register(
    SRCS "main.cpp" "../../../src/dj2go.cpp" "../../../src/deck_event.cpp"
    INCLUDE_DIRS "." "../../../src"
    REQUIRES usb)
