; SPDX-License-Identifier: Apache-2.0
; Copyright 2026 cubestudio-dev <cubestudio@qq.com>
; Open Cube OS - WP-10-chinese_support-fix2
; File: kernel/arch/x86_64/arch_acpi_s3_resume.S
; Purpose: S3 (suspend to RAM) resume trampoline.
;
; WP-10-chinese_support-fix2 root-cause fix: the firmware (SeaBIOS and
; real BIOSes alike - SeaBIOS src/fw/resume.c s3_resume() uses
; farcall16big) invokes the FACS waking vector in 16-bit BIG REAL
; MODE, not 32-bit protected mode.  The previous hand-encoded 32-bit
; trampoline therefore crashed the machine on resume (every byte of it
; decoded as garbage 16-bit code and the fault loop ran with the BIOS
; real-mode IDT).  This trampoline starts in 16-bit real mode exactly
; like the Linux real-mode wakeup head, transitions to 32-bit
; protected mode with paging off, then performs the long-mode entry
; sequence and jumps back into the kernel.
;
; The trampoline is POSITION-FIXED: the C staging code copies it to
; physical address OC_S3_TRAMP_BASE (0x8000, one free low-RAM page
; between the BDA and the EBDA) and all memory references below are
; absolute immediates for that base - no relocations, safe to copy.
;
; Save area (inside the same page, filled by acpi_s3_fire_stub and the
; staging code; offsets must match driver_acpi_power.c):
;   0x200 saved RSP (u64)   0x208 CR3   0x20C CR4   0x210 CR0
;   0x214 GDT descriptor (u16 limit + u32 base)
;   0x21C IDT descriptor (u16 limit + u32 base)
;   0x228 u32 "resumed" magic (written by the 64-bit continuation)

%define TRAMP 0x8000

SECTION .rodata.s3tramp progbits alloc noexec nowrite align=16

GLOBAL acpi_s3_resume_tramp_start
GLOBAL acpi_s3_resume_tramp_end

acpi_s3_resume_tramp_start:
; ---------------------------------------------------------------- 16-bit
; Entry: big real mode, CS:IP = TRAMP:0, all other registers firmware
; owned.  Interrupts are off (the firmware wake path runs with them
; disabled).
BITS 16
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00              ; scratch stack (free post-boot RAM)

    ; load the saved kernel GDT (m16&32 form -> needs the o32 prefix)
    o32 lgdt [dword TRAMP+0x214]

    ; protected mode on, paging still off
    mov eax, cr0
    or eax, 1                   ; CR0.PE
    mov cr0, eax

    ; far jump into the kernel's 32-bit code segment (selector 0x08).
    ; Hand-encoded o32 EA because NASM cannot express ptr16:32 far
    ; jumps portably under BITS 16.
    db 0x66
    db 0xEA
    dd tramp_pm32
    dw 0x08

; ---------------------------------------------------------------- 32-bit
BITS 32
tramp_pm32:
    mov ax, 0x10                ; kernel data
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax

    ; restore the pre-sleep control registers: paging tables first
    ; (CR3), then CR4 (PAE), then EFER.LME|NXE, then CR0.PG, which
    ; activates long mode.  Mirrors the WP-10-input_chipset sequence.
    mov esp, [TRAMP+0x200]      ; low half; upper zeroed (stack < 4 GiB)
    mov eax, [TRAMP+0x208]      ; CR3
    mov cr3, eax
    mov eax, [TRAMP+0x20C]      ; CR4 (PAE)
    mov cr4, eax
    mov ecx, 0xC0000080         ; EFER
    rdmsr
    or eax, 0x900               ; LME | NXE
    wrmsr
    mov eax, [TRAMP+0x210]      ; CR0 (PG set -> long mode active)
    or eax, 0x80000000
    mov cr0, eax

    ; far jump into the kernel's 64-bit code segment (selector 0x18);
    ; the kernel GDT is already loaded from the 16-bit head.
    jmp 0x18:tramp_pm64

; ---------------------------------------------------------------- 64-bit
BITS 64
tramp_pm64:
    mov ax, 0x10                ; kernel data
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax

    ; The platform reset left the TSS descriptor's BUSY bit set (the
    ; descriptor lives in the RAM-resident GDT and was busy when the
    ; machine slept).  LTR on a busy TSS descriptor raises #TS, so
    ; clear the busy bit in the descriptor first (type 0x8B -> 0x89).
    mov eax, [TRAMP+0x216]      ; saved GDT base (< 4 GiB)
    and byte [rax+0x20+5], 0xFD ; clear TSS-descriptor busy bit (0x02)
    mov ax, 0x20                ; TSS selector (kernel GDT layout)
    ltr ax

    ; restore the kernel IDT
    lidt [TRAMP+0x21C]

    ; tell the C caller the resume path ran
    mov dword [TRAMP+0x228], 0x52334B4F   ; "R3KO" (TRAMP_RESUME_MAGIC)

    ; restore the saved RSP, pop the six callee-saved registers the
    ; fire stub pushed, and RET into driver_acpi_power.c exactly as if
    ; the sleep write had returned.
    mov rsp, [TRAMP+0x200]      ; full saved u64 RSP
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    ret

    ; 0x00-0xFF of the page is trampoline code; the save area starts at
    ; 0x200.  Pad the code section out to 0x100 so the two can never
    ; collide even if this file grows.
    times (0x100 - ($ - acpi_s3_resume_tramp_start)) db 0

acpi_s3_resume_tramp_end:
