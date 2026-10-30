; SPDX-License-Identifier: Apache-2.0
; Copyright 2026 cubestudio-dev <cubestudio@qq.com>
; Open Cube OS - 3.0.0-WP05-ocbl
; File: ocbl/bios/tramp16.asm
; Purpose: the 64 <-> 16 real-mode BIOS trampoline for the BIOS flavor.
;
;          The 64-bit C core calls ocbl_bios_int() (ocbl_bios_asm.S);
;          this blob is copied to linear 0x40000 at init and does the
;          mode dance for every BIOS service call:
;
;            64-bit core
;              | far jmp 0x08:ENTER32      (32-bit compat, selector 0x08)
;            ENTER32: PG off, lgdt TRAMP GDT, far jmp 0x20:ENTER16
;            ENTER16: 16-bit protected, load 64K data segs, PE off,
;                     far jmp 0x4000:RM_BODY -> real mode
;            RM_BODY: stack, self-patched INT nn, run the BIOS call,
;                     save regs+flags
;              | reverse: PE on -> 32-bit, CR3/PAE/EFER/PG on,
;              | far jmp 0x18:BACK64 -> 64-bit, restore RSP, jmp RIP
;            64-bit core at the .resume label
;
;          The TRAMP GDT keeps the core selector layout (0x08 code32,
;          0x10 data32, 0x18 code64) and adds 0x20 code16 / 0x28 data16
;          for the real-mode transition, so the core never sees a
;          selector change.
;
;          Layout (org 0x40000, total footprint 0x10000):
;            0x40000..0x402FF  code + GDT + GDTR   (~0x210)
;            0x40300           request block (ocbl_tramp.h mirror)
;            0x40500           DAP area (16 B) for int 13h AH=42h
;            0x40600           AH=48h geometry scratch
;            0x40E00           32-bit + real-mode stack top (BIOS pushes
;                              reach down to ~0x40C00; DAP/scratch sit
;                              below the shadow - live-caught in B2a:
;                              reads kept returning zeros with the DAP
;                              at 0x40360)
;
;          Built as a FLAT BINARY (nasm -f bin, org 0x40000), embedded
;          into the core as data and memcpy'd to 0x40000 at init.

                bits 32
                org 0x40000


%define TR_BASE     0x40000
%define BLOCK_OFF   0x300          ; 0x40300

; request block field offsets (mirror ocbl_bios.h B_*).  The general
; registers are FULL 32-bit slots: E820 passes EDX='SMAP' and returns
; EAX='SM' - real mode happily runs 0x66-prefixed 32-bit moves.
%define B_INT       0x00
%define B_EAX       0x04
%define B_EBX       0x08
%define B_ECX       0x0C
%define B_EDX       0x10
%define B_ESI       0x14
%define B_EDI       0x18
%define B_ES        0x1C
%define B_DS        0x1E
%define B_FLG       0x20
%define B_RSP       0x28
%define B_RIP       0x30

TRAMP_ENTER32:
        ; ---- from 64-bit compat landing: flat 32-bit, paging ON

        mov     ax, 0x10
        mov     ds, ax
        mov     es, ax
        mov     ss, ax
        mov     esp, 0x40E00
        ; ---- paging off
        mov     eax, cr0
        and     eax, 0x7FFFFFFF
        mov     cr0, eax

        ; ---- our GDT (adds the 16-bit descriptors)
        lgdt    [tramp_gdtr]
        jmp     0x20:(ENTER16 - TR_BASE)

        bits 16
ENTER16:
        ; ---- 16-bit protected; load 64K data segments before PE off

        mov     ax, 0x28
        mov     ds, ax
        mov     es, ax
        mov     fs, ax
        mov     gs, ax
        mov     ss, ax
        mov     eax, cr0
        and     al, 0xFE
        mov     cr0, eax
        jmp     0x4000:(RM_BODY - TR_BASE)

