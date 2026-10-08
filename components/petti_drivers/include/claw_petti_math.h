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
