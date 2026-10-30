; SPDX-License-Identifier: Apache-2.0
; Copyright 2026 cubestudio-dev <cubestudio@qq.com>
; Open Cube OS - 3.0.0-WP05-ocbl
; File: ocbl/bios/ocbl_bios_asm.S
; Purpose: 64-bit wrapper around the real-mode trampoline.
;
;          u32 ocbl_bios_int(u16 int_no, ocbl_bios_regs_t* r)
;          rdi = int_no, rsi = r
;
;          Callee-saved registers (rbx, rbp, r12-r15) are pushed on the
;          core stack - the trampoline round trip (and the BIOS itself)
;          may trash them.  The trampoline restores RSP and jumps to
;          the .resume label in this very frame.
;
;          Built into the core image; the trampoline blob itself is
;          ocbl/bios/tramp_data.asm (incbin) and copied to 0x40000 by
;          ocbl_tramp_init().

                bits 64
                default rel

                global ocbl_bios_int
                global ocbl_tramp_init
                extern ocbl_tramp_blob
                extern ocbl_tramp_blob_end

%define BLOCK   0x40300

section .text
ocbl_bios_int:
        ; copy the regs struct into the block (u32 general slots +
        ; u16 es/ds; see ocbl_bios.h for the mirrored offsets)
        mov     eax, [rsi+0]
        mov     [abs BLOCK+0x04], eax            ; B_EAX
        mov     eax, [rsi+4]
        mov     [abs BLOCK+0x08], eax            ; B_EBX
        mov     eax, [rsi+8]
        mov     [abs BLOCK+0x0C], eax            ; B_ECX
        mov     eax, [rsi+12]
        mov     [abs BLOCK+0x10], eax            ; B_EDX
        mov     eax, [rsi+16]
        mov     [abs BLOCK+0x14], eax            ; B_ESI
        mov     eax, [rsi+20]
        mov     [abs BLOCK+0x18], eax            ; B_EDI
        movzx   eax, word [rsi+24]
        mov     [abs BLOCK+0x1C], ax             ; B_ES
        movzx   eax, word [rsi+26]
        mov     [abs BLOCK+0x1E], ax             ; B_DS
        mov     [abs BLOCK+0x00], di             ; B_INT (int_no)
        ; preserve callee-saved regs + the struct pointer across the trip
        push    rbx
        push    rbp
        push    r12
        push    r13
        push    r14
        push    r15
        push    rsi                            ; volatile, needed for copy-back
        ; resume state: the trampoline restores exactly this RSP (top of
        ; the seven saved registers) and jumps to .resume, so the pops
        ; below line up without any re-computation.
        mov     [abs BLOCK+0x28], rsp              ; B_RSP
        lea     rax, [rel .resume]
        mov     [abs BLOCK+0x30], rax             ; B_RIP
        ; far jump into the trampoline 32-bit entry (selector 0x08 = code32)
        jmp     far [rel .fptr]
.resume:
        pop     rsi                            ; struct pointer back
        ; copy the results back into the C struct (caller reads them)
        mov     eax, [abs BLOCK+0x04]
        mov     [rsi+0], eax                   ; eax
        mov     eax, [abs BLOCK+0x08]
        mov     [rsi+4], eax                   ; ebx
        mov     eax, [abs BLOCK+0x0C]
        mov     [rsi+8], eax                   ; ecx
        mov     eax, [abs BLOCK+0x10]
        mov     [rsi+12], eax                  ; edx
        mov     eax, [abs BLOCK+0x14]
        mov     [rsi+16], eax                  ; esi
        mov     eax, [abs BLOCK+0x18]
        mov     [rsi+20], eax                  ; edi
        movzx   eax, word [abs BLOCK+0x1C]
        mov     [rsi+24], ax                   ; es
        movzx   eax, word [abs BLOCK+0x20]
        mov     [rsi+28], ax                   ; flags
        pop     r15
        pop     r14
        pop     r13
        pop     r12
        pop     rbp
        pop     rbx
        ; return the CF bit from the BIOS FLAGS
        movzx   eax, word [abs BLOCK+0x20]
        and     eax, 1
        ret

.fptr:
        dq 0x40000                ; TRAMP_ENTER32 (fixed by construction)
        dw 0x08                   ; code32 selector (core GDT layout)

section .text
ocbl_tramp_init:
        ; memcpy(0x40000, ocbl_tramp_blob, ocbl_tramp_blob_end - blob)
        mov     rdi, 0x40000
        lea     rsi, [rel ocbl_tramp_blob]
        lea     rcx, [rel ocbl_tramp_blob_end]
        sub     rcx, rsi
        cld
        rep movsb
        ret
