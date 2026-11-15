<!-- SPDX-License-Identifier: Apache-2.0 -->
# 子系统内部机制

开发者手册（[../dev/index.md](../dev/index.md)）背后的深入
页面 —— 各子系统的机制、原文照录的结构体，以及
触发 -> 处理 -> 影响 契约。

| 页面 | 内容 |
|------|----------------|
| [klog](klog.md) | 1024 项环形缓冲区、5 个级别、4 条通道、发射钩子、/var/log/kernel.log + 1 MiB 轮转 x3、dmesg/logcat/logctl |
| [crash-dump](crash-dump.md) | panic 序列、OCDUMP01 格式、22 个 u64 寄存器快照、32 槽位 crashlog 环、IST 纪律、启动状态继电器 |
| [health](health.md) | 1 秒五维采样器、3 个去重告警位、告警到看门狗的馈送 |
| [watchdog](watchdog.md) | 15 秒软看门狗、8 个策略槽位、WDAT + Intel TCO、6300ESB 启动状态继电器、双布防规则 |
| [drbg-crypto](drbg-crypto.md) | HMAC_DRBG（SP 800-90A）播种、原语家族、内嵌根 CA 存储、cryptotest/dhtest |
| [ota-ab](ota-ab.md) | A/B 槽位布局、启动标志语义、16-op 表、安装/回滚/校验链 |
| [ocembed-guard](ocembed-guard.md) | OCIRE 嵌入：邮箱状态机、日志环、守卫进程、全盘写保护、注入的日志代理 |

`docs/dev/` 下的相关深入页面：
[data-structures.md](../dev/data-structures.md)（原文照录的
结构体集）、
[call-chains.md](../dev/call-chains.md)（六条路径解剖）、
[interface-dictionary.md](../dev/interface-dictionary.md)
（全部 208 个 L1SVC op）。架构总览：
[../root/ARCHITECTURE.md](../root/ARCHITECTURE.md)。
