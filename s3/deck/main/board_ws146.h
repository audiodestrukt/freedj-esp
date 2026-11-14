// SPDX-FileCopyrightText: 2026 Dan Newcome
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Waveshare ESP32-S3-Touch-LCD-1.46 (ESP32-S3R8, 16 MB flash). Pins from Waveshare's wiki and
// their ESP-IDF demo (github.com/waveshareteam/ESP32-S3-Touch-LCD-1.46).
#include "driver/gpio.h"

#define BOARD_NAME        "Waveshare ESP32-S3-Touch-LCD-1.46"

// SPD2010 round LCD, 412x412, QSPI. Touch is the same chip, on I2C.
#define LCD_W             412
#define LCD_H             412
#define LCD_SCK           GPIO_NUM_40
#define LCD_D0            GPIO_NUM_46
#define LCD_D1            GPIO_NUM_45
#define LCD_D2            GPIO_NUM_42
#define LCD_D3            GPIO_NUM_41
#define LCD_CS            GPIO_NUM_21
#define LCD_TE            GPIO_NUM_18
#define LCD_BL            GPIO_NUM_5         // backlight, PWM, active high
#define LCD_SPI_HZ        (80 * 1000 * 1000)
#define TOUCH_INT         GPIO_NUM_4
#define TOUCH_ADDR        0x53

// I2C: touch, TCA9554 expander (0x20), QMI8658 IMU (0x6B), PCF85063 RTC (0x51)
#define I2C_SDA           GPIO_NUM_11
#define I2C_SCL           GPIO_NUM_10
#define EXP_ADDR          0x20
// TCA9554 pins (bit = EXIOn - 1)
#define EXP_TOUCH_RST     0                  // EXIO1
#define EXP_LCD_RST       1                  // EXIO2
#define EXP_SD_D3         2                  // EXIO3: held high for SD mode

// PCM5101 DAC into the speaker amp (no line or headphone out on this board)
#define I2S_BCK           GPIO_NUM_48
#define I2S_WS            GPIO_NUM_38
#define I2S_DOUT          GPIO_NUM_47

// microSD, 1-bit SDMMC
#define SD_CLK            GPIO_NUM_14
#define SD_CMD            GPIO_NUM_17
#define SD_D0             GPIO_NUM_16

// power: PWR key in, and the battery power latch (high keeps the board on when on battery)
#define PWR_KEY           GPIO_NUM_6
#define PWR_LATCH         GPIO_NUM_7
#define BAT_ADC_GPIO      GPIO_NUM_8         // ADC1 channel 7, through a divider
