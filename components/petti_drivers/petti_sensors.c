/* SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 * Derived from lua_module_imu/src/bmi270/bmi270_backend.c and
 * lua_module_magnetometer/src/bmm150/bmm150_backend.c at 74b1870.
 * Bus callbacks use IDF's borrowed bus, enabling fixed BMI270 0x69 and two
 * independent BMM150 instances without address scanning or Lua glue.
 */
#include "bmi270.h"
#include "bmm150.h"
#include "claw_petti_drivers.h"
#include "claw_petti_math.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>

struct claw_petti_imu {
    struct bmi2_dev dev;
    i2c_master_dev_handle_t io;
};
struct claw_petti_mag {
    struct bmm150_dev dev;
    i2c_master_dev_handle_t io;
};
static int8_t read_reg(uint8_t reg, uint8_t *data, uint32_t len, void *ctx)
{
    return i2c_master_transmit_receive(*(i2c_master_dev_handle_t *)ctx, &reg, 1, data, len, 100) == ESP_OK
               ? 0
               : -1;
}
static int8_t write_reg(uint8_t reg, const uint8_t *data, uint32_t len, void *ctx)
{
    uint8_t buffer[33];
    if (len > 32)
        return -1;
    buffer[0] = reg;
    memcpy(buffer + 1, data, len);
    return i2c_master_transmit(*(i2c_master_dev_handle_t *)ctx, buffer, len + 1, 100) == ESP_OK ? 0 : -1;
}
static void delay_us(uint32_t us, void *ctx)
{
    (void)ctx;
    esp_rom_delay_us(us);
}
static esp_err_t attach(i2c_master_bus_handle_t bus, uint8_t address, i2c_master_dev_handle_t *out)
{
    i2c_device_config_t c = {
        .device_address = address, .dev_addr_length = I2C_ADDR_BIT_LEN_7, .scl_speed_hz = 400000};
    return i2c_master_bus_add_device(bus, &c, out);
}
esp_err_t claw_petti_imu_create(i2c_master_bus_handle_t bus, uint8_t address, claw_petti_imu_handle_t *out)
{
    if (!bus || !out || (address != 0x68 && address != 0x69))
        return ESP_ERR_INVALID_ARG;
    *out = NULL;
    claw_petti_imu_handle_t h = calloc(1, sizeof(*h));
    if (!h)
        return ESP_ERR_NO_MEM;
    esp_err_t err = attach(bus, address, &h->io);
    if (err != ESP_OK) {
        free(h);
        return err;
    }
    h->dev = (struct bmi2_dev){.intf = BMI2_I2C_INTF,
                               .intf_ptr = &h->io,
                               .read = read_reg,
                               .write = write_reg,
                               .delay_us = delay_us,
                               .read_write_len = 32};
    if (bmi270_init(&h->dev) != BMI2_OK)
        goto fail;
    if (bmi2_set_adv_power_save(BMI2_DISABLE, &h->dev) != BMI2_OK)
        goto fail;
    struct bmi2_sens_config c[2] = {{.type = BMI2_ACCEL}, {.type = BMI2_GYRO}};
    if (bmi2_get_sensor_config(c, 2, &h->dev) != BMI2_OK)
        goto fail;
    /* Preserve Petti P5: 50Hz, +/-4g, +/-2000deg/s. */
    c[0].cfg.acc.odr = BMI2_ACC_ODR_50HZ;
    c[0].cfg.acc.range = BMI2_ACC_RANGE_4G;
    c[0].cfg.acc.bwp = BMI2_ACC_NORMAL_AVG4;
    c[0].cfg.acc.filter_perf = BMI2_PERF_OPT_MODE;
    c[1].cfg.gyr.odr = BMI2_GYR_ODR_50HZ;
    c[1].cfg.gyr.range = BMI2_GYR_RANGE_2000;
    c[1].cfg.gyr.bwp = BMI2_GYR_NORMAL_MODE;
    c[1].cfg.gyr.noise_perf = BMI2_POWER_OPT_MODE;
    c[1].cfg.gyr.filter_perf = BMI2_PERF_OPT_MODE;
    uint8_t enabled[2] = {BMI2_ACCEL, BMI2_GYRO};
    if (bmi2_set_sensor_config(c, 2, &h->dev) != BMI2_OK ||
        bmi270_sensor_enable(enabled, 2, &h->dev) != BMI2_OK)
        goto fail;
    *out = h;
    return ESP_OK;
fail:
    i2c_master_bus_rm_device(h->io);
    free(h);
    return ESP_FAIL;
}
esp_err_t claw_petti_imu_read(claw_petti_imu_handle_t h, float accel[3], float gyro[3])
{
    if (!h || !accel || !gyro)
        return ESP_ERR_INVALID_ARG;
    struct bmi2_sens_data data = {0};
    if (bmi2_get_sensor_data(&data, &h->dev) != BMI2_OK)
        return ESP_FAIL;
    accel[0] = claw_petti_accel_g(data.acc.x);
    accel[1] = claw_petti_accel_g(data.acc.y);
    accel[2] = claw_petti_accel_g(data.acc.z);
    gyro[0] = claw_petti_gyro_dps(data.gyr.x);
    gyro[1] = claw_petti_gyro_dps(data.gyr.y);
    gyro[2] = claw_petti_gyro_dps(data.gyr.z);
    return ESP_OK;
}
esp_err_t claw_petti_imu_delete(claw_petti_imu_handle_t h)
{
    if (!h)
        return ESP_OK;
    esp_err_t err = i2c_master_bus_rm_device(h->io);
    free(h);
    return err;
}
esp_err_t claw_petti_mag_create(i2c_master_bus_handle_t bus, uint8_t address, claw_petti_mag_handle_t *out)
{
    if (!bus || !out || address < 0x10 || address > 0x13)
        return ESP_ERR_INVALID_ARG;
    *out = NULL;
    claw_petti_mag_handle_t h = calloc(1, sizeof(*h));
    if (!h)
        return ESP_ERR_NO_MEM;
    esp_err_t err = attach(bus, address, &h->io);
    if (err != ESP_OK) {
        free(h);
        return err;
    }
    h->dev = (struct bmm150_dev){.intf = BMM150_I2C_INTF,
                                 .intf_ptr = &h->io,
                                 .read = read_reg,
                                 .write = write_reg,
                                 .delay_us = delay_us};
    uint8_t power = 1;
    if (bmm150_set_regs(BMM150_REG_POWER_CONTROL, &power, 1, &h->dev) != BMM150_OK)
        goto fail;
    delay_us(BMM150_START_UP_TIME, NULL);
    if (bmm150_init(&h->dev) != BMM150_OK || h->dev.chip_id != BMM150_CHIP_ID)
        goto fail;
    uint8_t rep = 0;
    if (bmm150_set_regs(BMM150_REG_REP_XY, &rep, 1, &h->dev) != BMM150_OK ||
        bmm150_set_regs(BMM150_REG_REP_Z, &rep, 1, &h->dev) != BMM150_OK)
        goto fail;
    *out = h;
    return ESP_OK;
fail:
    i2c_master_bus_rm_device(h->io);
    free(h);
    return ESP_FAIL;
}
esp_err_t claw_petti_mag_read(claw_petti_mag_handle_t h, float xyz[3])
{
    if (!h || !xyz)
        return ESP_ERR_INVALID_ARG;
    struct bmm150_settings c = {.pwr_mode = BMM150_POWERMODE_FORCED};
    if (bmm150_set_op_mode(&c, &h->dev) != BMM150_OK)
        return ESP_FAIL;
    /* Upstream read immediately after triggering; wait for a fresh conversion. */
    int64_t deadline = esp_timer_get_time() + 200000;
    for (;;) {
        uint8_t ready = 0;
        if (bmm150_get_regs(BMM150_REG_DATA_READY_STATUS, &ready, 1, &h->dev) != BMM150_OK)
            return ESP_FAIL;
        if (ready & BMM150_DRDY_STATUS_MSK)
            break;
        if (esp_timer_get_time() >= deadline)
            return ESP_ERR_TIMEOUT;
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    struct bmm150_mag_data data = {0};
    if (bmm150_read_mag_data(&data, &h->dev) != BMM150_OK)
        return ESP_FAIL;
    xyz[0] = data.x;
    xyz[1] = data.y;
    xyz[2] = data.z;
    return ESP_OK;
}
esp_err_t claw_petti_mag_delete(claw_petti_mag_handle_t h)
{
    if (!h)
        return ESP_OK;
    esp_err_t err = i2c_master_bus_rm_device(h->io);
    free(h);
    return err;
}
