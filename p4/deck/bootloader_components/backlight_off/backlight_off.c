// SPDX-FileCopyrightText: 2026 Dan Newcome
// SPDX-License-Identifier: GPL-3.0-or-later
// The MSP4021's backlight is on whenever its LED pin isn't driven low, and
// until the app initialises the panel its memory is blank, so it glows white
// for the first seconds after power-on. Drive the pin low as soon as the
// bootloader runs (~50 ms in); the app turns the light on with its splash.
// GPIO 3 = board::LCD_BL (main/board.h).
#include "esp_rom_gpio.h"
#include "hal/gpio_ll.h"

#define LCD_BL_GPIO 3

void bootloader_hooks_include(void) {}

void bootloader_before_init(void)
{
    esp_rom_gpio_pad_select_gpio(LCD_BL_GPIO);
    gpio_ll_output_enable(&GPIO, LCD_BL_GPIO);
    gpio_ll_set_level(&GPIO, LCD_BL_GPIO, 0);
}