RM_BODY:
        ; ---- real mode: own stack + block access at DS:0x200

        mov     ax, 0x4000
        mov     ds, ax
        mov     ss, ax
        mov     sp, 0x0F00
        ; self-patch the INT opcode from the request block
        mov     ax, [BLOCK_OFF + B_INT]
        mov     byte [rm_int_patch - TR_BASE + 1], al
        ; load caller regs (full 32-bit general slots, 0x66-prefixed
        ; 32-bit moves are legal in real mode; E820's EDX='SMAP' works)
        mov     eax, [BLOCK_OFF + B_EAX]
        mov     ebx, [BLOCK_OFF + B_EBX]
        mov     ecx, [BLOCK_OFF + B_ECX]
        mov     edx, [BLOCK_OFF + B_EDX]
        mov     esi, [BLOCK_OFF + B_ESI]
        mov     edi, [BLOCK_OFF + B_EDI]
        mov     es, [BLOCK_OFF + B_ES]
        mov     ds, [BLOCK_OFF + B_DS]
rm_int_patch:
        int     0x00                    ; the self-patch target: keep
                                         ; PROBES AND DATA OUT of the
                                         ; byte right after this opcode

        ; ---- save results (BIOS may have clobbered DS)
        push    cs
        pop     ds
        mov     [BLOCK_OFF + B_EAX], eax
        mov     [BLOCK_OFF + B_EBX], ebx
        mov     [BLOCK_OFF + B_ECX], ecx
        mov     [BLOCK_OFF + B_EDX], edx
        mov     [BLOCK_OFF + B_ESI], esi
        mov     [BLOCK_OFF + B_EDI], edi
        mov     [BLOCK_OFF + B_ES], es
        pushf
        pop     word [BLOCK_OFF + B_FLG]
        ; ---- back: real -> protected 32
        mov     bx, tramp_gdtr - TR_BASE
        lgdt    [bx]
        mov     eax, cr0
        or      eax, 1
        mov     cr0, eax
        ; ptr16:32 (0x66-prefixed far jmp): the flat code32 selector has
        ; base 0, so the offset must be the FULL linear address - a
        ; plain ptr16:16 lands at 0x00000140 (live-caught in B2a).
        jmp     dword 0x08:BACK32

        bits 32
BACK32:

        mov     ax, 0x10
        mov     ds, ax
        mov     es, ax
        mov     ss, ax
        mov     fs, ax
        mov     gs, ax
        mov     esp, 0x40E00
        ; ---- paging + long mode back on (the core_head sequence)
        mov     eax, 0x8F000
        mov     cr3, eax
        mov     eax, cr4
        or      eax, 1 << 5                    ; PAE
        mov     cr4, eax
        mov     ecx, 0xC0000080
        rdmsr
        or      eax, (1 << 8) | (1 << 11)      ; LME | NXE
        wrmsr
        mov     eax, cr0
        or      eax, 0x80000000 | 0x10000      ; PG | WP
        mov     cr0, eax

        jmp     0x18:BACK64

        bits 64
BACK64:
        mov     ax, 0x10
        mov     ds, ax
        mov     es, ax
        mov     ss, ax
        mov     fs, ax
        mov     gs, ax
        mov     rsp, [0x40300 + B_RSP]
        mov     rax, [0x40300 + B_RIP]
        jmp     rax

; ---------------------------------------------------------------------------
; GDT: core-compatible selectors + 16-bit code/data
; ---------------------------------------------------------------------------
align 8
tramp_gdt:
        dq 0                                    ; 0x00 null
        dw 0xFFFF, 0x0000                       ; 0x08 code32
        db 0x00, 0x9A, 0xCF, 0x00
        dw 0xFFFF, 0x0000                       ; 0x10 data32
        db 0x00, 0x92, 0xCF, 0x00
        dw 0xFFFF, 0x0000                       ; 0x18 code64 (L=1)
        db 0x00, 0x9A, 0xAF, 0x00
        ; 16-bit descriptors with BASE = 0x40000: a D=0 code segment
        ; truncates offsets to 16 bits, so the segment base must BE the
        ; trampoline (GRUB gets away with base 0 because its whole
        ; trampoline lives below 64 KiB - live-caught in B2a).
        dw 0xFFFF, 0x0000                       ; 0x20 code16 (D=0)
        db 0x04, 0x9A, 0x00, 0x00
        dw 0xFFFF, 0x0000                       ; 0x28 data16 (B=0)
        db 0x04, 0x92, 0x00, 0x00
tramp_gdt_end:

tramp_gdtr:
        dw tramp_gdt_end - tramp_gdt - 1
        dd tramp_gdt
        dd 0

; pad the blob so the request block starts at 0x40100
times BLOCK_OFF-($-$$) db 0
