; SPDX-License-Identifier: Apache-2.0
; Copyright 2026 cubestudio-dev <cubestudio@qq.com>
; Open Cube OS - 3.0.0-WP05-ocbl
; File: ocbl/bios/core_head.S
; Purpose: 32-bit core entry at linear 0x8200 (both stage1 flavors
;          converge here with paging OFF and a flat 32-bit GDT loaded).
;
;          1. switch to the core's own GDT (32-bit code/data + 64-bit code)
;          2. zero the core .bss (bounds from the linker script)
;          3. build identity page tables at fixed low addresses:
;               PML4  @ 0x8F000 -> PDPT @ 0x90000
;               PDPT  @ 0x90000 -> PD[0..3] @ 0x91000..0x94000
;               every PD entry = 2 MiB page, present|writable|huge
;               (identity-maps the first 4 GiB with 4 tables)
;          4. enable PAE + LME (+NXE) + PG, far-jump into 64-bit code
;          5. set the core stack (0x9F000, grows down) and call ocbl_main
;
;          The core image stays well below the EBDA (0x9FC00) - the
;          linker enforces nothing, the build logs the final size and
;          ocbl.mk fails the build if the image would cross 0x40000
;          (the trampoline scratch area).

                bits 32


                global core_head
                extern ocbl_run
                extern __bss_start
                extern __bss_end

                section .text.head progbits alloc exec nowrite align=16

