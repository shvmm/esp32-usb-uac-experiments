/* USB UAC Mic
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <math.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "usb_device_uac.h"
#include "driver/i2s_std.h"

// #define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
// #include "esp_log.h"
// #include "esp_err.h"
// #include "driver/ledc.h"

#define MIC_I2S_WS         7
#define MIC_I2S_DOUT       8
#define MIC_I2S_BCLK       9

#define SPEAKER_I2S_DOUT  13
#define SPEAKER_I2S_BCLK  14
#define SPEAKER_I2S_LRC   21
#define SPEAKER_SD_MODE   12

// static const char *TAG = "usb_uac_main";

static i2s_chan_handle_t rx_handle;
static i2s_chan_handle_t tx_handle;

static bool is_muted = false;
// volume is in dB
static uint32_t volume = 0;
static uint32_t volume_factor = 100;

static esp_err_t uac_device_output_cb(uint8_t *buf, size_t len, void *arg) { // Speaker
    if (!tx_handle) {
        return ESP_FAIL;
    }
    
    size_t bytes_written = 0;
    i2s_channel_write(tx_handle, buf, len, &bytes_written, portMAX_DELAY);
    
    // int32_t *samples = (int32_t *)buf;
    
    // for (size_t i = 0; i < len / 2; i++) {
    //     if (is_muted) {
    //         samples[i] = 0;
    //         continue;
    //     }
    //     int32_t sample = samples[i];
    //     // volume is in dB
    //     // sample = (sample * volume_factor) / 100;
    //     // if(sample > 32767) {
    //     //     sample = 32767;
    //     // } else if(sample < -32768) {
    //     //     sample = -32768;
    //     // }

    //     // sample = sample >> 8;

    //     samples[i] = (int32_t) sample;
    // }

    // size_t total_bytes_written = 0;
    
    
    // while(total_bytes_written < len) {
    //     size_t bytes_written = 0;
    //     i2s_channel_write(tx_handle, (uint8_t *)buf + total_bytes_written, len - total_bytes_written, &bytes_written, portMAX_DELAY);
    //     total_bytes_written += bytes_written;
    // }

    return ESP_OK;
}

static esp_err_t uac_device_input_cb(uint8_t *buf, size_t len, size_t *bytes_read, void *arg) { // Mic
    if(!rx_handle) {
        return ESP_FAIL;
    }
    // uint8_t *rx_buffer = (uint8_t *) malloc(READ_BUF_SIZE_BYTES);
    return i2s_channel_read(rx_handle, buf, len, bytes_read, portMAX_DELAY); // portMAX_DELAY
}

static void uac_device_set_mute_cb(uint32_t mute, void *arg) {
    is_muted = mute;
}

static void uac_device_set_volume_cb(uint32_t _volume, void *arg) {
    // see here for what is going on here: https://github.com/espressif/esp-iot-solution/blob/36d8130e8e880720108de2c31ce0779827b1bcd9/components/usb/usb_device_uac/usb_device_uac.c#L259
    // _volume = (volume_db + 50) * 2
    // when _volume is 100 %, volume_db is 0. When _volume is 0%, volume_db is -50 or 0.00001
    
    int volume_db = _volume / 2 - 50;
    volume_factor = pow(10, volume_db / 20.0f) * 100.0f;
    // volume_factor ranges from 1 to 10E-2.5

    // Windows volume behaviour, at 1 -> -60dB, at 0 -> -96dB, 100 -> +30dB
    // 
}

static void usb_uac_device_init(void) {
    uac_device_config_t config = {
        // .output_cb = uac_device_output_cb,
        .output_cb = NULL,

        .input_cb = uac_device_input_cb,
        .set_mute_cb = NULL,
        // .set_mute_cb = uac_device_set_mute_cb,
        .set_volume_cb = NULL,
        // .set_volume_cb = uac_device_set_volume_cb,
        .cb_ctx = NULL,
    };
    /* Init UAC device, UAC related configurations can be set by the menuconfig */
    ESP_ERROR_CHECK(uac_device_init(&config));
}

void init_std_rx(void) {
    // References: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/i2s.html#std-rx-mode
    
    // i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    
    chan_cfg.dma_desc_num = 8;
    chan_cfg.dma_frame_num = 256; // samples
    
    i2s_new_channel(&chan_cfg, NULL, &rx_handle);

    i2s_std_config_t std_cfg = {
        // .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(CONFIG_UAC_SAMPLE_RATE),
        
        .clk_cfg = {
            .sample_rate_hz = 48000,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            // .ext_clk_freq_hz = 0,
            .mclk_multiple = I2S_MCLK_MULTIPLE_384, // I2S_MCLK_MULTIPLE_256/384
            .bclk_div = 8,
        },

        // The ICS-43434 is a philips format mic
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_24BIT, I2S_SLOT_MODE_MONO), // MSB/PHILIPS/PCM and bit width can be 16/32 bits
        
        // .slot_cfg = {            
        //     .data_bit_width = I2S_DATA_BIT_WIDTH_24BIT,  // ICS 43434 datasheet
        //     .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
        //     .slot_mode = I2S_SLOT_MODE_MONO,
        //     .slot_mask = I2S_STD_SLOT_BOTH,
        //     .ws_width = I2S_DATA_BIT_WIDTH_24BIT, // ICS 43434 datasheet
        //     .ws_pol = false,
        //     .bit_shift = true,
        //     .left_align = true,
        //     .big_endian = false,
        //     .bit_order_lsb = false,
        // },
        
        .gpio_cfg = {
            
            .mclk = I2S_GPIO_UNUSED,

            .bclk = MIC_I2S_BCLK,
            // QUESTION - what about the SEL clock pin? No longer relevant? Do we ties it high or low? SEL is pulled low by default
            .ws = MIC_I2S_WS,
            .dout = I2S_GPIO_UNUSED,
            .din = MIC_I2S_DOUT,     // I2S data
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    // std_cfg.slot_cfg.slot_mode = I2S_STD_SLOT_LEFT; // single mic
    // std_cfg.slot_cfg.slot_mode = I2S_SLOT_MODE_MONO
    i2s_channel_init_std_mode(rx_handle, &std_cfg);
    i2s_channel_enable(rx_handle);
}

static void init_pcm_tx(void) {
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle, NULL));

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(CONFIG_UAC_SAMPLE_RATE),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,   // set this if your amp needs MCLK
            .bclk = SPEAKER_I2S_BCLK,
            .ws   = SPEAKER_I2S_LRC,
            .dout = SPEAKER_I2S_DOUT,
            // .dout = I2S_GPIO_UNUSED,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,     // if L/R are swapped or silent, try true
            },
        },
    };

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(tx_handle));
}

void app_main(void) {
    
    // ESP_LOGI(TAG, "ICS 43434 USB MIC Booting\n");
    
    init_std_rx();
    // init_pcm_tx();
    usb_uac_device_init();

    // enable the amplifier
    gpio_reset_pin(SPEAKER_SD_MODE);
    gpio_set_direction(SPEAKER_SD_MODE, GPIO_MODE_OUTPUT);
    gpio_set_level(SPEAKER_SD_MODE, 1);

    // Optional code to pull up/down mic SEL for STEREO configuration
    // gpio_reset_pin(MIC_I2S_SEL);
    // gpio_set_direction(MIC_I2S_SEL, GPIO_MODE_OUTPUT);
    // gpio_set_level(MIC_I2S_SEL, 0);

    // Nothing to do here - the USB audio device will take care of everything
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
