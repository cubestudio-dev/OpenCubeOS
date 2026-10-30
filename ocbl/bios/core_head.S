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
core_gdt_end:

core_gdt_desc:
        dw core_gdt_end - core_gdt - 1
        dd core_gdt
        dd 0
