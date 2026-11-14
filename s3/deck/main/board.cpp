// SPDX-FileCopyrightText: 2026 Dan Newcome
// SPDX-License-Identifier: GPL-3.0-or-later
#include "board.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_spd2010.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char* TAG = "board";

static i2c_master_bus_handle_t s_i2c;
static i2c_master_dev_handle_t s_exp;
static uint8_t s_expOut = 0xFF;
static SemaphoreHandle_t s_lcdDone;

// TCA9554: reg 1 output, reg 3 config (1 = input)
static void exp_write(uint8_t reg, uint8_t v)
{
    const uint8_t b[2] = { reg, v };
    ESP_ERROR_CHECK(i2c_master_transmit(s_exp, b, 2, 100));
}

static void exp_pin(int bit, bool on)
{
    s_expOut = on ? (s_expOut | (1u << bit)) : (s_expOut & ~(1u << bit));
    exp_write(1, s_expOut);
}

static bool IRAM_ATTR lcd_done(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void*)
{
    BaseType_t woke = pdFALSE;
    xSemaphoreGiveFromISR(s_lcdDone, &woke);
    return woke == pdTRUE;
}

void board_backlight(int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, (uint32_t)((1 << 13) - 1) * percent / 100);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

static void lcd_init(Board& b)
{
    // reset through the expander
    exp_pin(EXP_LCD_RST, false);
    vTaskDelay(pdMS_TO_TICKS(100));
    exp_pin(EXP_LCD_RST, true);
    vTaskDelay(pdMS_TO_TICKS(100));

    spi_bus_config_t bus = {};
    bus.sclk_io_num = LCD_SCK;
    bus.data0_io_num = LCD_D0;
    bus.data1_io_num = LCD_D1;
    bus.data2_io_num = LCD_D2;
    bus.data3_io_num = LCD_D3;
    bus.data4_io_num = bus.data5_io_num = bus.data6_io_num = bus.data7_io_num = -1;
    bus.max_transfer_sz = LCD_W * 2 * 32;          // the largest band board_lcd_rows() sends at once
    bus.flags = SPICOMMON_BUSFLAG_MASTER;
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));

    // mode 0 at 80 MHz as Waveshare's demo runs it on this board (the driver's macro says 3 / 20 MHz)
    esp_lcd_panel_io_spi_config_t io = {};
    io.cs_gpio_num = LCD_CS;
    io.dc_gpio_num = -1;
    io.spi_mode = 0;
    io.pclk_hz = LCD_SPI_HZ;
    io.trans_queue_depth = 4;
    io.on_color_trans_done = lcd_done;
    io.lcd_cmd_bits = 32;
    io.lcd_param_bits = 8;
    io.flags.quad_mode = 1;
    esp_lcd_panel_io_handle_t ioh;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io, &ioh));

    spd2010_vendor_config_t vendor = {};
    vendor.flags.use_qspi_interface = 1;
    esp_lcd_panel_dev_config_t dev = {};
    dev.reset_gpio_num = -1;                        // reset is on the expander, done above
    dev.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    dev.bits_per_pixel = 16;
    dev.vendor_config = &vendor;
    ESP_ERROR_CHECK(esp_lcd_new_panel_spd2010(ioh, &dev, &b.lcd));
    ESP_ERROR_CHECK(esp_lcd_panel_init(b.lcd));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(b.lcd, true));

    // backlight PWM, off until there's a picture
    ledc_timer_config_t t = {};
    t.speed_mode = LEDC_LOW_SPEED_MODE;
    t.duty_resolution = LEDC_TIMER_13_BIT;
    t.timer_num = LEDC_TIMER_0;
    t.freq_hz = 5000;
    t.clk_cfg = LEDC_AUTO_CLK;
    ESP_ERROR_CHECK(ledc_timer_config(&t));
    ledc_channel_config_t c = {};
    c.gpio_num = LCD_BL;
    c.speed_mode = LEDC_LOW_SPEED_MODE;
    c.channel = LEDC_CHANNEL_0;
    c.timer_sel = LEDC_TIMER_0;
    c.duty = 0;
    ESP_ERROR_CHECK(ledc_channel_config(&c));
}

void board_lcd_send(Board& b, int y0, int y1, const uint16_t* px)
{
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(b.lcd, 0, y0, LCD_W, y1, px));
}

void board_lcd_wait() { xSemaphoreTake(s_lcdDone, portMAX_DELAY); }

void board_lcd_rows(Board& b, int y0, int y1, const uint16_t* px)
{
    board_lcd_send(b, y0, y1, px);
    board_lcd_wait();
}

static void i2s_init(Board& b, uint32_t rate)
{
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    cc.dma_desc_num = 6;
    cc.dma_frame_num = 256;
    cc.auto_clear = true;                           // silence, not a repeated buffer, on an underrun
    ESP_ERROR_CHECK(i2s_new_channel(&cc, &b.i2s, nullptr));
    i2s_std_config_t sc = {};
    sc.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(rate);
    sc.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    sc.gpio_cfg.mclk = I2S_GPIO_UNUSED;             // the PCM5101 makes its own from BCK
    sc.gpio_cfg.bclk = I2S_BCK;
    sc.gpio_cfg.ws = I2S_WS;
    sc.gpio_cfg.dout = I2S_DOUT;
    sc.gpio_cfg.din = I2S_GPIO_UNUSED;
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(b.i2s, &sc));
    ESP_ERROR_CHECK(i2s_channel_enable(b.i2s));
}

void board_init(Board& b, uint32_t rate)
{
    // keep the board on when it runs from the battery
    gpio_set_direction(PWR_LATCH, GPIO_MODE_OUTPUT);
    gpio_set_level(PWR_LATCH, 1);
    gpio_set_direction(PWR_KEY, GPIO_MODE_INPUT);

    s_lcdDone = xSemaphoreCreateBinary();

    i2c_master_bus_config_t bc = {};
    bc.i2c_port = I2C_NUM_0;
    bc.sda_io_num = I2C_SDA;
    bc.scl_io_num = I2C_SCL;
    bc.clk_source = I2C_CLK_SRC_DEFAULT;
    bc.glitch_ignore_cnt = 7;
    bc.flags.enable_internal_pullup = true;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bc, &s_i2c));
    i2c_device_config_t dc = {};
    dc.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dc.device_address = EXP_ADDR;
    dc.scl_speed_hz = 400000;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(s_i2c, &dc, &s_exp));
    // EXIO1-3 outputs (touch reset, LCD reset, SD D3), all high; the rest inputs
    exp_write(1, s_expOut);
    exp_write(3, (uint8_t)~((1u << EXP_TOUCH_RST) | (1u << EXP_LCD_RST) | (1u << EXP_SD_D3)));
    static const uint8_t kProbe[] = { TOUCH_ADDR, 0x51, 0x6B };      // touch, RTC, IMU
    for (uint8_t a : kProbe)
        ESP_LOGI(TAG, "i2c 0x%02X %s", a, i2c_master_probe(s_i2c, a, 50) == ESP_OK ? "found" : "missing");

    lcd_init(b);
    i2s_init(b, rate);
    ESP_LOGI(TAG, "%s up", BOARD_NAME);
}
