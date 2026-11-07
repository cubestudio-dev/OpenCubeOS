<!-- SPDX-License-Identifier: Apache-2.0 -->
# 内部机制：内核日志（klog）

`kernel/lib/lib_klog.{c,h}` - 29 个声明；编号的第 6 个：

```
klog_write(level, fmt, ...)      /* the entry point */
klog_set_level(lvl)
klog_set_level_enabled(lvl, on)
klog_flush()
klog_rotate()
klog_filter_set(min_level)
```

## 行为（已通过端到端验证，非目标性描述）

- 级别 DEBUG/INFO/WARN/ERROR/FATAL；支持按级别启用 + 全局最低过滤器。
- 环形缓冲区镜像到串口；持久化文件（/etc 上的 `kernel.log`）按需刷新。
- 在 4096 字节阈值处轮转：`kernel.log` 变为 `kernel.log.1`（真正的代际对 - S5 断言两个大小）。
- 过滤器证明：级别 >= 3 仅允许 ERR/FATAL，而 dbg/inf/wrn 计数归零（S6 的确切断言）。

## 命令

`dmesg`（环形缓冲区）、`logcat`（实时跟踪）、`logctl`（级别/过滤器）、`crashlog`（崩溃记录）。

## 什么写入

所有子系统的错误路径 + 健康警报 + 看门狗策略终止 - 日志即是系统叙述；`logctl` 是你降低噪音而不丢失错误的方式。
