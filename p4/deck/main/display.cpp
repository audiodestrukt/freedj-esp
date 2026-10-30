#include "display.h"

#include <string.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st7796.h"
#include "esp_lcd_touch_ft5x06.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "audio_out.h"
#include "board.h"

namespace display {

static const char* TAG = "display";
static esp_lcd_panel_handle_t s_panel;
static esp_lcd_touch_handle_t s_touch;
static constexpr int kStripRows = 40;  // LVGL draw buffer height (x2, internal DMA RAM)
static constexpr int kSpiHz = 40 * 1000 * 1000;  // fine on ~15 cm jumpers; drop to 20 if the image corrupts

// LVGL's default wait for a finished SPI transfer is `while (flushing);`,
// which kept core 0 at ~100 % while the waveform scrolled. Instead the
// LVGL task sleeps on this semaphore and the transfer-done ISR wakes it,
// leaving core 0 free for the loader, USB and (later) the network.
static SemaphoreHandle_t s_flushDone;

static bool IRAM_ATTR onFlushDone(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void*)
{
    BaseType_t woke = pdFALSE;
    xSemaphoreGiveFromISR(s_flushDone, &woke);
    return woke == pdTRUE;
}

static void waitFlush(lv_display_t*)
{
    // Called once per flush while it's in flight; LVGL clears `flushing`.
    if (xSemaphoreTake(s_flushDone, pdMS_TO_TICKS(200)) != pdTRUE) ESP_LOGW(TAG, "flush timeout");
}

static void probeI2c()
{
    i2c_master_bus_handle_t bus = audio_out::i2cBus();
    if (!bus) return;
    char found[128] = ""; int n = 0;
    for (int a = 0x08; a < 0x78; a++)
        if (i2c_master_probe(bus, a, 20) == ESP_OK)
            n += snprintf(found + n, sizeof found - n, " 0x%02X", a);
    ESP_LOGI(TAG, "I2C devices:%s  (0x18 codec, 0x38 touch)", n ? found : " none");
}

static void beginTouch()
{
    i2c_master_bus_handle_t bus = audio_out::i2cBus();
    if (!bus) { ESP_LOGW(TAG, "no I2C bus, touch disabled"); return; }
    esp_lcd_panel_io_handle_t io = nullptr;
    esp_lcd_panel_io_i2c_config_t ioc = {};    // ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG is C-only
    ioc.dev_addr = ESP_LCD_TOUCH_IO_I2C_FT5x06_ADDRESS;
    ioc.scl_speed_hz = 400000;
    ioc.control_phase_bytes = 1;
    ioc.lcd_cmd_bits = 8;
    ioc.flags.disable_control_phase = 1;
    if (esp_lcd_new_panel_io_i2c(bus, &ioc, &io) != ESP_OK) { ESP_LOGW(TAG, "touch io failed"); return; }
    esp_lcd_touch_config_t tc = {};
    tc.x_max = H;               // native portrait coordinates
    tc.y_max = W;
    tc.rst_gpio_num = board::TP_RST;
    // INT is wired but not used: LVGL polls touch every refresh anyway, and on
    // jumper wires the SPI clock couples into INT. Each false edge woke LVGL
    // through an event group (handled by the FreeRTOS timer task), which cost
    // ~25 % of a core while the waveform scrolled.
    tc.int_gpio_num = GPIO_NUM_NC;
    tc.flags.swap_xy = 1;       // landscape; mirror runs before the swap, so
    tc.flags.mirror_y = 1;      // raw y (screen x) is flipped to match the panel
    if (esp_lcd_touch_new_i2c_ft5x06(io, &tc, &s_touch) != ESP_OK) {
        ESP_LOGW(TAG, "FT6336U not answering — check CTP_SDA/SCL/RST wires");
        s_touch = nullptr;
        return;
    }
    ESP_LOGI(TAG, "touch up");
}

lv_display_t* begin()
{
    gpio_config_t bl = {};
    bl.pin_bit_mask = 1ULL << board::LCD_BL;
    bl.mode = GPIO_MODE_OUTPUT;
    gpio_config(&bl);
    gpio_set_level(board::LCD_BL, 0);

    spi_bus_config_t bus = {};
    bus.sclk_io_num = board::LCD_SCK;
    bus.mosi_io_num = board::LCD_MOSI;
    bus.miso_io_num = -1;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = W * kStripRows * 2;
    if (spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) return nullptr;

    esp_lcd_panel_io_handle_t io = nullptr;
    esp_lcd_panel_io_spi_config_t ioc = {};
    ioc.cs_gpio_num = board::LCD_CS;
    ioc.dc_gpio_num = board::LCD_DC;
    ioc.spi_mode = 0;
    ioc.pclk_hz = kSpiHz;
    ioc.trans_queue_depth = 4;
    ioc.lcd_cmd_bits = 8;
    ioc.lcd_param_bits = 8;
    if (esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &ioc, &io) != ESP_OK) return nullptr;

    esp_lcd_panel_dev_config_t pc = {};
    pc.reset_gpio_num = board::LCD_RST;
    pc.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR;
    pc.bits_per_pixel = 16;
    if (esp_lcd_new_panel_st7796(io, &pc, &s_panel) != ESP_OK) return nullptr;
    esp_lcd_panel_reset(s_panel);
    esp_lcd_panel_init(s_panel);
    esp_lcd_panel_invert_color(s_panel, false);
    esp_lcd_panel_disp_on_off(s_panel, true);
    ESP_LOGI(TAG, "ST7796S up: %dx%d, SPI %d MHz", W, H, kSpiHz / 1000000);

    probeI2c();
    beginTouch();

    lvgl_port_cfg_t lc = {};
    lc.task_priority = 3;           // below the loader (4) on core 0
    lc.task_stack = 8192;
    lc.task_affinity = 0;           // keep LVGL off the audio core
    lc.task_max_sleep_ms = 500;
    lc.task_stack_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_DEFAULT;
    lc.timer_period_ms = 5;
    if (lvgl_port_init(&lc) != ESP_OK) return nullptr;

    lvgl_port_display_cfg_t dc = {};
    dc.io_handle = io;
    dc.panel_handle = s_panel;
    dc.buffer_size = W * kStripRows;
    dc.double_buffer = true;
    dc.hres = W;
    dc.vres = H;
    dc.rotation.swap_xy = true;     // landscape, as verified with the test bars
    dc.rotation.mirror_x = true;
    dc.rotation.mirror_y = true;
    dc.color_format = LV_COLOR_FORMAT_RGB565;
    dc.flags.buff_dma = 1;
    dc.flags.swap_bytes = 1;        // panel takes big-endian RGB565
    lv_display_t* disp = lvgl_port_add_disp(&dc);
    if (!disp) return nullptr;
    // Replace the port's busy-wait flush handshake with a sleeping one.
    s_flushDone = xSemaphoreCreateBinary();
    lvgl_port_lock(0);
    esp_lcd_panel_io_callbacks_t cbs = {};
    cbs.on_color_trans_done = onFlushDone;
    esp_lcd_panel_io_register_event_callbacks(io, &cbs, nullptr);
    lv_display_set_flush_wait_cb(disp, waitFlush);
    lvgl_port_unlock();

    if (s_touch) {
        lvgl_port_touch_cfg_t tc = {};
        tc.disp = disp;
        tc.handle = s_touch;
        lvgl_port_add_touch(&tc);
    }
    gpio_set_level(board::LCD_BL, 1);
    return disp;
}

} // namespace display
