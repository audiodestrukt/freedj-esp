// SPDX-FileCopyrightText: 2026 Dan Newcome
// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio_out.h"
#include "board.h"
#include "usb_audio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"

namespace audio_out {

static const char* TAG = "audio";
static esp_codec_dev_handle_t s_codec;
static i2c_master_bus_handle_t s_bus;

static volatile uint32_t s_xruns;

i2c_master_bus_handle_t i2cBus() { return s_bus; }
uint32_t xruns() { return s_xruns; }

// The DMA ran out of queued audio and played silence (auto_clear).
static bool IRAM_ATTR onSendOverflow(i2s_chan_handle_t, i2s_event_data_t*, void*)
{
    s_xruns = s_xruns + 1;
    return false;
}

bool begin(uint32_t rate, int volume)
{
    // I2S master, MCLK = 256 × fs, codec is the slave.
    i2s_chan_handle_t tx = nullptr;
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    cc.auto_clear = true;  // underrun plays silence, not the stale buffer
    ESP_ERROR_CHECK(i2s_new_channel(&cc, &tx, nullptr));
    i2s_std_config_t sc = {};
    sc.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(rate);
    sc.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    sc.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    sc.gpio_cfg.mclk = board::I2S_MCLK;
    sc.gpio_cfg.bclk = board::I2S_BCLK;
    sc.gpio_cfg.ws   = board::I2S_WS;
    sc.gpio_cfg.dout = board::I2S_DOUT;
    sc.gpio_cfg.din  = I2S_GPIO_UNUSED;
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx, &sc));
    i2s_event_callbacks_t cbs = {};
    cbs.on_send_q_ovf = onSendOverflow;
    ESP_ERROR_CHECK(i2s_channel_register_event_callback(tx, &cbs, nullptr));
    ESP_ERROR_CHECK(i2s_channel_enable(tx));

    i2c_master_bus_config_t bc = {};
    bc.i2c_port = I2C_NUM_0;
    bc.sda_io_num = board::I2C_SDA;
    bc.scl_io_num = board::I2C_SCL;
    bc.clk_source = I2C_CLK_SRC_DEFAULT;
    bc.glitch_ignore_cnt = 7;
    bc.flags.enable_internal_pullup = true;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bc, &s_bus));

    audio_codec_i2c_cfg_t ic = {};
    ic.port = I2C_NUM_0;
    ic.addr = ES8311_CODEC_DEFAULT_ADDR;
    ic.bus_handle = s_bus;
    const audio_codec_ctrl_if_t* ctrl = audio_codec_new_i2c_ctrl(&ic);

    audio_codec_i2s_cfg_t dc = {};
    dc.port = I2S_NUM_0;
    dc.tx_handle = tx;
    const audio_codec_data_if_t* data = audio_codec_new_i2s_data(&dc);

    es8311_codec_cfg_t ec = {};
    ec.ctrl_if = ctrl;
    ec.gpio_if = audio_codec_new_gpio();
    ec.codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC;
    ec.master_mode = false;
    ec.use_mclk = true;
    ec.pa_pin = board::PA_EN;
    ec.pa_reverted = false;
    ec.hw_gain.pa_voltage = 5.0;
    ec.hw_gain.codec_dac_voltage = 3.3;
    ec.mclk_div = I2S_MCLK_MULTIPLE_256;
    const audio_codec_if_t* es8311 = es8311_codec_new(&ec);
    if (!ctrl || !data || !es8311) { ESP_LOGE(TAG, "codec interface setup failed"); return false; }

    esp_codec_dev_cfg_t dv = {};
    dv.dev_type = ESP_CODEC_DEV_TYPE_OUT;
    dv.codec_if = es8311;
    dv.data_if = data;
    s_codec = esp_codec_dev_new(&dv);

    esp_codec_dev_sample_info_t si = {};
    si.bits_per_sample = 16;
    si.channel = 2;
    si.channel_mask = 0x03;
    si.sample_rate = rate;
    if (esp_codec_dev_open(s_codec, &si) != ESP_CODEC_DEV_OK) { ESP_LOGE(TAG, "codec open failed"); return false; }
    esp_codec_dev_set_out_vol(s_codec, volume);
    ESP_LOGI(TAG, "ES8311 up: %lu Hz, codec volume %d", (unsigned long)rate, volume);
    return true;
}

void write(const int16_t* lr, size_t frames)
{
    // USB audio, when plugged in, gets the frames and the speaker gets
    // silence. Not a codec mute: that's I2C, and the touch chip on the same
    // bus is read from the other core (a collision aborted the deck). I2S
    // keeps running either way: its DMA paces the audio loop.
    if (usb_audio::active()) {
        usb_audio::write(lr, frames);
        static const int16_t zeros[2 * 256] = {};
        for (size_t done = 0; done < frames;) {
            const size_t n = frames - done < 256 ? frames - done : 256;
            esp_codec_dev_write(s_codec, (void*)zeros, n * 2 * sizeof(int16_t));
            done += n;
        }
        return;
    }
    esp_codec_dev_write(s_codec, (void*)lr, frames * 2 * sizeof(int16_t));
}

} // namespace audio_out
