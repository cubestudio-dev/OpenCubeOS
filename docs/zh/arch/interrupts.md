<!-- SPDX-License-Identifier: Apache-2.0 -->
# 中断与异常

## 表格

`kernel/arch/x86_64/arch_idt.{c,h}`：一个包含 256 个中断门的中断描述符表（IDT）；一个新的包含 TSS 的全局描述符表（GDT）；IST1 = 恐慌栈（#DF/#MC 有自己的栈 - IST2 永远不会发生上下文切换，IST2 是专用的异常退出栈，BUG-0139）；IST2 = 调试栈。

## PIC 和 LAPIC

`arch_irq.c` 将 8259 重新映射到向量 32-47（IRQ0-15）。本地 APIC 的实现在 `arch_lapic.c` 中。IRQ 线路可通过 `irqabitest` 在客户机内可见。

## 异常路径

存根（`arch_idt_stub.S`）构建一个帧，并通过向量进行分发：缺页异常增长（栈/堆）、系统调用入口（int 0x80 系列 -> `core_syscall.c`）、设备 IRQ，以及任何未处理情况的崩溃路径：寄存器转储到串口，崩溃日志写入 /etc，然后停止。

启动时的自检会故意触发 #DE、#UD 和 #PF，并断言内核能够存活 - 每次启动日志上都有 `exception self-test: 3/3`，因此有缺陷的陷阱路径永远不会到达 Shell 提示符。

## 时间源

PIT（节拍）、HPET、ACPI PM 定时器、TSC、CMOS RTC - 每个都有一个命令（`timer`、`hpet`、`pmtimer`、`tsc`、`rtc`）以及一份在 [../drivers/](../drivers/) 下的点亮手册。
