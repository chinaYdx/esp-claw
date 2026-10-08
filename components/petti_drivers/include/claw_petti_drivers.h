// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "driver/spi_master.h"
#include "esp_codec_dev.h"
#include "i2c_bus.h"
#include "spi_nand_flash.h"

typedef struct claw_petti_imu *claw_petti_imu_handle_t;
typedef struct claw_petti_mag *claw_petti_mag_handle_t;
typedef struct claw_petti_audio *claw_petti_audio_handle_t;
typedef struct claw_petti_battery *claw_petti_battery_handle_t;
typedef struct claw_petti_nand *claw_petti_nand_handle_t;
/* Buses are borrowed from board manager. Driver instances own only devices. */
esp_err_t claw_petti_imu_create(i2c_master_bus_handle_t bus, uint8_t address, claw_petti_imu_handle_t *out);
esp_err_t claw_petti_imu_read(claw_petti_imu_handle_t handle, float accel[3], float gyro[3]);
esp_err_t claw_petti_imu_delete(claw_petti_imu_handle_t handle);
esp_err_t claw_petti_mag_create(i2c_master_bus_handle_t bus, uint8_t address, claw_petti_mag_handle_t *out);
esp_err_t claw_petti_mag_read(claw_petti_mag_handle_t handle, float xyz[3]);
esp_err_t claw_petti_mag_delete(claw_petti_mag_handle_t handle);
typedef struct {
    i2c_master_bus_handle_t bus;
    i2s_chan_handle_t tx, rx;
    uint8_t address_8bit;
    int pa_pin;
    esp_codec_dev_type_t type;
} claw_petti_audio_config_t;
esp_err_t claw_petti_audio_create(const claw_petti_audio_config_t *config, claw_petti_audio_handle_t *out);
esp_err_t claw_petti_audio_open(claw_petti_audio_handle_t handle, unsigned volume);
esp_err_t claw_petti_audio_read(claw_petti_audio_handle_t handle, void *data, size_t bytes);
esp_err_t claw_petti_audio_write(claw_petti_audio_handle_t handle, const void *data, size_t bytes);
esp_err_t claw_petti_audio_volume(claw_petti_audio_handle_t handle, unsigned volume);
esp_err_t claw_petti_audio_delete(claw_petti_audio_handle_t handle);
esp_err_t claw_petti_battery_create(i2c_bus_handle_t bus, claw_petti_battery_handle_t *out);
esp_err_t claw_petti_battery_read(claw_petti_battery_handle_t handle, unsigned *percent, bool *charging);
esp_err_t claw_petti_battery_delete(claw_petti_battery_handle_t handle);
typedef struct {
    spi_host_device_t host;
    int clk, data0, data1, data2, data3, cs;
    int clock_hz, max_transfer, queue_size;
} claw_petti_nand_config_t;
esp_err_t claw_petti_nand_create(const claw_petti_nand_config_t *config, claw_petti_nand_handle_t *out);
spi_nand_flash_device_t *claw_petti_nand_get_device(claw_petti_nand_handle_t handle);
esp_err_t claw_petti_nand_sync(claw_petti_nand_handle_t handle);
esp_err_t claw_petti_nand_delete(claw_petti_nand_handle_t handle);
