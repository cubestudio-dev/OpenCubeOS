<!-- SPDX-License-Identifier: Apache-2.0 -->
# 中断与异常：IDT、TSS、IST、LAPIC

一切中断形态的东西集中在一处：描述符表、向量映射、带 IST 纪律的
异常分派路径，以及 LAPIC/MSI 层。文件：
`ocos/kernel/arch/x86_64/arch_idt.{c,h}`、`arch_idt_stub.S`、
`arch_idt_load.S`、`arch_exceptions.{c,h}`、`arch_irq.{c,h}`、
`arch_pic.h`、`arch_lapic.{c,h}`、`arch_exc_exit.S`、
`arch_abi_probe.S`。

## GDT / TSS

一张 64 字节 GDT（`arch_idt.c` 中的 `arch_gdt_init`）：

```
 0 = null          (sel 0x00)
 1 = code32        (sel 0x08)  P|ring0|code|RX, 4K|32-bit
 2 = data          (sel 0x10)  P|ring0|data|RW
 3 = code64        (sel 0x18)  P|ring0|code|RX, 4K|64-bit (L=1)
 4 = TSS (16-byte) (sel 0x20)  P|ring0|system|TSS64-available (0x89)
 6 = user code64   (sel 0x30, pushed as 0x33 with RPL 3)
 7 = user data     (sel 0x38, pushed as 0x3B with RPL 3)
```

TSS（`arch_tss_t`，packed）携带：

- **rsp0**——每次任务切换时更新（ring 3 中断入口栈；每个任务的
  内核栈顶存于 `task_t.rsp0`）。
- **IST1 = 8 KiB `g_panic_stack`**——由向量 8（#DF）与 18（#MC）
  使用。
- **IST2 = 8 KiB `g_debug_stack`**——由向量 1（#DB）、2（NMI）、
  3（#BP）使用。
- **iopb_offset 位于 0x66**（按 SDM 表 8-2），TSS 限界内是 32 字节
  全 0xFF 的 IOPB -> **ring 3 对每个端口 [0, 256) 都被拒绝**
  （PRIV_TEST 根因修复：用户态的 `in`/`out` 现在触发异常而非静默
  生效）。

`arch_load_gdt_idt_tr`（`arch_idt_load.S`）：lgdt + lidt + 段重载 +
远 `retfq` CS 重载 + `ltr 0x20`。

## IDT

来自 `arch_isr_table` 桩数组的 256 个门：选择子 0x18，标志
**0x8E**（P | ring0 | 中断门）——唯有向量 0x80（系统调用）与
向量 3（#BP）取 **0xEE（DPL=3）**，使 ring 3 能 `int 0x80` /
`int3`。向量 8 与 18 用 IST1；向量 1、2、3 用 IST2。`arch_idt_load`
还设置 EFER.NXE（MSR 0xC0000080 位 11）。

桩（`arch_idt_stub.S`，NASM 宏 x 256）：带 CPU 错误码的向量（8、
10、11、12、13、14、17、30）跳过伪错误码压栈；公共路径压 15 个
GPR，执行 `cld`（BUG-0138——ABI 要求 C 入口处 DF=0），在
`call arch_isr_dispatch` 前后把栈动态对齐到 16 字节（BUG-0137），
最后 `iretq`。

## 向量映射

| 向量 | 用途 | EOI |
|---|---|---|
| 0..31 | CPU 异常 -> `arch_exc_dispatch` | - |
| 32..47（0x20..0x2F） | 8259 PIC IRQ0..15 -> `arch_irq_dispatch` | 提前 EOI |
| 48..111（0x30..0x6F） | MSI/MSI-X -> `arch_msi_dispatch` | LAPIC EOI |
| 128（0x80） | 系统调用（DPL=3）-> `core_syscall_dispatch` | - |
| 0xFF | LAPIC 伪中断（计数后丢弃） | - |

两片 8259 被重映射，使 IRQ0-15 落在向量 32-47。LAPIC 以 xAPIC
方式启用（MMIO 位于 0xFEE00000），带 EOI、自 IPI 与 MSI/MSI-X
向量分派 0x30..0x6F（每向量 4 个处理器槽位）；`msitest` 布防真实
的 e1000 MSI 并统计 LAPIC 向量命中（见
[driver-model.md](driver-model.md)）。PIC 与 MSI 两条路径上的
**提前 EOI 策略**之所以存在，是为了让处理器内的上下文切换绝不
会卡死中断控制器。

异常名（`arch_exc_names[32]`）：`"#DE Divide Error"`、`"#DB Debug"`、`"NMI Non-Maskable Interrupt"`、`"#BP Breakpoint"`、
`"#OF Overflow"`、`"#BR Bound Range Exceeded"`、`"#UD Invalid Opcode"`、`"#NM Device Not Available"`、`"#DF Double Fault"`、
`"#TS Invalid TSS"`、`"#NP Segment Not Present"`、`"#SS Stack-Segment Fault"`、`"#GP General Protection"`、`"#PF Page Fault"`、…… `"#MF x87 FPU"`、`"#AC Alignment Check"`、`"#MC Machine Check"`、`"#XF SIMD FP"`、`"#SX Security Exception"`。

## 注册接口

- 异常：`arch_exc_register_handler(vector, handler)`——每向量最多
  4 个处理器（`OC_EXC_CHAIN_LEN 4`），最近注册者最先运行；返回 1
  的处理器认领该异常，执行随之恢复。
