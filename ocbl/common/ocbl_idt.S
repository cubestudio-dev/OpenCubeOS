; SPDX-License-Identifier: Apache-2.0
; Copyright 2026 cubestudio-dev <cubestudio@qq.com>
; Open Cube OS - WP-tui-input-encoding-hardening 4.5
; File: ocbl/common/ocbl_idt.S
; Purpose: the minimal long-mode IDT stubs for the OCBL core.
;
;          WP-tui-input-encoding-hardening finding H1 (the VMware
;          triple-fault loop): the OCBL 64-bit core NEVER loaded an
;          IDT - the IDTR carried whatever the firmware left behind
;          (SeaBIOS happens to leave a working protected-mode table,
;          which masked the defect under QEMU; on the reporter's
;          VMware/Phoenix firmware the inherited state misdecodes
;          and any asynchronous event escalates #GP -> double fault
;          -> triple fault -> "Triple fault." + CPU reset, in a
;          ~1.3 s loop).  This module gives the core its OWN table:
;
;            vector 2 (NMI)  -> iretq (ignored - a bootloader does
;                               not act on parity/ECC NMIs)
;            vectors 0..31   -> exception stubs (push the vector,
;                               save, report on serial, halt)
;            vectors 32..255 -> plain iretq (masked IRQs / spurious)
;
;          The gate table itself is built in C (ocbl_idt.c) from
;          these stub addresses; the IDT lives in the core's .bss so
;          no fixed low-memory address is consumed (both flavors).
;
;          Known limitations (documented, accepted): no TSS/IST, so
;          a double fault with a corrupted RSP still escalates; the
;          trampoline's microsecond 32-bit-compat windows run with
;          IF=0 and the CMOS NMI mask held set, but an NMI landing
;          inside one of those windows still escalates (the windows
;          are too short to justify a second 8-byte-gate table).  The
;          real-mode window itself is properly arbitrated: ENTER32
;          lidt's the firmware IVT on the way down and BACK32 lidt's
;          this table back on the way up (tramp_init publishes the
;          facts at the fixed scratch pair 0x404F4/0x404F6).

                section .text
                global ocbl_idt_irq_stub
                global ocbl_idt_exc_common
                global ocbl_idt_exc_stub_table
                extern ocbl_idt_exception_report

; ---- the IRQ / NMI / spurious stub: interrupt gate, just return --
ocbl_idt_irq_stub:
                iretq

; ---- exception stubs: push the vector, common save+report -------
exc_stub_0:    push qword 0
                jmp ocbl_idt_exc_common
exc_stub_1:    push qword 1
                jmp ocbl_idt_exc_common
                ; vector 2 (NMI) uses the iretq stub above
exc_stub_3:    push qword 3
                jmp ocbl_idt_exc_common
exc_stub_4:    push qword 4
                jmp ocbl_idt_exc_common
exc_stub_5:    push qword 5
                jmp ocbl_idt_exc_common
exc_stub_6:    push qword 6
                jmp ocbl_idt_exc_common
exc_stub_7:    push qword 7
                jmp ocbl_idt_exc_common
exc_stub_8:    push qword 8
                jmp ocbl_idt_exc_common
exc_stub_9:    push qword 9
                jmp ocbl_idt_exc_common
exc_stub_10:   push qword 10
                jmp ocbl_idt_exc_common
exc_stub_11:   push qword 11
                jmp ocbl_idt_exc_common
exc_stub_12:   push qword 12
                jmp ocbl_idt_exc_common
exc_stub_13:   push qword 13
                jmp ocbl_idt_exc_common
exc_stub_14:   push qword 14
                jmp ocbl_idt_exc_common
exc_stub_15:   push qword 15
                jmp ocbl_idt_exc_common
exc_stub_16:   push qword 16
                jmp ocbl_idt_exc_common
exc_stub_17:   push qword 17
                jmp ocbl_idt_exc_common
exc_stub_18:   push qword 18
                jmp ocbl_idt_exc_common
exc_stub_19:   push qword 19
                jmp ocbl_idt_exc_common
exc_stub_20:   push qword 20
                jmp ocbl_idt_exc_common
exc_stub_21:   push qword 21
                jmp ocbl_idt_exc_common
exc_stub_22:   push qword 22
                jmp ocbl_idt_exc_common
exc_stub_23:   push qword 23
                jmp ocbl_idt_exc_common
exc_stub_24:   push qword 24
                jmp ocbl_idt_exc_common
exc_stub_25:   push qword 25
                jmp ocbl_idt_exc_common
exc_stub_26:   push qword 26
                jmp ocbl_idt_exc_common
exc_stub_27:   push qword 27
                jmp ocbl_idt_exc_common
exc_stub_28:   push qword 28
                jmp ocbl_idt_exc_common
exc_stub_29:   push qword 29
                jmp ocbl_idt_exc_common
exc_stub_30:   push qword 30
                jmp ocbl_idt_exc_common
exc_stub_31:   push qword 31
                jmp ocbl_idt_exc_common

; ---- common exception path: save, report, halt ------------------
; entry stack: [rsp] = vector (pushed), [rsp+8] = faulting RIP,
;              [rsp+16] = CS, [rsp+24] = RFLAGS
ocbl_idt_exc_common:
                push rax
                push rcx
                push rdx
                push rsi
                push rdi
                push r8
                push r9
                push r10
                push r11
                mov  rdi, [rsp+9*8]        ; the pushed vector
                mov  rsi, [rsp+9*8+8]      ; the faulting RIP
                call ocbl_idt_exception_report
                ; ocbl_idt_exception_report never returns: it prints
                ; the fault facts on serial and parks in cli;hlt - a
                ; diagnosable stop instead of a silent reset loop
.halt:
                cli
                hlt
                jmp  .halt

; ---- the stub address table (consumed by ocbl_idt.c) ------------
; vector 2 (NMI) is ABSENT on purpose: the C builder points it at
; the iretq stub together with vectors 32..255.
                section .rodata
ocbl_idt_exc_stub_table:
                dq exc_stub_0
                dq exc_stub_1
                dq 0                        ; vector 2: iretq stub
                dq exc_stub_3
                dq exc_stub_4
                dq exc_stub_5
                dq exc_stub_6
                dq exc_stub_7
                dq exc_stub_8
                dq exc_stub_9
                dq exc_stub_10
                dq exc_stub_11
                dq exc_stub_12
                dq exc_stub_13
                dq exc_stub_14
                dq exc_stub_15
                dq exc_stub_16
                dq exc_stub_17
                dq exc_stub_18
                dq exc_stub_19
                dq exc_stub_20
                dq exc_stub_21
                dq exc_stub_22
                dq exc_stub_23
                dq exc_stub_24
                dq exc_stub_25
                dq exc_stub_26
                dq exc_stub_27
                dq exc_stub_28
                dq exc_stub_29
                dq exc_stub_30
                dq exc_stub_31
