// SPDX-FileCopyrightText: 2026 Dan Newcome
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Waveshare ESP32-P4-Module-DEV-KIT pin map (from the espp BSP,
// components/esp32-p4-module-dev-kit/include/esp32-p4-module-dev-kit.hpp).
#include "driver/gpio.h"

namespace board {
// Internal I2C: ES8311 codec, GT911 touch, camera SCCB.
constexpr gpio_num_t I2C_SDA = GPIO_NUM_7;
constexpr gpio_num_t I2C_SCL = GPIO_NUM_8;
// ES8311 I2S.
constexpr gpio_num_t I2S_MCLK = GPIO_NUM_13;
constexpr gpio_num_t I2S_BCLK = GPIO_NUM_12;
constexpr gpio_num_t I2S_WS   = GPIO_NUM_10;
constexpr gpio_num_t I2S_DOUT = GPIO_NUM_9;   // P4 -> codec DSDIN
constexpr gpio_num_t I2S_DIN  = GPIO_NUM_11;  // codec ASDOUT -> P4 (mono mic)
// NS4150B speaker amp enable.
constexpr gpio_num_t PA_EN    = GPIO_NUM_53;
// microSD, 4-bit SDMMC (powered from the P4's internal LDO_VO4).
constexpr gpio_num_t SD_CLK = GPIO_NUM_43, SD_CMD = GPIO_NUM_44;
constexpr gpio_num_t SD_D0 = GPIO_NUM_39, SD_D1 = GPIO_NUM_40, SD_D2 = GPIO_NUM_41, SD_D3 = GPIO_NUM_42;

// Stand-in screen on the 40-pin header: LCDwiki MSP4021, 4" 480x320,
// ST7796S over SPI + FT6336U capacitive touch on the internal I2C bus.
// (Header pin numbers in the comments.)
constexpr gpio_num_t LCD_SCK  = GPIO_NUM_21;  // pin 11
constexpr gpio_num_t LCD_MOSI = GPIO_NUM_20;  // pin 13
constexpr gpio_num_t LCD_CS   = GPIO_NUM_6;   // pin 15
constexpr gpio_num_t LCD_DC   = GPIO_NUM_5;   // pin 16 (LCD_RS)
constexpr gpio_num_t LCD_RST  = GPIO_NUM_4;   // pin 18
constexpr gpio_num_t LCD_BL   = GPIO_NUM_3;   // pin 19 (LED)
constexpr gpio_num_t TP_RST   = GPIO_NUM_2;   // pin 21
constexpr gpio_num_t TP_INT   = GPIO_NUM_23;  // pin 7

// Ethernet: IP101GRI PHY over RMII. The data pins, MDC 31, MDIO 52 and the
// 50 MHz reference clock in on GPIO 50 are ESP-IDF's P4 defaults.
constexpr gpio_num_t ETH_PHY_RST = GPIO_NUM_51;
constexpr int        ETH_PHY_ADDR = 1;
} // namespace board
