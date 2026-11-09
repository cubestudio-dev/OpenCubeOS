<!-- SPDX-License-Identifier: Apache-2.0 -->
# Interrupts and exceptions

## Tables

`kernel/arch/x86_64/arch_idt.{c,h}`: a 256-entry IDT with interrupt
gates; a new GDT with a TSS; IST1 = panic stack (#DF/#MC get their
own stack - never a context switch on IST2, which is the dedicated
exception-exit stack, BUG-0139); IST2 = debug stack.

## PIC and LAPIC

`arch_irq.c` remaps the 8259 to vectors 32-47 (IRQ0-15). The local
APIC handling lives in `arch_lapic.c`. The IRQ lines are visible
in-guest via `irqabitest`.

## Exception path

The stubs (`arch_idt_stub.S`) build a frame, dispatch by vector:
page-fault growth (stack/heap), syscall entry (int 0x80 family ->
`core_syscall.c`), device IRQs, and the crash path for anything
unhandled: register dump to serial, crashlog to /etc, halt.

The boot-time self-test fires #DE, #UD and #PF deliberately and
asserts the kernel survives - `exception self-test: 3/3` is on every
boot log, so a broken trap path never makes it to a shell prompt.

## Time sources

PIT (the tick), HPET, ACPI PM timer, TSC, CMOS RTC - each with a
command (`timer`, `hpet`, `pmtimer`, `tsc`, `rtc`) and a bring-up
manual page under [../drivers/](../drivers/).
