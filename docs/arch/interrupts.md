<!-- SPDX-License-Identifier: Apache-2.0 -->
# Interrupts and exceptions: IDT, TSS, IST, LAPIC

Everything interrupt-shaped in one place: the descriptor tables, the
vector map, the exception dispatch path with its IST discipline, and
the LAPIC/MSI layer. Files: `ocos/kernel/arch/x86_64/arch_idt.{c,h}`,
`arch_idt_stub.S`, `arch_idt_load.S`, `arch_exceptions.{c,h}`,
`arch_irq.{c,h}`, `arch_pic.h`, `arch_lapic.{c,h}`, `arch_exc_exit.S`,
`arch_abi_probe.S`.

## GDT / TSS

A 64-byte GDT (`arch_gdt_init` in `arch_idt.c`):

```
 0 = null          (sel 0x00)
 1 = code32        (sel 0x08)  P|ring0|code|RX, 4K|32-bit
 2 = data          (sel 0x10)  P|ring0|data|RW
 3 = code64        (sel 0x18)  P|ring0|code|RX, 4K|64-bit (L=1)
 4 = TSS (16-byte) (sel 0x20)  P|ring0|system|TSS64-available (0x89)
 6 = user code64   (sel 0x30, pushed as 0x33 with RPL 3)
 7 = user data     (sel 0x38, pushed as 0x3B with RPL 3)
```

The TSS (`arch_tss_t`, packed) carries:

- **rsp0** - updated on every task switch (the ring-3 interrupt entry
  stack; each task's kernel stack top lives in `task_t.rsp0`).
- **IST1 = 8 KiB `g_panic_stack`** - used by vectors 8 (#DF) and 18
  (#MC).
- **IST2 = 8 KiB `g_debug_stack`** - used by vectors 1 (#DB), 2 (NMI),
  3 (#BP).
- **iopb_offset at 0x66** per SDM table 8-2 with a 32-byte all-0xFF
  IOPB inside the TSS limit -> **every port [0, 256) is denied from
  ring 3** (the PRIV_TEST root-cause fix: `in`/`out` from user mode
  now faults instead of silently working).

`arch_load_gdt_idt_tr` (`arch_idt_load.S`): lgdt + lidt + segment
reloads + a far `retfq` CS reload + `ltr 0x20`.

## IDT

256 gates from the `arch_isr_table` stub array: selector 0x18, flags
**0x8E** (P | ring0 | interrupt gate) - except vector 0x80 (syscall)
and vector 3 (#BP) which get **0xEE (DPL=3)** so ring 3 can
`int 0x80` / `int3`. IST1 for vectors 8 and 18; IST2 for vectors 1, 2,
3. `arch_idt_load` also sets EFER.NXE (MSR 0xC0000080 bit 11).

The stubs (`arch_idt_stub.S`, NASM macro x 256): vectors with CPU
error codes (8, 10, 11, 12, 13, 14, 17, 30) skip the fake error-code
push; the common path pushes 15 GPRs, executes `cld` (BUG-0138 - ABI
requires DF=0 at C entry), dynamically aligns the stack to 16 bytes
around `call arch_isr_dispatch` (BUG-0137), and `iretq`s.

## Vector map

| Vectors | Use | EOI |
|---|---|---|
| 0..31 | CPU exceptions -> `arch_exc_dispatch` | - |
| 32..47 (0x20..0x2F) | 8259 PIC IRQ0..15 -> `arch_irq_dispatch` | early EOI |
| 48..111 (0x30..0x6F) | MSI/MSI-X -> `arch_msi_dispatch` | LAPIC EOI |
| 128 (0x80) | syscall (DPL=3) -> `core_syscall_dispatch` | - |
| 0xFF | LAPIC spurious (counted, dropped) | - |

The 8259s are remapped so IRQ0-15 land on vectors 32-47. The LAPIC is
enabled xAPIC-style (MMIO at 0xFEE00000) with EOI, self-IPI and
MSI/MSI-X vector dispatch 0x30..0x6F (4 handler slots per vector);
`msitest` arms a real e1000 MSI and counts LAPIC vector hits
([driver-model.md](driver-model.md)). The **early-EOI policy** on both
PIC and MSI paths exists so a context switch inside a handler can
never wedge the interrupt controller.

Exception names (`arch_exc_names[32]`): `"#DE Divide Error"`, `"#DB Debug"`, `"NMI Non-Maskable Interrupt"`, `"#BP Breakpoint"`, `"#OF Overflow"`, `"#BR Bound Range Exceeded"`, `"#UD Invalid Opcode"`,
`"#NM Device Not Available"`, `"#DF Double Fault"`, `"#TS Invalid TSS"`, `"#NP Segment Not Present"`, `"#SS Stack-Segment Fault"`,
`"#GP General Protection"`, `"#PF Page Fault"`, ... `"#MF x87 FPU"`,
`"#AC Alignment Check"`, `"#MC Machine Check"`, `"#XF SIMD FP"`,
`"#SX Security Exception"`.

## Registration interfaces

- Exceptions: `arch_exc_register_handler(vector, handler)` - up to 4
  handlers per vector (`OC_EXC_CHAIN_LEN 4`), most-recently-registered
  runs FIRST; a handler returning 1 claims the fault and execution
  resumes.
- IRQs: `arch_irq_register_handler(irq 0..15, fn, ctx)` - 4 slots per
  IRQ, auto-PIC-unmask; unregister compacts and re-masks when empty.
- MSI: `arch_lapic_register_vector(0x30..0x6F, fn, ctx)`.
- Page faults: `mem_vmm_register_fault_handler(fn)` - up to 4
  ([memory.md](memory.md)).
- Per-vector counters (`arch_irq_counts[256]` / `arch_exc_counts[256]`)
  back the `exc` and `irqabitest` diagnostics.

## Exception dispatch (`arch_exc_dispatch`)

1. **Nested-#PF guard**: a #PF raised while already handling one
   prints (serial only) `*** NESTED #PF INSIDE EXCEPTION HANDLER - HALTING ***` and cli;hlt forever.
2. The L1 handler chain (reverse registration order).
3. #PF -> `mem_vmm_handle_page_fault` (stack growth etc.); resume if
   handled.
4. Serial dump: `*** EXCEPTION <v>: <name> ***` with error_code/rip/
   cs/rflags/rsp/ss (+ cr2 for #PF).
5. **Ring-3 origin** (`cs & 3 == 3`): SIGSEGV delivery attempt
   (handler + not-in-handler + < 3 deliveries; the restorer
   trampoline `mov eax,42; int 0x80; hlt` is written on-stack at
   stack_base+0x400, BUG-0277); on failure or exhaustion: crashlog
   record + `*** KILLING user process (pid=N) due to exception V, kernel continues ***`, unified reaper, parent wake, TASK_EXITED,
   yield. **For vectors 1/2/3 the kill runs on the dedicated exit
   stack** - see IST discipline below.
6. **Ring-0 origin** -> panic with stack-overflow attribution:
   `#PF` with CR2 inside a task's guard page -> `"KERNEL STACK OVERFLOW (guard page hit, task=<name>)"`; `#DF` with rsp at/below
   the task stack bottom or implausible garbage (>= 0x40000000000) ->
   `"KERNEL STACK OVERFLOW (stack exhausted -> #DF, task=<name>)"`;
   otherwise `"ring-0 exception <v> (<name>)"`. ->
   `kernel_panic_frame(reason, f)` ([crash-recovery.md](crash-recovery.md)).

## IST discipline (the part you cannot see)

IST1/IST2 give #DF/#MC/#DB/NMI/#BP stacks that nothing else can touch
- but that creates a constraint: the scheduler must never save an rsp
*inside* IST2. A ring-3 kill taken on vector 1/2/3 would do exactly
that (reap + yield from the IST2 frame). The fix (BUG-0139,
`arch_exc_exit.S`): `arch_exc_run_on_exit_stack()` switches to a
dedicated 8 KiB exit stack *before* the reap+yield, so IST2 is never
suspended or resumed. Same family of discipline: the `#DF` panic path
runs entirely at dispatcher depth with one static 192-byte serial line
buffer - no record, no dump, no flush for #DF (stated in
KNOWN_ISSUES), because by definition the machine is already on its
last stack.

## The ABI probe (`arch_abi_probe.S`)

`irqabitest` arms `g_arch_irq_entry_probe` - the first action in
`arch_isr_dispatch` jumps to the probe, which verifies that at C
dispatch entry rsp is 8 mod 16 (the SysV alignment rule with the
call-return address pushed) and DF is 0, accumulating result bits and
self-healing DF. This is the regression test for the BUG-0137/0138
alignment/cld fixes above - the ABI contract between the stubs and the
C world is *tested*, not assumed.

## Timer plumbing

The PIT (8253/8254 ch0 @ 100 Hz, divisor 11932, mode 2, ports
0x40/0x43) raises IRQ0; `core_timer.c` bumps the tick/ms counters and
runs the 16-slot soft-timer table (periodic + one-shot, callbacks in
IRQ context). Tick consumers: the scheduler slice, the net stack's 10
ms poll, the health sampler's 1 s, the watchdog checks, the boot-state
heartbeat ([scheduler.md](scheduler.md),
[network-stack.md](network-stack.md)). HPET interrupts (when tested)
ride the **legacy route timer1 -> IRQ8** because the kernel drives only
the two PICs - timer0 would collide with the PIT on IRQ0 and is never
enabled. RTC UIE/PIE/AIE also share IRQ8 through the chain table.

## S3 suspend round-trip

`arch_acpi_s3.S` saves RSP/CR3/CR4/CR0 into a fixed low-memory save
area (offsets 0x200/0x208/0x20C/0x210) and writes PM1a_CNT
SLP_TYP|SLP_EN. `arch_acpi_s3_resume.S` is the 16/32-bit resume
trampoline: restores control regs, re-enters long mode, restores RSP,
returns into the C caller. After firmware reset: `arch_irq_rearm`
(PIC), `core_timer_resume` (PIT), `driver_pci_restore_commands`
(IO+MEM+BUS_MASTER on every device) and the e1000e register replay -
[driver-model.md](driver-model.md).