- IRQ：`arch_irq_register_handler(irq 0..15, fn, ctx)`——每 IRQ
  4 个槽位，自动解除 PIC 屏蔽；注销时压缩，空则重新屏蔽。
- MSI：`arch_lapic_register_vector(0x30..0x6F, fn, ctx)`。
- 缺页：`mem_vmm_register_fault_handler(fn)`——最多 4 个
  （见 [memory.md](memory.md)）。
- 按向量计数器（`arch_irq_counts[256]` / `arch_exc_counts[256]`）
  支撑 `exc` 与 `irqabitest` 诊断。

## 异常分派（`arch_exc_dispatch`）

1. **嵌套 #PF 守卫**：在处理 #PF 期间再次触发的 #PF 仅经串口打印
   `*** NESTED #PF INSIDE EXCEPTION HANDLER - HALTING ***` 并
   cli;hlt 永久停机。
2. L1 处理器链（按注册的逆序）。
3. #PF -> `mem_vmm_handle_page_fault`（栈增长等）；若已处理则恢复
   执行。
4. 串口转储：`*** EXCEPTION <v>: <name> ***`，附 error_code/rip/
   cs/rflags/rsp/ss（#PF 另加 cr2）。
5. **ring 3 来源**（`cs & 3 == 3`）：尝试投递 SIGSEGV（有处理器 +
   不在处理器内 + 已投递 < 3 次；恢复器跳板 `mov eax,42; int 0x80; hlt` 写在栈上 stack_base+0x400 处，BUG-0277）；失败或耗尽时：
   crashlog 记录 + `*** KILLING user process (pid=N) due to exception V, kernel continues ***`、统一收割、唤醒父进程、
   TASK_EXITED、yield。**向量 1/2/3 的击杀在专用退出栈上运行**——
   见下文 IST 纪律。
6. **ring 0 来源** -> 带栈溢出归因的 panic：CR2 落在任务守卫页内的
   `#PF` -> `"KERNEL STACK OVERFLOW (guard page hit, task=<name>)"`；
   rsp 位于任务栈底或之下、或为不可信垃圾值（>= 0x40000000000）的
   `#DF` -> `"KERNEL STACK OVERFLOW (stack exhausted -> #DF, task=<name>)"`；否则 `"ring-0 exception <v> (<name>)"`。->
   `kernel_panic_frame(reason, f)`（见
   [crash-recovery.md](crash-recovery.md)）。

## IST 纪律（你看不见的那部分）

IST1/IST2 给了 #DF/#MC/#DB/NMI/#BP 别的东西碰不到的栈——但这
带来一条约束：调度器绝不能把 rsp 保存在 IST2 *之内*。在向量 1/2/3
上执行的 ring 3 击杀恰恰会那样做（从 IST2 帧里 reap + yield）。
修复（BUG-0139，`arch_exc_exit.S`）：
`arch_exc_run_on_exit_stack()` 在 reap+yield *之前*切换到专用
8 KiB 退出栈，因此 IST2 绝不会被挂起或恢复。同族纪律：`#DF` panic
路径完全在分派器深度运行，只用一个静态 192 字节串口行缓冲——#DF
无记录、无转储、无刷新（已在 KNOWN_ISSUES 中声明），因为按定义
机器已经在其最后一张栈上了。

## ABI 探针（`arch_abi_probe.S`）

`irqabitest` 布防 `g_arch_irq_entry_probe`——`arch_isr_dispatch`
的第一个动作即跳到该探针，验证 C 分派入口处 rsp 为 8 mod 16（计入
被压入的调用返回地址后的 SysV 对齐规则）且 DF 为 0，累积结果位并
自愈 DF。这是对上文 BUG-0137/0138 对齐/cld 修复的回归测试——桩
与 C 世界之间的 ABI 契约是*被测试的*，不是被假设的。

## 定时器管道

PIT（8253/8254 ch0 @ 100 Hz，除数 11932，模式 2，端口 0x40/0x43）
引发 IRQ0；`core_timer.c` 递增 tick/毫秒计数器并运行 16 槽位软
定时器表（周期 + 单次，回调在 IRQ 上下文中）。节拍消费者：调度器
时间片、网络栈的 10 毫秒轮询、health 采样器的 1 秒、看门狗检查、
引导状态心跳（见 [scheduler.md](scheduler.md)、
[network-stack.md](network-stack.md)）。HPET 中断（受测时）走
**legacy 路由 timer1 -> IRQ8**，因为内核只驱动两片 PIC——timer0
会与 PIT 在 IRQ0 上相撞，故从不启用。RTC 的 UIE/PIE/AIE 也经链表
共享 IRQ8。

## S3 挂起往返

`arch_acpi_s3.S` 把 RSP/CR3/CR4/CR0 保存进固定的低内存保存区
（偏移 0x200/0x208/0x20C/0x210）并写 PM1a_CNT 的
SLP_TYP|SLP_EN。`arch_acpi_s3_resume.S` 是 16/32 位恢复跳板：恢复
控制寄存器、重入长模式、恢复 RSP、返回 C 调用者。固件复位之后：
`arch_irq_rearm`（PIC）、`core_timer_resume`（PIT）、
`driver_pci_restore_commands`（每设备恢复 IO+MEM+BUS_MASTER）以及
e1000e 寄存器重放——见 [driver-model.md](driver-model.md)。
