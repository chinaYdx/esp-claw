/* SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 * Missing in ESP-Claw 74b1870. Derived from Mosaico onboard/nand_flash.c.
 * SIO bootstrap sets/reads manufacturer-specific QE before QIO. No VFS mount,
 * formatting, erase, partition, or filesystem policy in this driver.
 */
#include "claw_petti_drivers.h"
#include "claw_petti_math.h"
#include "driver/gpio.h"
#include <stdlib.h>
struct claw_petti_nand {
    claw_petti_nand_config_t config;
    spi_device_handle_t io;
    spi_nand_flash_device_t *device;
    bool bus_ready;
};
esp_err_t claw_petti_nand_delete(claw_petti_nand_handle_t h)
{
    if (!h)
        return ESP_OK;
    esp_err_t ret = ESP_OK, err;
    if (h->device) {
        err = spi_nand_flash_deinit_device(h->device);
        if (ret == ESP_OK)
            ret = err;
    }
    if (h->io) {
        err = spi_bus_remove_device(h->io);
        if (ret == ESP_OK)
            ret = err;
    }
    if (h->bus_ready) {
        err = spi_bus_free(h->config.host);
        if (ret == ESP_OK)
            ret = err;
    }
    free(h);
    return ret;
}
static esp_err_t feature(spi_device_handle_t io, uint8_t cmd, uint8_t addr, uint8_t *value, bool write)
{
    spi_transaction_ext_t t = {.base = {.flags = SPI_TRANS_VARIABLE_ADDR | SPI_TRANS_VARIABLE_CMD |
                                                 SPI_TRANS_VARIABLE_DUMMY |
                                                 (write ? SPI_TRANS_USE_TXDATA : SPI_TRANS_USE_RXDATA),
                                        .cmd = cmd,
                                        .addr = addr},
                               .command_bits = 8,
                               .address_bits = 8};
    if (write) {
        t.base.length = 8;
        t.base.tx_data[0] = *value;
    } else
        t.base.rxlength = 8;
    esp_err_t ret = spi_device_transmit(io, (spi_transaction_t *)&t);
    if (ret == ESP_OK && !write)
        *value = t.base.rx_data[0];
    return ret;
}
static esp_err_t quad_enable(spi_device_handle_t io)
{
    uint8_t id = 0, config = 0;
    esp_err_t ret = feature(io, 0x9f, 0, &id, false);
    if (ret != ESP_OK)
        return ret;
    bool needs_qe = false;
    if (!claw_petti_nand_qe_policy(id, &needs_qe)) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (!needs_qe) {
        return ESP_OK;
    }
    ret = feature(io, 0x0f, 0xb0, &config, false);
    if (ret != ESP_OK)
        return ret;
    if (!(config & 1)) {
        config |= 1;
        ret = feature(io, 0x1f, 0xb0, &config, true);
        if (ret != ESP_OK)
            return ret;
        ret = feature(io, 0x0f, 0xb0, &config, false);
        if (ret != ESP_OK)
            return ret;
    }
    return config & 1 ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}
static esp_err_t create_bus(claw_petti_nand_handle_t h, bool quad)
{
    const claw_petti_nand_config_t *c = &h->config;
    spi_bus_config_t bus = {.sclk_io_num = c->clk,
                            .mosi_io_num = c->data0,
                            .miso_io_num = c->data1,
                            .quadwp_io_num = quad ? c->data2 : -1,
                            .quadhd_io_num = quad ? c->data3 : -1,
                            .max_transfer_sz = c->max_transfer};
    esp_err_t ret = spi_bus_initialize(c->host, &bus, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK)
        return ret;
    h->bus_ready = true;
    spi_device_interface_config_t dev = {.clock_speed_hz = c->clock_hz,
                                         .spics_io_num = c->cs,
                                         .mode = 0,
                                         .queue_size = c->queue_size,
                                         .flags = SPI_DEVICE_HALFDUPLEX};
    return spi_bus_add_device(c->host, &dev, &h->io);
}
esp_err_t claw_petti_nand_create(const claw_petti_nand_config_t *c, claw_petti_nand_handle_t *out)
{
    if (!c || !out || c->clock_hz <= 0 || c->max_transfer <= 0 || c->queue_size <= 0)
        return ESP_ERR_INVALID_ARG;
    int pins[] = {c->clk, c->data0, c->data1, c->data2, c->data3, c->cs};
    for (unsigned i = 0; i < sizeof(pins) / sizeof(pins[0]); i++)
        if (!GPIO_IS_VALID_OUTPUT_GPIO(pins[i])) {
            return ESP_ERR_INVALID_ARG;
        }
    *out = NULL;
    claw_petti_nand_handle_t h = calloc(1, sizeof(*h));
    if (!h)
        return ESP_ERR_NO_MEM;
    h->config = *c;
    gpio_config_t controls = {.pin_bit_mask = (UINT64_C(1) << c->data2) | (UINT64_C(1) << c->data3),
                              .mode = GPIO_MODE_OUTPUT,
                              .pull_up_en = GPIO_PULLUP_ENABLE};
    esp_err_t ret = gpio_set_level(c->data2, 1);
    if (ret != ESP_OK)
        goto fail;
    ret = gpio_set_level(c->data3, 1);
    if (ret != ESP_OK)
        goto fail;
    ret = gpio_config(&controls);
    if (ret != ESP_OK)
        goto fail;
    ret = create_bus(h, false);
    if (ret != ESP_OK)
        goto fail;
    ret = quad_enable(h->io);
    if (ret != ESP_OK)
        goto fail;
    ret = spi_bus_remove_device(h->io);
    if (ret != ESP_OK)
        goto fail;
    h->io = NULL;
    ret = spi_bus_free(c->host);
    if (ret != ESP_OK)
        goto fail;
    h->bus_ready = false;
    ret = create_bus(h, true);
    if (ret != ESP_OK)
        goto fail;
    spi_nand_flash_config_t flash = {.device_handle = h->io,
                                     .gc_factor = 0,
                                     .io_mode = SPI_NAND_IO_MODE_QIO,
                                     .flags = SPI_DEVICE_HALFDUPLEX};
    ret = spi_nand_flash_init_device(&flash, &h->device);
    if (ret != ESP_OK)
        goto fail;
    *out = h;
    return ESP_OK;
fail:
    claw_petti_nand_delete(h);
    return ret;
}
spi_nand_flash_device_t *claw_petti_nand_get_device(claw_petti_nand_handle_t h)
{
    return h ? h->device : NULL;
}
esp_err_t claw_petti_nand_sync(claw_petti_nand_handle_t h)
{
    return h && h->device ? spi_nand_flash_sync(h->device) : ESP_ERR_INVALID_STATE;
}
