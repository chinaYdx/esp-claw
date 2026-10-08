# U11 fork 交付

app_claw 接入同级 petti-ui；Lua 和 esp-claw 显示层不参加 Petti 构建，路由器显式拒绝 run_script，调度器仍独立编译。修改点与原因见 docs/petti-integration.md。硬件仍由 U9 的 petti_drivers 提供给 petti_platform，NAND/Wi-Fi/TLS 所有权未移交给 AI。

主机仅运行替身，不运行 esp-claw。随附日志证明 TLS 后端替身 100 轮清理、CA/主机名失败保护，NAND 目录替身 100 次成功/失败与一次挂载、不格式化，AI 队列关联/复制/取消，三个关键保护绿→红→绿，以及私有 NVS seed 的离线模拟。设备实际全部 retained capabilities 用 IDF 6.2/S31 编译并由宿主链接；完整 Flash/RAM、依赖锁、ELF 审计、原始构建输出与固件 SHA 在 petti-ui/evidence/U11/delivery-report.md 及其 build-metrics.json。

fork 仅此 U11 提交，未改 Adapt/MicroPython/IDF，未触碰开发板或推送。真实 Wi-Fi/LLM/屏幕/NAND 验收留待开发板窗口；无本单待负责人决定事项。随后 U12 只修改宿主，处理 1200、开发模式 REPL 与 U8 实板发现的栈问题。
