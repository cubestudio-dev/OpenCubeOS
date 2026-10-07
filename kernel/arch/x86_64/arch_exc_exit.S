; SPDX-License-Identifier: Apache-2.0
; Copyright 2026 cubestudio-dev <cubestudio@qq.com>
; Open Cube OS - WP-10-AUDIT_P2-fix1
; File: kernel/arch/x86_64/arch_exc_exit.S
; Purpose: BUG-0139 fix (A1-6) - dedicated exit stack for kills taken on
;          IST2-backed exceptions.
;
; Vectors 1/2/3 (#DB/NMI/#BP) run on the single global IST2 stack
; (g_debug_stack, wired in the TSS). Killing a ring-3 offender used to mean
; marking the task EXITED and calling core_sched_yield() *while rsp sat in
; the middle of IST2*; the scheduler then stored that IST2 address as the
; task's saved stack, and the NEXT IST2 exception reloaded the TSS IST2 top
; and overwrote the suspended frame. The fix: switch to a dedicated exit
; stack before the reap+yield, so the scheduler saves an address on this
; private stack and the IST2 region is abandoned (never resumed, never
; reused by this task again).

bits 64
default rel

global g_exc_exit_stack
global arch_exc_run_on_exit_stack
extern arch_exc_kill_and_yield

section .bss
align 16
g_exc_exit_stack:
    resb 8192
g_exc_exit_stack_top:

section .text
align 16
; arch_exc_run_on_exit_stack(rdi = user_proc_t *proc, rsi = u64 vector)
;   Never returns: switches rsp to the exit stack and calls
;   arch_exc_kill_and_yield(proc, vector) there.
arch_exc_run_on_exit_stack:
    lea rsp, [rel g_exc_exit_stack_top]
    and rsp, -16
    call arch_exc_kill_and_yield
.halt:
    hlt
    jmp .halt
