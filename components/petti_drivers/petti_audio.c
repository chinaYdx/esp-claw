/* SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 * Non-Lua device open/volume/read/write flow extracted from
 * lua_module_audio/src/audio_device.c at 74b1870. Board owns borrowed buses.
 */
#include "claw_petti_drivers.h"
#include "esp_codec_dev_defaults.h"
#include <limits.h>
#include <stdlib.h>
struct claw_petti_audio {
    esp_codec_dev_handle_t codec;
    const audio_codec_data_if_t *data;
    const audio_codec_ctrl_if_t *ctrl;
    const audio_codec_gpio_if_t *gpio;
    const audio_codec_if_t *chip;
    esp_codec_dev_type_t type;
    bool opened;
};
esp_err_t claw_petti_audio_delete(claw_petti_audio_handle_t h)
{
    if (!h)
        return ESP_OK;
    if (h->codec) {
        if (h->opened)
            esp_codec_dev_close(h->codec);
        esp_codec_dev_delete(h->codec);
    }
    if (h->chip)
        audio_codec_delete_codec_if(h->chip);
    if (h->ctrl)
        audio_codec_delete_ctrl_if(h->ctrl);
    if (h->data)
        audio_codec_delete_data_if(h->data);
    if (h->gpio)
        audio_codec_delete_gpio_if(h->gpio);
    free(h);
    return ESP_OK;
}
esp_err_t claw_petti_audio_create(const claw_petti_audio_config_t *c, claw_petti_audio_handle_t *out)
{
    if (!c || !out || !c->bus || !c->tx || !c->rx)
        return ESP_ERR_INVALID_ARG;
    *out = NULL;
    claw_petti_audio_handle_t h = calloc(1, sizeof(*h));
    if (!h)
        return ESP_ERR_NO_MEM;
    h->type = c->type;
    audio_codec_i2s_cfg_t data = {.port = 0, .tx_handle = c->tx, .rx_handle = c->rx};
    h->data = audio_codec_new_i2s_data(&data);
    h->gpio = audio_codec_new_gpio();
    audio_codec_i2c_cfg_t ctrl = {.bus_handle = c->bus, .addr = c->address_8bit};
    h->ctrl = audio_codec_new_i2c_ctrl(&ctrl);
    if (!h->data || !h->gpio || !h->ctrl)
        goto fail;
    es8311_codec_cfg_t chip = {.ctrl_if = h->ctrl,
                               .gpio_if = h->gpio,
                               .codec_mode = c->type == ESP_CODEC_DEV_TYPE_OUT ? ESP_CODEC_DEV_WORK_MODE_DAC
                                                                               : ESP_CODEC_DEV_WORK_MODE_ADC,
                               .pa_pin = c->pa_pin,
                               .use_mclk = true,
                               .master_mode = false,
                               .digital_mic = false,
                               .hw_gain = {.pa_voltage = 5.0f, .codec_dac_voltage = 3.3f},
                               .no_dac_ref = true};
    h->chip = es8311_codec_new(&chip);
    if (!h->chip)
        goto fail;
    esp_codec_dev_cfg_t dev = {.dev_type = c->type, .codec_if = h->chip, .data_if = h->data};
    h->codec = esp_codec_dev_new(&dev);
    if (!h->codec)
        goto fail;
    *out = h;
    return ESP_OK;
fail:
    claw_petti_audio_delete(h);
    return ESP_FAIL;
}
esp_err_t claw_petti_audio_open(claw_petti_audio_handle_t h, unsigned volume)
{
    if (!h || volume > 100)
        return ESP_ERR_INVALID_ARG;
    if (h->opened)
        return ESP_ERR_INVALID_STATE;
    esp_codec_dev_sample_info_t format = {.sample_rate = 48000, .channel = 2, .bits_per_sample = 16};
    if (esp_codec_dev_open(h->codec, &format) != ESP_CODEC_DEV_OK)
        return ESP_FAIL;
    int ret = h->type == ESP_CODEC_DEV_TYPE_OUT ? esp_codec_dev_set_out_vol(h->codec, volume)
                                                : esp_codec_dev_set_in_gain(h->codec, 30.0f);
    if (ret != ESP_CODEC_DEV_OK) {
        esp_codec_dev_close(h->codec);
        return ESP_FAIL;
    }
    h->opened = true;
    return ESP_OK;
}
esp_err_t claw_petti_audio_volume(claw_petti_audio_handle_t h, unsigned volume)
{
    if (!h || !h->opened || volume > 100 || h->type != ESP_CODEC_DEV_TYPE_OUT)
        return ESP_ERR_INVALID_ARG;
    return esp_codec_dev_set_out_vol(h->codec, volume) == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
}
esp_err_t claw_petti_audio_read(claw_petti_audio_handle_t h, void *data, size_t bytes)
{
    if (!h || !h->opened || !data || bytes > INT_MAX || h->type != ESP_CODEC_DEV_TYPE_IN)
        return ESP_ERR_INVALID_ARG;
    return esp_codec_dev_read(h->codec, data, (int)bytes) == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
}
esp_err_t claw_petti_audio_write(claw_petti_audio_handle_t h, const void *data, size_t bytes)
{
    if (!h || !h->opened || !data || bytes > INT_MAX || h->type != ESP_CODEC_DEV_TYPE_OUT)
        return ESP_ERR_INVALID_ARG;
    return esp_codec_dev_write(h->codec, (void *)data, (int)bytes) == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
}
