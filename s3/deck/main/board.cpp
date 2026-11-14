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
#include "freertos/portmacro.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char* TAG = "board";

static i2c_master_bus_handle_t s_i2c;
static i2c_master_dev_handle_t s_exp;
static uint8_t s_expOut = 0xFF;
static SemaphoreHandle_t s_lcdDone;
static esp_lcd_panel_io_handle_t s_lcdIo;
static SemaphoreHandle_t s_te;
static volatile uint32_t s_teCount;
static float s_teHz;

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
    s_lcdIo = ioh;

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

// ---- vsync (TE)

static void IRAM_ATTR te_isr(void*)
{
    s_teCount = s_teCount + 1;
    BaseType_t woke = pdFALSE;
    xSemaphoreGiveFromISR(s_te, &woke);
    if (woke) portYIELD_FROM_ISR();
}

static float te_measure()
{
    const uint32_t c0 = s_teCount;
    vTaskDelay(pdMS_TO_TICKS(250));
    return (s_teCount - c0) * 4.0f;
}

static void te_init()
{
    s_te = xSemaphoreCreateBinary();
    gpio_config_t g = {};
    g.pin_bit_mask = 1ULL << LCD_TE;
    g.mode = GPIO_MODE_INPUT;
    g.intr_type = GPIO_INTR_POSEDGE;
    ESP_ERROR_CHECK(gpio_config(&g));
    gpio_install_isr_service(0);
    ESP_ERROR_CHECK(gpio_isr_handler_add(LCD_TE, te_isr, nullptr));
    s_teHz = te_measure();
    if (s_teHz < 1) {
        // not on by default: TEON (0x35), V-blank only. Commands go as opcode 0x02, cmd << 8.
        const uint8_t mode = 0x00;
        esp_lcd_panel_io_tx_param(s_lcdIo, (0x02 << 24) | (0x35 << 8), &mode, 1);
        s_teHz = te_measure();
    }
    ESP_LOGI(TAG, "vsync (TE): %.0f Hz", s_teHz);
}

bool board_vsync(int ms)
{
    if (s_teHz < 1) return false;
    xSemaphoreTake(s_te, 0);                          // drop a pulse that came before the call
    return xSemaphoreTake(s_te, pdMS_TO_TICKS(ms)) == pdTRUE;
}

float board_vsync_hz() { return s_teHz; }

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
    // BCK = 64 fs: with no MCLK the PCM510x's PLL locks on that (freedj-nano found this on the
    // PCM5102A); 16-bit data in 32-bit slots, WS a full slot wide
    sc.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT;
    sc.slot_cfg.ws_width = 32;
    sc.gpio_cfg.mclk = I2S_GPIO_UNUSED;             // the PCM5101 makes its own from BCK
    sc.gpio_cfg.bclk = I2S_BCK;
    sc.gpio_cfg.ws = I2S_WS;
    sc.gpio_cfg.dout = I2S_DOUT;
    sc.gpio_cfg.din = I2S_GPIO_UNUSED;
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(b.i2s, &sc));
    ESP_ERROR_CHECK(i2s_channel_enable(b.i2s));
}

static uint32_t s_rate;

void board_audio_rate(Board& b, uint32_t rate)
{
    if (!rate || rate == s_rate) return;
    i2s_std_clk_config_t c = I2S_STD_CLK_DEFAULT_CONFIG(rate);
    i2s_channel_disable(b.i2s);
    i2s_channel_reconfig_std_clock(b.i2s, &c);
    i2s_channel_enable(b.i2s);
    s_rate = rate;
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
    te_init();
    i2s_init(b, rate);
    s_rate = rate;
    ESP_LOGI(TAG, "%s up", BOARD_NAME);
}

// ---- touch: the SPD2010's touch side, at 0x53 on the I2C bus. Its protocol (from Waveshare's
// demo): 16-bit register addresses; read the status (0x2000) for what's waiting and how much;
// if the controller is still in its BIOS or CPU start-up, send it the commands that start
// touch reporting; when a point is there, read the report (0x0003); then clear the interrupt.

static i2c_master_dev_handle_t s_tpDev;
static SemaphoreHandle_t s_tpIrq;
static portMUX_TYPE s_tpLock = portMUX_INITIALIZER_UNLOCKED;
static Touch s_touch;
static void (*s_touchFn)(const Touch&);

static esp_err_t tp_cmd(uint16_t reg, uint16_t val)
{
    const uint8_t b[4] = { (uint8_t)(reg >> 8), (uint8_t)reg, (uint8_t)val, (uint8_t)(val >> 8) };
    const esp_err_t e = i2c_master_transmit(s_tpDev, b, 4, 50);
    esp_rom_delay_us(200);
    return e;
}

static esp_err_t tp_read(uint16_t reg, uint8_t* out, size_t n)
{
    const uint8_t a[2] = { (uint8_t)(reg >> 8), (uint8_t)reg };
    const esp_err_t e = i2c_master_transmit_receive(s_tpDev, a, 2, out, n, 50);
    esp_rom_delay_us(200);
    return e;
}

