// SPDX-FileCopyrightText: 2026 Dan Newcome
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// AI-Thinker ESP32-A1S Audio Kit (V2.2/V2.3, ES8388) pin map, for the deck
// firmware built for the ESP32 (sdkconfig.defaults.a1s). Included by board.h.
// Codec/SD/I2S pins are the board's own; the screen sits on the header.
#include "driver/gpio.h"

namespace board {
// Codec I2C, shared with the screen's touch (0x38) and the AS5600 jog (0x36).
// ES8388 is 0x10.
constexpr gpio_num_t I2C_SDA = GPIO_NUM_33;
constexpr gpio_num_t I2C_SCL = GPIO_NUM_32;
// ES8388 I2S.
constexpr gpio_num_t I2S_MCLK = GPIO_NUM_0;
constexpr gpio_num_t I2S_BCLK = GPIO_NUM_27;
constexpr gpio_num_t I2S_WS   = GPIO_NUM_25;
constexpr gpio_num_t I2S_DOUT = GPIO_NUM_26;   // ESP32 -> codec DSDIN
constexpr gpio_num_t I2S_DIN  = GPIO_NUM_35;   // codec ASDOUT -> ESP32 (timecode, later)
// Speaker amps (NS4150 x2) enable, normally GPIO 21. Off for now: 21 is the screen's SCK
// (see below). The headphone jack plays without it.
constexpr gpio_num_t PA_EN    = GPIO_NUM_NC;
// microSD, 1-bit SDMMC (slot 1's IOMUX pins). D1-D3 left to the DIP switches/keys.
constexpr gpio_num_t SD_CLK = GPIO_NUM_14, SD_CMD = GPIO_NUM_15, SD_D0 = GPIO_NUM_2;

// 2.8" screen (MSP2834, ILI9341V + FT6336 touch) on the header. 23/5/19 are also
// KEY4/KEY6/KEY3: don't press those on-board keys with the screen wired.
// Screen LED and CTP_RST go to 3V3; CTP_INT and SDO are not used.
// Bring-up (2026-11-07): this wiring only draws cleanly up to ~2 MHz (display.cpp), with
// SCK on 18 or on 21 alike, so it's not one pin; 18 is free again. Not solved yet.
constexpr gpio_num_t LCD_SCK  = GPIO_NUM_21;
constexpr gpio_num_t LCD_MOSI = GPIO_NUM_23;
constexpr gpio_num_t LCD_CS   = GPIO_NUM_5;
constexpr gpio_num_t LCD_DC   = GPIO_NUM_19;
constexpr gpio_num_t LCD_RST  = GPIO_NUM_22;
constexpr gpio_num_t LCD_MISO = GPIO_NUM_NC;   // SDO: set a pin to read the panel back
constexpr gpio_num_t LCD_BL   = GPIO_NUM_NC;   // LED to 3V3: always on
constexpr gpio_num_t TP_RST   = GPIO_NUM_NC;   // CTP_RST to 3V3

// PLAY / CUE: the board's own KEY1 / KEY2 (to GND, pulled up on the board).
// KEY2 reaches GPIO 13 through the DIP switch; if CUE does nothing, check it.
// No pitch pot yet: its ADC1 pin isn't chosen (GPIO 34 may be free; unchecked).
constexpr gpio_num_t PANEL_PLAY  = GPIO_NUM_36;
constexpr gpio_num_t PANEL_CUE   = GPIO_NUM_13;
constexpr gpio_num_t PANEL_PITCH = GPIO_NUM_NC;

// AS5600 jog: I2C only (no analog fallback pin on this board).
constexpr gpio_num_t JOG_ADC  = GPIO_NUM_NC;
} // namespace board