core_head:
        cli
        cld
        ; stage1's PE switch refreshes CS (0x08) but leaves the segment
        ; registers holding real-mode values - 0x07C0 as a selector is
        ; beyond the GDT limit.  Load flat data segments FIRST (0x10 in
        ; stage1's GDT) or the very first DS-relative access faults.
        mov     ax, 0x10
        mov     ds, ax
        mov     es, ax
        mov     ss, ax
        mov     fs, ax
        mov     gs, ax
        ; ---- own GDT (adds the 64-bit code descriptor)
        lgdt    [core_gdt_desc]
        mov     ax, 0x10                        ; data32
        mov     ds, ax
        mov     es, ax
        mov     ss, ax
        mov     fs, ax
        mov     gs, ax
        jmp     0x08:.reload_cs
.reload_cs:
        ; ---- zero .bss
        mov     edi, __bss_start
        mov     ecx, __bss_end
        sub     ecx, edi
        xor     eax, eax
        shr     ecx, 2
        rep stosd
        mov     ecx, __bss_end
        sub     ecx, edi
        and     ecx, 3
        rep stosb

        ; ---- identity page tables: PML4[0] -> PDPT, PDPT[0..3] -> PDs,
        ;      every PD entry a 2 MiB page (4 GiB total)
        mov     edi, 0x8F000                    ; PML4
        mov     dword [edi], 0x90003            ; PDPT | P | W
        mov     eax, 0x91003                     ; PD[0] | P | W
        mov     ecx, 0x90000                     ; PDPT entry iterator
.pt_pdpt:
        mov     [ecx], eax
        add     eax, 0x1000                       ; next PD table
        add     ecx, 8
        cmp     ecx, 0x90020                     ; 4 entries
        jb      .pt_pdpt
        ; fill all 4 PD tables with 2 MiB pages (2048 entries)
        mov     edi, 0x91000
        mov     eax, 0x83                        ; P | W | PS (2 MiB page)
        mov     ecx, 2048
.pt_pd:
        mov     [edi], eax
        add     eax, 0x200000
        add     edi, 8
        dec     ecx
        jnz     .pt_pd

        ; ---- long mode on
        mov     eax, 0x8F000
        mov     cr3, eax
        mov     eax, cr4
        or      eax, 1 << 5                      ; PAE
        mov     cr4, eax
        mov     ecx, 0xC0000080                  ; IA32_EFER
        rdmsr
        or      eax, (1 << 8) | (1 << 11)        ; LME | NXE
        wrmsr
        mov     eax, cr0
        or      eax, 0x80000001                  ; PG | WP
        mov     cr0, eax
        jmp     0x18:core_long

        bits 64
core_long:
        mov     ax, 0x10
        mov     ds, ax
        mov     es, ax
        mov     fs, ax
        mov     gs, ax
        mov     ss, ax
        mov     rsp, 0x9F000                     ; core stack top
        xor     rbp, rbp
        call    ocbl_run
.hang:
        cli
        hlt
        jmp     .hang

; ---------------------------------------------------------------------------
; ocbl_handoff(rdi = kernel 32-bit entry, rsi = mbi physical)
;
; Downgrades the core back to 32-bit protected mode with paging OFF
; and jumps to the kernel entry with the multiboot2 contract
; (EAX = 0x36D76289, EBX = mbi).  Mirrors GRUB's handoff to boot.S.
; ---------------------------------------------------------------------------
        global ocbl_handoff
        section .text.ho progbits alloc exec nowrite align=16
        bits 64
ocbl_handoff:
        cli
        cld
        mov rcx, rdi                     ; kernel entry
        ; STEP 1: far-jump into the 32-bit COMPATIBLE code segment
        ; while long mode is still active (paging ON).  Clearing PG
        ; while CS.L=1 is invalid would #GP on the very next fetch
        ; (live-caught: three variants of that, all triple faults).
        ; 64-bit-mode far jmp reads m16:64 (8-byte offset + selector)
        ; - the same 10-byte layout the working trampoline entry uses
        lea rax, [rel handoff32]
        mov [rel farptr], rax
        mov word [rel farptr+8], 0x08
        jmp far [rel farptr]

        section .data
        align 8
farptr: dq 0
        dw 0
        section .text.ho

        bits 32
handoff32:
        ; STEP 2: now drop the paging - CS.L=0 keeps legacy valid
        mov eax, cr0
        btc eax, 31                      ; PG off -> legacy mode
        mov cr0, eax
        mov eax, cr4
        and eax, ~0x20                   ; PAE off (legal without PG)
        mov cr4, eax
        xor eax, eax
        mov cr3, eax
        mov eax, 0x36D76289              ; multiboot2 boot magic
        mov ebx, esi                     ; mbi physical (low dword)
        jmp ecx                          ; kernel _start

; ---------------------------------------------------------------------------
; ocbl_chain16(rdi = drive number, rsi = VBR LBA already LOADED by the
; caller into 0x7C00)
;
; Full downgrade to REAL MODE with interrupts still vectored through
; the BIOS IVT, then a far jump to the loaded VBR with the classic
; chainload register contract (DL = BIOS drive).  This is what GRUB's
; `chainloader +1` does for BIOS-booted partitions.
; ---------------------------------------------------------------------------
        global ocbl_chain16
        bits 64
ocbl_chain16:
        cli
        cld
        mov r8d, edi                     ; drive number
        ; 64 -> 32 (compatible segment, paging still on)
        lea rax, [rel chain32]
        mov [rel farptr], rax
        mov word [rel farptr+8], 0x08
        jmp far [rel farptr]

        bits 32
chain32:
        mov eax, cr0
        btc eax, 31                      ; PG off -> legacy mode
        mov cr0, eax
        mov eax, cr4
        and eax, ~0x20                   ; PAE off
        mov cr4, eax
        xor eax, eax
        mov cr3, eax
        ; The trampoline leaves the TRAMP GDT loaded (its 0x20 base is
        ; 0x40000, live-caught: far jmping to chain16 through it
        ; triple-faulted).  Reload the CORE GDT first - its 0x20 is
        ; the flat-base code16 this path needs.
        lgdt [core_gdt_desc]
        ; 32 -> 16-bit protected (byte-granular segments)
        jmp dword 0x20:chain16

        bits 16
chain16:
        mov eax, cr0
        and eax, ~1
        mov cr0, eax
        xor ax, ax
        mov cx, ax
        mov dx, ax
        mov ds, ax
        mov es, ax
        mov fs, ax
        mov gs, ax
        mov ss, ax
        mov sp, 0x7C00                   ; stack just below the VBR
        jmp dword 0x0000:0x7C00          ; far jump: real mode, CS=0

; ---------------------------------------------------------------------------
; data
; ---------------------------------------------------------------------------
        section .rodata progbits alloc noexec nowrite align=8
core_gdt:
        dq 0                                    ; 0x00 null
        dw 0xFFFF, 0x0000
        db 0x00, 0x9A, 0xCF, 0x00               ; 0x08 code32
        dw 0xFFFF, 0x0000
        db 0x00, 0x92, 0xCF, 0x00               ; 0x10 data32
        dw 0xFFFF, 0x0000
        db 0x00, 0x9A, 0xAF, 0x00               ; 0x18 code64 (L=1)
        dw 0xFFFF, 0x0000
        db 0x00, 0x9A, 0x8F, 0x00               ; 0x20 code16 (byte gran)
        dw 0xFFFF, 0x0000
        db 0x00, 0x92, 0x8F, 0x00               ; 0x28 data16
core_gdt_end:

core_gdt_desc:
        dw core_gdt_end - core_gdt - 1
        dd core_gdt
        dd 0