// One poll. Returns true with *down / *x / *y when there's news.
static bool tp_poll(bool* down, uint16_t* x, uint16_t* y)
{
    uint8_t st[4];
    if (tp_read(0x2000, st, 4) != ESP_OK) return false;
    const bool ptExist = st[0] & 0x01, gesture = st[0] & 0x02;
    const bool inBios = st[1] & 0x40, inCpu = st[1] & 0x20, cpuRun = st[1] & 0x08;
    const uint16_t len = (uint16_t)(st[3] << 8 | st[2]);
    if (inBios) { tp_cmd(0x0200, 0x0001); tp_cmd(0x0400, 0x0001); return false; }      // clear INT, CPU start
    if (inCpu) { tp_cmd(0x5000, 0x0000); tp_cmd(0x4600, 0x0000); tp_cmd(0x0200, 0x0001); return false; }   // point mode, start, clear
    if (cpuRun && len == 0) { tp_cmd(0x0200, 0x0001); return false; }
    const bool aux = st[0] & 0x08;
    if (!(ptExist || gesture)) { if (cpuRun && aux) tp_cmd(0x0200, 0x0001); return false; }
    if (len < 4) return false;
    uint8_t d[4 + 10 * 6];
    const size_t n = len < sizeof d ? len : sizeof d;
    if (tp_read(0x0003, d, n) != ESP_OK) return false;
    bool news = false;
    if (d[4] <= 0x0A && ptExist) {                               // a point report: id, x, y, weight
        const int pts = (int)(n - 4) / 6;
        *down = pts > 0 && d[8] != 0;                            // weight 0: lifted
        if (pts > 0) {
            *x = (uint16_t)(((d[7] & 0xF0) << 4) | d[5]);
            *y = (uint16_t)(((d[7] & 0x0F) << 8) | d[6]);
        }
        news = true;
    }
    // the status after a report: read any rest of it, then clear the interrupt
    for (int tries = 0; tries < 4; tries++) {
        uint8_t h[8];
        if (tp_read(0xFC02, h, 8) != ESP_OK) break;
        if (h[5] == 0x82) { tp_cmd(0x0200, 0x0001); break; }
        if (h[5] != 0x00) break;
        const uint16_t more = (uint16_t)(h[2] | h[3] << 8);
        uint8_t rest[32];
        if (more) tp_read(0x0003, rest, more < sizeof rest ? more : sizeof rest);
    }
    return news;
}

static void IRAM_ATTR tp_isr(void*)
{
    BaseType_t woke = pdFALSE;
    xSemaphoreGiveFromISR(s_tpIrq, &woke);
    if (woke) portYIELD_FROM_ISR();
}

static void touch_task(void*)
{
    for (;;) {
        // INT, or a poll every 20 ms: a lift has to be seen even if INT is missed
        const bool irq = xSemaphoreTake(s_tpIrq, pdMS_TO_TICKS(20)) == pdTRUE;
        bool down = false; uint16_t x = 0, y = 0;
        const bool news = tp_poll(&down, &x, &y);
        portENTER_CRITICAL(&s_tpLock);
        if (news) {
            s_touch.down = down;
            if (down) { s_touch.x = x; s_touch.y = y; }
            s_touch.seq++;
        } else if (!irq && gpio_get_level(TOUCH_INT)) {
            if (s_touch.down) { s_touch.down = false; s_touch.seq++; }   // quiet and INT high: lifted
        }
        const Touch now = s_touch;
        portEXIT_CRITICAL(&s_tpLock);
        static uint32_t seen;
        if (s_touchFn && now.seq != seen) { seen = now.seq; s_touchFn(now); }
    }
}

Touch board_touch()
{
    portENTER_CRITICAL(&s_tpLock);
    const Touch t = s_touch;
    portEXIT_CRITICAL(&s_tpLock);
    return t;
}

void board_touch_start(void (*fn)(const Touch&))
{
    s_touchFn = fn;
    i2c_device_config_t dc = {};
    dc.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dc.device_address = TOUCH_ADDR;
    dc.scl_speed_hz = 400000;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(s_i2c, &dc, &s_tpDev));
    exp_pin(EXP_TOUCH_RST, false);
    vTaskDelay(pdMS_TO_TICKS(50));
    exp_pin(EXP_TOUCH_RST, true);
    vTaskDelay(pdMS_TO_TICKS(50));
    uint8_t ver[18] = {};
    if (tp_read(0x2600, ver, sizeof ver) == ESP_OK)
        ESP_LOGI(TAG, "touch: %.4s%.4s firmware %u", (const char*)ver + 14, (const char*)ver + 10, (unsigned)(ver[5] << 8 | ver[4]));
    else
        ESP_LOGE(TAG, "touch: no answer at 0x%02X", TOUCH_ADDR);

    s_tpIrq = xSemaphoreCreateBinary();
    gpio_config_t g = {};
    g.pin_bit_mask = 1ULL << TOUCH_INT;
    g.mode = GPIO_MODE_INPUT;
    g.pull_up_en = GPIO_PULLUP_ENABLE;
    g.intr_type = GPIO_INTR_NEGEDGE;
    ESP_ERROR_CHECK(gpio_config(&g));
    gpio_install_isr_service(0);
    ESP_ERROR_CHECK(gpio_isr_handler_add(TOUCH_INT, tp_isr, nullptr));
    xTaskCreatePinnedToCore(touch_task, "touch", 4096, nullptr, 5, nullptr, 1);
}
