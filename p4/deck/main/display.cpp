#include "display.h"

#include <string.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st7796.h"
#include "esp_lcd_touch_ft5x06.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "audio_out.h"
#include "board.h"

namespace display {

static const char* TAG = "display";
static esp_lcd_panel_handle_t s_panel;
static esp_lcd_touch_handle_t s_touch;
static SemaphoreHandle_t s_done;       // SPI finished with s_strip
static uint16_t* s_strip;               // DMA bounce buffer, W x kStripRows
static constexpr int kStripRows = 40;
static constexpr int kSpiHz = 20 * 1000 * 1000;  // conservative for jumper wires

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
    tc.int_gpio_num = board::TP_INT;
    tc.flags.swap_xy = 1;       // landscape; mirror runs before the swap, so
    tc.flags.mirror_y = 1;      // raw y (screen x) is flipped to match the panel
    if (esp_lcd_touch_new_i2c_ft5x06(io, &tc, &s_touch) != ESP_OK) {
        ESP_LOGW(TAG, "FT6336U not answering — check CTP_SDA/SCL/RST wires");
        s_touch = nullptr;
        return;
    }
    ESP_LOGI(TAG, "touch up");
}

static bool onDone(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void*)
{
    BaseType_t woke = pdFALSE;
    xSemaphoreGiveFromISR(s_done, &woke);
    return woke == pdTRUE;
}

static void draw(int x, int y, int x2, int y2)
{
    esp_lcd_panel_draw_bitmap(s_panel, x, y, x2, y2, s_strip);
    xSemaphoreTake(s_done, portMAX_DELAY);
}

bool begin()
{
    s_done = xSemaphoreCreateBinary();
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
    if (spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) return false;

    esp_lcd_panel_io_handle_t io = nullptr;
    esp_lcd_panel_io_spi_config_t ioc = {};
    ioc.cs_gpio_num = board::LCD_CS;
    ioc.dc_gpio_num = board::LCD_DC;
    ioc.spi_mode = 0;
    ioc.pclk_hz = kSpiHz;
    ioc.trans_queue_depth = 4;
    ioc.lcd_cmd_bits = 8;
    ioc.lcd_param_bits = 8;
    ioc.on_color_trans_done = onDone;
    if (esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &ioc, &io) != ESP_OK) return false;

    esp_lcd_panel_dev_config_t pc = {};
    pc.reset_gpio_num = board::LCD_RST;
    pc.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR;
    pc.bits_per_pixel = 16;
    if (esp_lcd_new_panel_st7796(io, &pc, &s_panel) != ESP_OK) return false;
    esp_lcd_panel_reset(s_panel);
    esp_lcd_panel_init(s_panel);
    esp_lcd_panel_swap_xy(s_panel, true);       // landscape
    esp_lcd_panel_mirror(s_panel, true, true);
    esp_lcd_panel_invert_color(s_panel, false);
    esp_lcd_panel_disp_on_off(s_panel, true);

    s_strip = (uint16_t*)heap_caps_malloc(W * kStripRows * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!s_strip) return false;
    fill(0, 0, W, H, 0x0000);
    gpio_set_level(board::LCD_BL, 1);
    ESP_LOGI(TAG, "ST7796S up: %dx%d, SPI %d MHz", W, H, kSpiHz / 1000000);

    probeI2c();
    beginTouch();
    return true;
}

// Panel wants big-endian RGB565; strips are copied through the DMA buffer.
void blit(int x, int y, int w, int h, const uint16_t* px)
{
    const int rows = kStripRows * W / w;
    for (int r = 0; r < h; r += rows) {
        const int n = h - r < rows ? h - r : rows;
        for (int i = 0; i < n * w; i++) { uint16_t c = px[r * w + i]; s_strip[i] = (c >> 8) | (c << 8); }
        draw(x, y + r, x + w, y + r + n);
    }
}

void fill(int x, int y, int w, int h, uint16_t c)
{
    const uint16_t be = (c >> 8) | (c << 8);
    const int rows = kStripRows * W / w;
    for (int i = 0; i < w * (rows < h ? rows : h); i++) s_strip[i] = be;
    for (int r = 0; r < h; r += rows) {
        const int n = h - r < rows ? h - r : rows;
        draw(x, y + r, x + w, y + r + n);
    }
}

bool touch(int* x, int* y)
{
    if (!s_touch) return false;
    esp_lcd_touch_read_data(s_touch);
    esp_lcd_touch_point_data_t p = {};
    uint8_t cnt = 0;
    if (esp_lcd_touch_get_data(s_touch, &p, &cnt, 1) != ESP_OK || !cnt) return false;
    *x = p.x < W ? p.x : W - 1;
    *y = p.y < H ? p.y : H - 1;
    return true;
}

} // namespace display
