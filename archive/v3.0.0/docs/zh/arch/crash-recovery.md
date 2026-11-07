<!-- SPDX-License-Identifier: Apache-2.0 -->
# 崩溃恢复架构

## 崩溃路径

`kernel/core/core_crash.c`：未处理的异常会捕获帧（寄存器、向量、故障地址、CR2/CR3 上下文），将转储打印到串口，将崩溃日志记录写入 /etc 卷，然后停止。`dumpinfo` 在重启后读取该记录；`crashlog` 列出历史记录。

## 恐慌级隔离

- IST1 为 #DF/#MC 提供专用栈——即使故障处理程序中发生双重故障，仍有可执行代码的位置。
- 异常退出路径绝不会在 IST2 上进行上下文切换（BUG-0139）。
- 启动自检（捕获 #DE/#UD/#PF，内核存活）在 Shell 之前运行——在任何组件信任该机制之前，其可靠性已得到验证。

## 恢复姿态

系统本身就是自己的 WinRE：可从无磁盘的 ISO 启动，检查故障机器的卷，修复配置，重新安装启动代码（`install`/`abdisk`/`grub-install` 将其有效负载内置在内核中），从 /etc 提取日志和转储。带有救援表的恢复手册：[../manual/recovery.md](../manual/recovery.md)。

## 测试

`crashtest`（故意引发恐慌）、`dumpinfo`（读取回数据）、回归测试套件中的崩溃日志检查，以及 E2E 硬重置场景（S12/S13）通过硬件看门狗证明了重启部分的可靠性。
