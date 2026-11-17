<!-- SPDX-License-Identifier: Apache-2.0 -->
# OCBL——Open Cube OS 引导加载器（组件百科）

原生引导加载器：一份共享核心、两种固件形态（BIOS 与 UEFI）、
启动状态监视器与恢复链的 A/B 槽机制。

| 页面 | 内容 |
|---|---|
| [架构与职责](ocbl-architecture.md) | 两种形态、移交契约、七类条目 |
| [双固件引导与 A/B 槽判定](ocbl-ab.md) | 双分区恢复盘、LBA 1024 标志扇区、0x98000/0x9B000 状态区、失败计数与清零条件、看门狗接力 |
| [故障排查与状态码](ocbl-troubleshooting.md) | 记录种类、接管菜单、/HANGAT 钩子、已声明的 UEFI 限制 |

英文版位于 [components/ocbl/](../../../components/ocbl/index.md)。
