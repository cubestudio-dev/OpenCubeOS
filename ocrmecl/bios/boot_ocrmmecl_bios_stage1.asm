; SPDX-License-Identifier: Apache-2.0
; Copyright 2026 cubestudio-dev <cubestudio@qq.com>
; Open Cube OS - 3.0.0-WP-runtime-hardening-and-experience-rebase
; File: ocrmecl/bios/ocr_stage1.asm
; Purpose: OCRMECL stage 1 - the 512-byte El Torito boot sector of the
;          OCRMECL (Open Cube Runtime Machine Environment Compatibility
;          Layer) BIOS track.  The ISO El Torito no-emulation image is
;          the OCRMECL composite:
;
;            [ ocr stage1 (512 B) ][ ocr core (512-padded) ][ OCBL.BIN ]
;
;          preloaded whole at linear 0x7C00 by SeaBIOS (same contract
;          the OCBL image used since WP05 - xorriso pins load segment
;          0x0000).  Stage1 does the minimum a 512-byte sector can:
;          accept both entry forms, arm a stack, save the boot drive,
;          wake the serial line for the evidence transcript, and far
;          jump into the real-mode core at linear 0x7E00.  Everything
;          else (A20, GDT/IDT, PIC/IOAPIC, the OCBL move) is core work.
;
;          Patch block at image offset 0x1A0 (written by
;          ocrmecl/bios/build_ocr_image.py - see interface/boot_handoff.h):
;            +0x1A0 u32 magic 'OCRM' (0x4D52434F)
;            +0x1A4 u32 OCBL.BIN byte size
;            +0x1A8 u32 stage1+core byte size (the OCBL.BIN offset)
;            +0x1AC u32 reserved
;
;          The OCRMECL stage is deliberately SILENT on the VGA path
;          (no text-mode writes at all - the boot looks identical to
;          the user) and speaks only on COM1, one line, like the OCBL
;          stage1 before it.
;
;          Bytes 446..509 stay zero (partition-table area, mirrors the
;          OCBL stage1 discipline); 510..511 = 0xAA55.

                bits 16
                org 0

COM1            equ 0x3F8

start:
        cli
        cld
        xor     ax, ax
        mov     ss, ax
        mov     sp, 0x7C00                  ; stack below the load area
        mov     es, ax                       ; ES=0: absolute low stores
        mov     [es:0x7000], dl              ; boot drive for the core (0x7000: never BIOS scratch)
        ; F3: VGA progress marks (row 24) - observable without COM1
        push    es
        mov     bx, 0xB800
        mov     es, bx
        mov     word [es:0x0F00], 0x0F52     ; 'R' white-on-black
        pop     es
        ; entry form check: accept
        ;   07C0:0000 -> pop ax == (.ip)      ; file-offset IP
        ;   0000:7C00 -> pop ax == (.ip)+0x7C00
        call    .ip
.ip:
        pop     ax
        cmp     ax, .ip
        je      .base_ok
        sub     ax, .ip
        cmp     ax, 0x7C00
        je      .base_ok
        mov     si, s_base
        call    ser_str
.hang_b:
        hlt
        jmp     .hang_b
.base_ok:
        mov     ax, 0x07C0
        mov     ds, ax                       ; data labels now linear
        call    ser_init
        mov     si, s_banner
        call    ser_str
        ; F3: stage1 complete -> core
        push    es
        mov     bx, 0xB800
        mov     es, bx
        mov     word [es:0x0F02], 0x0F42     ; 'B' white-on-black
        pop     es
        ; into the real-mode OCRMECL core at linear 0x7E00
        jmp     0x07E0:0x0000

; ---------------------------------------------------------------------------
; helpers (all data via DS = 0x07C0)
; ---------------------------------------------------------------------------
; ser_init: 16550 COM1 at 115200 8N1, polled (same line discipline as
; the OCBL stage1 - the evidence transcript stays comparable).
ser_init:
        ; F2: scratch-register probe (two patterns) - a missing/broken
        ; COM1 is declared dead instantly and every later print is
        ; skipped (the F1 per-char timeout stays as the backstop).
        mov     dx, COM1+7
        mov     al, 0x5A
        out     dx, al
        in      al, dx
        cmp     al, 0x5A
        jne     .dead
        mov     al, 0xA5
        out     dx, al
        in      al, dx
        cmp     al, 0xA5
        jne     .dead
        mov     dx, COM1+1
        xor     al, al
        out     dx, al                        ; disable ints
        mov     dx, COM1+3
        mov     al, 0x80
        out     dx, al                         ; DLAB
        mov     dx, COM1
        mov     al, 1
        out     dx, al                         ; divisor 1 = 115200
        mov     dx, COM1+1
        xor     al, al
        out     dx, al
        mov     dx, COM1+3
        mov     al, 0x03
        out     dx, al                         ; 8N1
        mov     dx, COM1+2
        mov     al, 0xC7
        out     dx, al                         ; FIFO
        mov     dx, COM1+4
        mov     al, 0x0B
        out     dx, al                         ; DTR|RTS|OUT2
        ret
.dead:
        mov     byte [ser_dead], 1             ; no usable COM1: line dead
        ret

; ser_str: print DS:SI NUL string to COM1 - BOUNDED (WP-boot-stack F1).
; A dead/blocked line burns the per-char budget once, is declared
; dead, and every later print returns immediately - a broken serial
; console must never hang the boot.
ser_str:
        cmp     byte [ser_dead], 1
        je      .done
        lodsb
        or      al, al
        jz      .done
        mov     bl, al
        mov     cx, 0xFFFF                 ; per-char wait budget
        mov     dx, COM1+5
.w:
        in      al, dx
        test    al, 0x20
        jnz     .go
        loop    .w
        mov     byte [ser_dead], 1         ; timeout: line declared dead
        jmp     .done
.go:
        mov     dx, COM1
        mov     al, bl
        out     dx, al
        jmp     ser_str
.done:
        ret

ser_dead:       db 0                        ; 1 = COM1 declared dead

; ---------------------------------------------------------------------------
; data (DS = 0x07C0, org-0 offsets are linear-correct)
; ---------------------------------------------------------------------------
s_banner:       db "OCRMECL stage1 ok", 13, 10, 0
s_base:         db "OCRMECL stage1: bad entry form", 13, 10, 0

; patch block (offset 0x1A0 - the packer fills it)
times 0x1A0-($-$$) db 0
        dd 0x4D52434F                          ; 'OCRM' magic
        dd 0                                   ; OCBL.BIN byte size
        dd 0                                   ; stage1+core byte size
        dd 0                                   ; reserved

; partition-table area + boot signature
times 446-($-$$) db 0
        times 64 db 0
        dw 0xAA55
