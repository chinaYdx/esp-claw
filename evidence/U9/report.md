# U9 Petti C 驱动后端

基线固定官方 esp-claw `74b18700a1d6c40de472bfc71c19e49356ca1cc0`，分支 petti；没有修改原 AI/Lua 子系统或只读 esp-claw-upstream。完整历史与对象核验见 `fork-integrity.log`、`fork-fsck.log`、`fork-complete-fetch-retry.log`。

本次只增加 `components/petti_drivers` 和配对说明 `docs/petti-drivers.en.md` / `.zh-CN.md`。BMI270/BMM150/ES8311 提取自上游 C 后端，改用借用 bus、opaque instance，允许 0x69 IMU 和两颗独立磁力计；BQ27220/NAND 是上游缺少的后端，参数取自 Petti 保留的 Mosaico BSP。源码保留来源、版权和 Apache-2.0 许可。

四个 C 后端通过同级 `petti-ui` 的实际 board manager 配置在 ESP-IDF 6.2 / esp32s31 构建；未构建无关的 edge_agent/Lua 应用，也未连接开发板。最终命令为在 `C:/code/esp32/petti-ui` 执行 `powershell -NoProfile -ExecutionPolicy Bypass -File tools/run-idf.ps1 build`，输出见 `idf-delivery-build.log`；源码与 object SHA 见 `source-artifacts.json`。

固件 2,111,728 字节，4MB 上限内余 2,082,576，SHA-256 `0761583abbdd4cb1bcea2ea49c708733a3c433932e6b7dcb3ac81411dfe2dcd8`。实际组件 manager 0.6.2，LVGL 9.5.0，adapter 0.6.2。ELF BSP/Lua 符号均 0；完整 UI 的主机回归记录在同级 petti-ui/evidence/U9。

`petti_driver_math_test.exe` 覆盖 65,536 原始传感器值、707 电量/电流组合、65,435 非法 SOC 和 256 NAND manufacturer IDs。`tools/test-hardware-policy.py` 对 IMU range、电量范围、零电流充电判断和未知 NAND 写 feature 四个 helper 进行绿→红→绿验证；四个板策略变异也全部检出。复制的 `policy-driver-mutations-final.log`、`mutation-*.log` 为实际输出，测试在 build 中编译副本，未污染工作源码。

物理行为等待 U10：特别是电源 ramp、全双工音频、forced/DRDY 磁力计、BQ27220 profile 与 NAND QE。NAND 不挂 VFS、无明确格式化/擦除调用，只保留驱动的 wear-leveling 初始化及 sync。映射和逐器件步骤见 Petti `docs/hardware-layer.md`。无待负责人决定事项，无 U9 范围偏差；没有修改 IDF 或安装系统软件。
