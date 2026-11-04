/* SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 * Missing in ESP-Claw 74b1870: BQ27220 backend preserves Mosaico's 80mAh
 * CEDV profile and sealing step. Wrapper bus is borrowed, never deleted here.
 * A gauge that refuses the default unseal keys stays SEALED: its standard commands still answer, so the
 * handle then runs read-only (no profile write, no key, no security-state change) and keeps reporting.
 */
#include "esp_assert.h"
/* BQ27220's public header uses ESP_STATIC_ASSERT without including its provider. */
#include "bq27220.h"
#include "claw_petti_drivers.h"
#include "claw_petti_math.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
static const char *TAG = "petti_battery";
#define GAUGE_ADDR 0x55
#define GAUGE_REG_CONTROL 0x00
#define GAUGE_REG_VOLTAGE 0x08
#define GAUGE_REG_CURRENT 0x0c
#define GAUGE_REG_SOC 0x2c
#define GAUGE_REG_OPERATION_STATUS 0x3a
#define GAUGE_REG_DESIGN_CAPACITY 0x3c
#define GAUGE_REG_MAC_DATA 0x40
#define GAUGE_CONTROL_DEVICE_NUMBER 0x0001
struct claw_petti_battery {
    bq27220_handle_t gauge; /* NULL: gauge stayed SEALED, only standard commands are used */
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

/* bq27220_create() only says NULL. Decide from read-only traffic whether a real BQ27220 with a battery
 * answers: Control(DEVICE_NUMBER) is a read request, the other three are standard commands. Nothing here
 * writes data memory, sends a key, or seals/unseals. */
static esp_err_t attach_read_only(i2c_bus_device_handle_t io)
{
    uint8_t request[2] = {GAUGE_CONTROL_DEVICE_NUMBER & 0xff, GAUGE_CONTROL_DEVICE_NUMBER >> 8};
    uint8_t device[2], status[2], voltage[2], design[2];
    esp_err_t ret = i2c_bus_write_bytes(io, GAUGE_REG_CONTROL, sizeof(request), request);
    if (ret == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(15));
        ret = i2c_bus_read_bytes(io, GAUGE_REG_MAC_DATA, sizeof(device), device);
    }
    if (ret == ESP_OK)
        ret = i2c_bus_read_bytes(io, GAUGE_REG_OPERATION_STATUS, sizeof(status), status);
    if (ret == ESP_OK)
        ret = i2c_bus_read_bytes(io, GAUGE_REG_VOLTAGE, sizeof(voltage), voltage);
    if (ret == ESP_OK)
        ret = i2c_bus_read_bytes(io, GAUGE_REG_DESIGN_CAPACITY, sizeof(design), design);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BQ27220 0x%02X does not answer: no gauge, no battery reading", GAUGE_ADDR);
        return ESP_ERR_NOT_FOUND;
    }
    claw_petti_gauge_probe_t probe;
    if (!claw_petti_gauge_probe_decode(device, status, voltage, design, &probe)) {
        ESP_LOGE(TAG, "BQ27220 probe rejected: device=%02x%02x OperationStatus=%02x%02x voltage=%02x%02x: "
                      "not a BQ27220 with a battery attached",
                 device[1], device[0], status[1], status[0], voltage[1], voltage[0]);
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (probe.security == CLAW_PETTI_GAUGE_SEC_SEALED)
        ESP_LOGW(TAG, "BQ27220 is SEALED and rejected the default unseal keys (OperationStatus=0x%04x SEC=%u): "
                      "battery profile not written, security state untouched, reporting SOC/current from the "
                      "standard commands (read-only). voltage=%u mV design_capacity=%u mAh (profile expects %u)",
                 probe.operation_status, probe.security, probe.voltage_mv, probe.design_capacity_mah,
                 s_default_cedv.design_cap);
    else
        ESP_LOGW(TAG, "BQ27220 profile setup failed and left SEC=%u (OperationStatus=0x%04x): security state "
                      "untouched, reporting SOC/current from the standard commands (read-only). "
                      "voltage=%u mV design_capacity=%u mAh",
                 probe.security, probe.operation_status, probe.voltage_mv, probe.design_capacity_mah);
    return ESP_OK;
}

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
    if (h->gauge) {
        esp_err_t ret = bq27220_seal(h->gauge);
        if (ret != ESP_OK) {
            bq27220_delete(h->gauge);
            free(h);
            return ret;
        }
    }
    h->checked_io = i2c_bus_device_create(bus, GAUGE_ADDR, 400000);
    if (!h->checked_io) {
        if (h->gauge)
            bq27220_delete(h->gauge);
        free(h);
        return ESP_FAIL;
    }
    if (!h->gauge) {
        esp_err_t ret = attach_read_only(h->checked_io);
        if (ret != ESP_OK) {
            i2c_bus_device_delete(&h->checked_io);
            free(h);
            return ret;
        }
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
    esp_err_t ret = i2c_bus_read_bytes(h->checked_io, GAUGE_REG_SOC, 2, soc);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = i2c_bus_read_bytes(h->checked_io, GAUGE_REG_CURRENT, 2, current);
    if (ret != ESP_OK) {
        return ret;
    }
    return claw_petti_battery_decode(soc, current, percent, charging) ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}
esp_err_t claw_petti_battery_delete(claw_petti_battery_handle_t h)
{
    if (!h)
        return ESP_OK;
    esp_err_t ret = h->gauge ? bq27220_delete(h->gauge) : ESP_OK;
    esp_err_t io = i2c_bus_device_delete(&h->checked_io);
    free(h);
    return ret == ESP_OK ? io : ret;
}
