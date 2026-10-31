# Bootloader hook: switch the screen's backlight off right after power-on.
idf_component_register(SRCS "backlight_off.c" REQUIRES hal esp_rom)
# Keep the hook in the link (nothing in the bootloader references it).
target_link_libraries(${COMPONENT_LIB} INTERFACE "-u bootloader_hooks_include")
