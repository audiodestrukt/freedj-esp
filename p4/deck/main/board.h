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
} // namespace board
