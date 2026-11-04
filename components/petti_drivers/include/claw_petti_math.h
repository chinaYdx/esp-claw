// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdbool.h>
#include <stdint.h>
/* BMI270 configuration in petti_sensors.c: +/-4g, +/-2000 degrees/s. */
static inline float claw_petti_accel_g(int16_t raw) { return raw*(4.0f/32768.0f); }
static inline float claw_petti_gyro_dps(int16_t raw) { return raw*(2000.0f/32768.0f); }
/* Mosaico NAND QIO bootstrap allowlist. Unknown chips must not be written. */
static inline bool claw_petti_nand_qe_policy(uint8_t manufacturer,bool *needs_qe) {
    if(!needs_qe)return false;
    if(manufacturer==0xef||manufacturer==0x2c){*needs_qe=false;return true;}
    if(manufacturer==0x52||manufacturer==0xc8||manufacturer==0xba||manufacturer==0x0b||manufacturer==0xa1||manufacturer==0xc2){
        *needs_qe=true;return true;
    }
    return false;
}
/* BQ27220 registers are little endian; positive current means charging. */
static inline bool claw_petti_battery_decode(const uint8_t soc[2],const uint8_t current[2],unsigned *percent,bool *charging) {
    if(!soc||!current||!percent||!charging)return false;
    unsigned value=(unsigned)soc[0]|((unsigned)soc[1]<<8);
    if(value>100)return false;
    int16_t ma=(int16_t)((uint16_t)current[0]|((uint16_t)current[1]<<8));
    *percent=value;*charging=ma>0;return true;
}
/* Read-only BQ27220 probe, used when the gauge refuses the default unseal keys. The standard commands
 * (OperationStatus 0x3A, Voltage 0x08, DesignCapacity 0x3C) and Control(DEVICE_NUMBER) answer in SEALED
 * mode; data memory does not. security is OperationStatus[SEC]: 3 sealed, 2 unsealed, 1 full access,
 * 0 invalid (what a dead or garbled read yields). Voltage outside a single Li-ion cell window means no
 * battery is attached to the gauge. */
#define CLAW_PETTI_GAUGE_DEVICE_NUMBER 0x0220u
#define CLAW_PETTI_GAUGE_SEC_SEALED 3u
#define CLAW_PETTI_GAUGE_MIN_MV 2000u
#define CLAW_PETTI_GAUGE_MAX_MV 5000u
typedef struct {
    uint16_t operation_status, voltage_mv, design_capacity_mah;
    unsigned security;
} claw_petti_gauge_probe_t;
static inline bool claw_petti_gauge_probe_decode(const uint8_t device[2],const uint8_t status[2],const uint8_t voltage[2],
                                                 const uint8_t design[2],claw_petti_gauge_probe_t *out) {
    if(!device||!status||!voltage||!design||!out)return false;
    unsigned id=(unsigned)device[0]|((unsigned)device[1]<<8);
    unsigned op=(unsigned)status[0]|((unsigned)status[1]<<8);
    unsigned mv=(unsigned)voltage[0]|((unsigned)voltage[1]<<8);
    unsigned sec=(op>>1)&3u;
    if(id!=CLAW_PETTI_GAUGE_DEVICE_NUMBER||sec==0||mv<CLAW_PETTI_GAUGE_MIN_MV||mv>CLAW_PETTI_GAUGE_MAX_MV)return false;
    out->operation_status=(uint16_t)op;out->voltage_mv=(uint16_t)mv;
    out->design_capacity_mah=(uint16_t)((unsigned)design[0]|((unsigned)design[1]<<8));out->security=sec;
    return true;
}
