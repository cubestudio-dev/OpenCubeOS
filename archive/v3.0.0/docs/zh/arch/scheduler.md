<!-- SPDX-License-Identifier: Apache-2.0 -->
# 进程调度

## 模型

`kernel/core/core_sched.{c,h}`：采用抢占式、基于优先级的调度，同一优先级内采用时间片轮转。任务控制块（TCB）保存状态、优先级、时间片以及保存的帧；上下文切换（`arch/x86_64/arch_context_switch.S`）通过了 ABI 探测验证（在调度调用处保持 SysV 16 字节栈对齐 - BUG-0137）。

## 节拍

PIT 以 100 Hz 的频率触发 IRQ0：节拍计数器、`g_boot_time_ms`、软定时器轮以及调度器的抢占均依赖于此。`sched`、`ps`、`nice`、`sleep`、`kill`、`spawn`、`multi` 是面向用户的命令。

## 后台任务与策略看门狗

长时间运行的工作（`g6_test &`、自旋任务）作为后台任务运行。看门狗子系统（`kernel/core/core_watchdog.c`）添加了监控槽位：必须为其槽位"喂狗"的任务若停止执行，则会被终止并记录原因，同时内核本身保持存活。硬件看门狗（TCO/WDAT）则保护整个机器：参见 [内部机制：看门狗子系统](../internals/watchdog.md)。

## 信号与进程

Ring-3 进程通过系统调用接口接收信号；用户测试程序（`run signal_test`、`run fork_test`、`run exec_test`、`run pipe_test`、`run select_test`）展示了完整的 Unix 风格进程模型：fork、exec、管道、select、mmap。
