# Petti C 硬件后端

此 fork 固定上游 `74b18700a1d6c40de472bfc71c19e49356ca1cc0`。Petti 改动放在 `petti` 分支。仓库和新增 `components/petti_drivers` 组件使用 Apache-2.0；源文件头保留出处。

Petti 从同级 ESP-IDF 工程编译此组件。不构建 edge_agent 应用、cap_lua、Lua 模块胶水、display_service 或 system_ui。AI 集成是另一工单。fork 的上游 Lua 模块仍然保留且未修改。

## 来源和适配

| 后端 | 来源 | 适配 |
|---|---|---|
| BMI270 | `lua_modules/lua_module_imu/src/bmi270/bmi270_backend.c` | C 实例借用 IDF I2C 总线和 Bosch SensorAPI，支持0x69；保留Petti 50Hz、±4g、±2000deg/s |
| BMM150 | `lua_modules/lua_module_magnetometer/src/bmm150/bmm150_backend.c` | 两个固定地址实例；上电/trim/forced采样；等待新DRDY，200ms超时；不扫描替代地址 |
| 音频 | `lua_modules/lua_module_audio/src/audio_device.c` | 去Lua后提取codec open/level/PCM流程；借用manager TX/RX；保留Mosaico ES8311参数和增益 |
| BQ27220 | 上游无后端；Mosaico BSP电池profile | 保留80mAh CEDV和seal；SOC/current带错误返回的读取，避免IO错误被当成零；默认unseal密钥被拒（网关仍SEALED）时不写profile、不改安全状态，改用只读标准命令继续读SOC/current并记一条WARN；网关无应答或电压不在2000–5000mV视为无电池 |
| SPI NAND | 上游无后端；Mosaico BSP NAND时序 | SIO读取manufacturer/QE后切QIO；拒绝未知厂商；无VFS和显式格式化/擦除 |

组件不会创建共享I2C/I2S总线。传感器、codec和gauge实例只拥有各自device/interface；NAND实例拥有独立SPI3的bootstrap/QIO总线。delete函数释放实例资源。Petti board manager拥有生命周期，应用启动/退出不能重建物理硬件。

## 集成和验证

公开C接口在 `components/petti_drivers/include/claw_petti_drivers.h`。可移植换算及NAND策略helper在 `claw_petti_math.h`；Petti原生测试执行同一helper。

板电气引脚、电源顺序、LED、马达PWM、按键、CO5300/CST9220工厂和版本识别放在 `petti-ui/boards/petti/petti_mosaico`，与可复用后端分开。数值来自保留的Mosaico BSP。Petti平台是物理IO的唯一使用方；LVGL仍由外壳拥有。

构建验证使用Petti的ESP-IDF 6.2 / ESP32-S31构建，四个后端源文件全部参与编译。`evidence/U9`包含构建日志、源码哈希、固件数字和测试。未连接或烧写开发板。物理验证留到U10。

NAND就绪不表示已经挂载文件系统。U9的 `petti_platform_nand_mount_path()` 返回NULL；U11不能假定已有DATA VFS根目录。
