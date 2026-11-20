<!-- SPDX-License-Identifier: Apache-2.0 -->
# 发布工程

| 页面 | 内容概览 |
|------|----------------|
| [构建系统全量参考](build-reference.md) | 每个 Makefile 目标、变量、宏与 .mk 包含 |
| [架构守护脚本](guard-scripts.md) | check_deps / check_bss / 注册表门禁等 |
| [CI 门禁与发布链](ci-gates.md) | CI 步骤、硬断言与 12 点发布链 |
| [工具清单](tool-inventory.md) | tools/ 全部 117 个文件，每个一行 |
| [流程](process.md) | 发布 SOP，门禁一个不少 |
| [版本管理](versioning.md) | 阶段名、构建号、承诺 |
| [发布说明模板](release-notes-template.md) | 强制的说明格式 |
| [网站更新](website-update.md) | update_website.sh 的机制 |
| [wiki 同步](wiki-sync.md) | sync_wiki.sh 的机制 |
| [wp04-l1-migration](wp04-l1-migration.md) | 每应用一条命令的迁移记录 |

## 发布说明

- [3.0.0-WP-tui-input-encoding-hardening](3.0.0-WP-tui-input-encoding-hardening.md) - TUI 输入与编码加固：
  OCIRE 空格修复、跨写入 UTF-8 TTY、串口方向裁定、VMware 三重
  故障修复、TUI 入口隐藏、三链路扩展排查
- [3.0.0-WP08-driver-framework](3.0.0-WP08-driver-framework.md) - 驱动框架：元数据、设备树、电源钩子、热插拔事件、失败隔离、
  确定性顺序与 drv/dev 诊断命令
- [3.0.0-WP-l1apps-merge](3.0.0-WP-l1apps-merge.md)——8 个合并
  大应用 + 文档重写
- [3.0.0-WP06g3](3.0.0-WP06g3.md)——g3 时代
- [3.0.0-WP06g2](3.0.0-WP06g2.md)——g2 时代
