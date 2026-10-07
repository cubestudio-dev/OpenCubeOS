; SPDX-License-Identifier: Apache-2.0
; Copyright 2026 cubestudio-dev <cubestudio@qq.com>
; Open Cube OS - WP-10-AUDIT_P2-fix1
; File: kernel/arch/x86_64/arch_abi_probe.S
; Purpose: IRQ-entry ABI probe (regression coverage for BUG-0137 / BUG-0138).
;
; The shell `irqabitest` command hooks this via g_arch_irq_entry_probe; the
; hook runs as the FIRST thing in arch_isr_dispatch, i.e. exactly at the C
; entry of the interrupt dispatch chain, on every vector taken while armed.
; It verifies the two SysV ABI invariants that must hold there:
;   1. rsp ≡ 8 (mod 16) on entry to a C function (=> rsp ≡ 0 at the stub's
;      `call arch_isr_dispatch`)                    - BUG-0137 stack align.
;   2. DF == 0 (interrupt/trap gates do not clear DF; the stub must `cld`)
;                                                    - BUG-0138.
;
; Result bits accumulate in g_arch_irq_probe_result:
;   bit 0 = entry alignment OK (rsp ≡ 8 mod 16)
;   bit 1 = DF clear OK
;   0x3 = both invariants hold. The probe self-heals DF before returning so
;   a failure is recorded but cannot poison the rest of the kernel run.

bits 64
default rel

global arch_abi_probe_entry
extern g_arch_irq_probe_result

section .text
align 16
arch_abi_probe_entry:
    xor eax, eax
    test rsp, 15
    jz .dfcheck             ; entry rsp ≡ 0 -> caller violated SysV (BUG-0137)
    or eax, 1
.dfcheck:
    pushfq
    pop rcx
    test rcx, 0x400         ; EFLAGS.DF
    jnz .store              ; DF set -> BUG-0138 regression (bit 1 stays 0)
    or eax, 2
.store:
    cld                     ; self-heal: never leave DF=1 running in the kernel
    lea rcx, [rel g_arch_irq_probe_result]
    or [rcx], rax           ; accumulate bits across every probed vector
    ret
