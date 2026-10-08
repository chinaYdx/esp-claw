# Petti C hardware backends

This fork pins upstream `74b18700a1d6c40de472bfc71c19e49356ca1cc0`. Petti changes live on branch `petti`. The repository and the new `components/petti_drivers` component use Apache-2.0; source headers retain attribution.

Petti builds this component from its sibling ESP-IDF project. It does not build the edge_agent application, cap_lua, Lua module glue, display_service, or system_ui. AI integration is a separate ticket. Upstream Lua modules remain available and unchanged in this fork.

## Sources and adaptations

| Backend | Source | Adaptation |
|---|---|---|
| BMI270 | `lua_modules/lua_module_imu/src/bmi270/bmi270_backend.c` | C instance using a borrowed IDF I2C bus and Bosch SensorAPI, including address 0x69; preserve Petti 50Hz, ±4g, ±2000deg/s |
| BMM150 | `lua_modules/lua_module_magnetometer/src/bmm150/bmm150_backend.c` | Two fixed-address instances; power/trim/forced sampling; wait for fresh DRDY with a 200ms bound; no alternate-address scanning |
| Audio | `lua_modules/lua_module_audio/src/audio_device.c` | Extract codec open/level/PCM flow without Lua; borrow manager TX/RX; preserve Mosaico ES8311 parameters and gain |
| BQ27220 | No upstream backend; Mosaico BSP battery profile | Preserve 80mAh CEDV and seal; checked SOC/current reads avoid swallowed IO errors being reported as zero |
| SPI NAND | No upstream backend; Mosaico BSP NAND sequence | SIO manufacturer/QE bootstrap followed by QIO; unknown manufacturers rejected; no VFS or explicit formatting/erase |

The component never creates shared I2C/I2S buses. Sensor, codec and gauge instances own only their devices/interfaces; the NAND instance owns its dedicated SPI3 bootstrap/QIO bus. Delete functions release instance resources. Board manager in Petti owns the lifetime, and application start/stop must not recreate physical hardware.

## Integration and validation

Public C APIs are in `components/petti_drivers/include/claw_petti_drivers.h`. Portable conversion and NAND policy helpers are in `claw_petti_math.h`; Petti native tests exercise the same helpers.

Board electrical pins, power sequencing, LEDs, motor PWM, buttons, CO5300/CST9220 factories and revision detection belong to `petti-ui/boards/petti/petti_mosaico`, outside reusable backends. Values come from the retained Mosaico BSP. The Petti platform is the only consumer of physical IO; LVGL remains the shell's owner.

Build verification uses Petti's ESP-IDF 6.2 / ESP32-S31 build, compiling all four backend source files. `evidence/U9` contains the build transcript, source hashes, binary metrics and tests. No board was connected or flashed. Physical verification remains U10.

NAND readiness does not imply a mounted filesystem. Petti's `petti_platform_nand_mount_path()` returns NULL in U9; U11 must not assume a DATA VFS root exists.
