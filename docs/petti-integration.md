# Petti fork 的 U11 改动

宿主工程为同级 petti-ui，平台拥有 NAND/Wi-Fi/TLS/LVGL。此 fork 不自行启动硬件或显示层；app_claw 被宿主作为组件引用，框架模块不复制。

- app_lua_modules.c 和 lua_module_builder 只在 APP_CLAW_CAP_LUA 开启时构建；system_ui 依赖依据自身 APP_CLAW_SYSTEM_UI_ENABLE。Petti 两者关闭，全部 Lua module/driver 未入链。
- event_router 在关闭 Lua 时，run_script 直接返回 ESP_ERR_NOT_SUPPORTED；scheduler 保留事件发布，不依赖 Lua。
- Petti 的三个 HTTP 客户端使用平台的 esp-tls wolfSSL global CA store。非 Petti 工程仍使用原 bundle 选项；TLS 库和统一配置由宿主提供，fork 没有另编 TLS。
- APP_CLAW_PETTI 打开无凭据/query 的 HTTPS URL 与 header 控制字符检查，保存前拒绝不合法设置。Key 状态仅 configured/missing。远端 HTTP 错误仅记录状态码，local IM 不记录完整出站文字。
- cap_boards 生成目录允许宿主指定到自己的 build，不再把 Petti 板元数据生成到 fork 源码目录。

保留/关闭的完整清单、设置预置、NAND 不格式化策略、TLS profile、待实板验收步骤见同级 petti-ui/docs/ai-integration.md；该宿主是可复跑的 S31 最终链接工程。Petti 配置在宿主 sdkconfig.defaults，默认通用 fork 的新开关 APP_CLAW_PETTI 关闭。
