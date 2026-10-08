/* SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 * Missing in ESP-Claw 74b1870: BQ27220 backend preserves Mosaico's 80mAh
 * CEDV profile and sealing step. Wrapper bus is borrowed, never deleted here.
 */
#include "esp_assert.h"
/* BQ27220's public header uses ESP_STATIC_ASSERT without including its provider. */
#include "bq27220.h"
#include "claw_petti_drivers.h"
#include "claw_petti_math.h"
#include <stdlib.h>
struct claw_petti_battery {
    bq27220_handle_t gauge;
    i2c_bus_device_handle_t checked_io;
};
/* Interim fixed-EDV profile characterized on two 80 mAh cells at 25 C. */
static const parameter_cedv_t s_default_cedv = {
    .full_charge_cap = 80,
    .design_cap = 80,
    .reserve_cap = 0,
    .near_full = 5,
    .self_discharge_rate = 20,
    .EDV0 = 3000,
    .EDV1 = 3410,
    .EDV2 = 3530,
    .EMF = 3670,
    .C0 = 115,
    .R0 = 968,
    .T0 = 4547,
    .R1 = 4764,
    .TC = 11,
    .C1 = 0,
    .DOD0 = 4147,
    .DOD10 = 4002,
    .DOD20 = 3969,
    .DOD30 = 3938,
    .DOD40 = 3880,
    .DOD50 = 3824,
    .DOD60 = 3794,
    .DOD70 = 3753,
    .DOD80 = 3677,
    .DOD90 = 3574,
    .DOD100 = 3490,
};

static const gauging_config_t s_default_gauging = {
    .CCT = true,
    .SC = true,
    .FCC_LIM = true,
    .FC_FOR_VDQ = true,
    .IGNORE_SD = true,
};

esp_err_t claw_petti_battery_create(i2c_bus_handle_t bus, claw_petti_battery_handle_t *out)
{
    if (!bus || !out)
        return ESP_ERR_INVALID_ARG;
    *out = NULL;
    claw_petti_battery_handle_t h = calloc(1, sizeof(*h));
    if (!h)
        return ESP_ERR_NO_MEM;
    bq27220_config_t c = {.i2c_bus = bus, .cfg = &s_default_gauging, .cedv = &s_default_cedv};
    h->gauge = bq27220_create(&c);
    if (!h->gauge) {
        free(h);
        return ESP_FAIL;
    }
    esp_err_t ret = bq27220_seal(h->gauge);
    if (ret != ESP_OK) {
        bq27220_delete(h->gauge);
        free(h);
        return ret;
    }
    h->checked_io = i2c_bus_device_create(bus, 0x55, 400000);
    if (!h->checked_io) {
        bq27220_delete(h->gauge);
        free(h);
        return ESP_FAIL;
    }
    *out = h;
    return ESP_OK;
}
esp_err_t claw_petti_battery_read(claw_petti_battery_handle_t h, unsigned *percent, bool *charging)
{
    if (!h || !percent || !charging)
        return ESP_ERR_INVALID_ARG;
    /* Gauge convenience getters swallow IO errors as zero. Check both reads. */
    uint8_t soc[2], current[2];
    esp_err_t ret = i2c_bus_read_bytes(h->checked_io, 0x2c, 2, soc);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = i2c_bus_read_bytes(h->checked_io, 0x0c, 2, current);
    if (ret != ESP_OK) {
        return ret;
    }
    return claw_petti_battery_decode(soc, current, percent, charging) ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}
esp_err_t claw_petti_battery_delete(claw_petti_battery_handle_t h)
{
    if (!h)
        return ESP_OK;
    esp_err_t ret = bq27220_delete(h->gauge);
    esp_err_t io = i2c_bus_device_delete(&h->checked_io);
    free(h);
    return ret == ESP_OK ? io : ret;
}
