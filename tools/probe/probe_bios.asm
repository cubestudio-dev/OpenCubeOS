; SPDX-License-Identifier: Apache-2.0
; Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
; Open Cube OS - 3.0.0-WP-runtime-hardening-and-experience-rebase
; File: tools/probe/probe_bios.asm
; Purpose: the bare-metal QEMU-behavior probe (BIOS/SeaBIOS side).
;          El Torito preloads this whole image at 0000:0x7C00; the
;          probe dumps the machine state the firmware handed over -
;          the exact set the 拉平清单 (flattening inventory) documents:
;
;            - every GPR + segment + FLAGS at the boot-sector entry
;            - GDTR / IDTR (limit + base)
;            - A20 line state (double-pattern alias test)
;            - 8259A IMRs (in 0x21 / in 0xA1) + the cascade state
;            - IOAPIC version register (unreal-mode MMIO read)
;
;          The image is deliberately SILENT about VGA - serial only
;          (the probe runs under -display none in both QEMU modes).
;
;          Output contract: one line per fact, "PROBE:" prefixed,
;          ending with "PROBE-BIOS done" - the shell driver diffs the
;          DEFAULT vs STRICT (-cpu qemu64,enforce -machine vmport=off
;          -boot strict=on) transcripts into the flattening list.

                bits 16
                org 0x7C00

COM1            equ 0x3F8

; entry-register save area (0x7000, never BIOS scratch - the OCRMECL
; 0x540-lesson)
SAVE            equ 0x7000

start:
        cli
        cld
        xor     ax, ax
        mov     ss, ax
        mov     sp, 0x7C00
        ; DS at entry is the FIRMWARE's DS - save it, then zero DS so
        ; every SAVE-area store/read below is linear 0x7000 (the probe
        ; transcript of the first run read firmware-segment garbage)
        mov     [SAVE+14], ds
        mov     [SAVE+16], es
        mov     ds, ax                      ; DS = 0: SAVE/absolute access
        mov     [SAVE+0], ax               ; AX
        mov     [SAVE+2], bx
        mov     [SAVE+4], cx
        mov     [SAVE+6], dx
        mov     [SAVE+8], si
        mov     [SAVE+10], di
        mov     [SAVE+12], bp
        mov     ax, cs
        mov     [SAVE+18], ax
        mov     ax, ss
        mov     [SAVE+20], ax
        pushf
        pop     ax
        mov     [SAVE+22], ax
        mov     ax, sp                     ; entry SP is gone (we reset
        mov     [SAVE+24], ax              ; it) - record the reset one
        mov     byte [SAVE+26], dl         ; boot drive
        call    ser_init

        mov     si, s_banner
        call    ser_str
        ; registers
        mov     si, s_ax
        call    ser_str
        mov     ax, [SAVE+0]
        call    ser_hex16
        mov     si, s_bx
        call    ser_str
        mov     ax, [SAVE+2]
        call    ser_hex16
        mov     si, s_cx
        call    ser_str
        mov     ax, [SAVE+4]
        call    ser_hex16
        mov     si, s_dx
        call    ser_str
        mov     ax, [SAVE+6]
        call    ser_hex16
        call    ser_nl
        mov     si, s_si
        call    ser_str
        mov     ax, [SAVE+8]
        call    ser_hex16
        mov     si, s_di
        call    ser_str
        mov     ax, [SAVE+10]
        call    ser_hex16
        mov     si, s_bp
        call    ser_str
        mov     ax, [SAVE+12]
        call    ser_hex16
        mov     si, s_sp
        call    ser_str
        mov     ax, [SAVE+24]
        call    ser_hex16
        call    ser_nl
        mov     si, s_seg
        call    ser_str
        mov     ax, [SAVE+18]
        call    ser_hex16                  ; CS
        mov     al, '/'
        call    ser_chr
        mov     ax, [SAVE+14]
        call    ser_hex16                  ; DS
        mov     al, '/'
        call    ser_chr
        mov     ax, [SAVE+16]
        call    ser_hex16                  ; ES
        mov     al, '/'
        call    ser_chr
        mov     ax, [SAVE+20]
        call    ser_hex16                  ; SS
        call    ser_nl
        mov     si, s_flags
        call    ser_str
        mov     ax, [SAVE+22]
        call    ser_hex16
        mov     si, s_drive
        call    ser_str
        mov     al, [SAVE+26]
        call    ser_hex8
        call    ser_nl

        ; GDTR / IDTR
        mov     si, s_gdtr
        call    ser_str
        sgdt    [SAVE+32]
        mov     ax, [SAVE+32]
        call    ser_hex16
        mov     al, ':'
        call    ser_chr
        mov     eax, [SAVE+34]
        call    ser_hex32
        call    ser_nl
        mov     si, s_idtr
        call    ser_str
        sidt    [SAVE+40]
        mov     ax, [SAVE+40]
        call    ser_hex16
        mov     al, ':'
        call    ser_chr
        mov     eax, [SAVE+42]
        call    ser_hex32
        call    ser_nl

        ; A20 (double-pattern alias test, the OCRMECL discipline)
        call    a20_check
        jnz     .a20_on
        mov     si, s_a20off
        call    ser_str
        jmp     .a20_done
.a20_on:
        mov     si, s_a20on
        call    ser_str
.a20_done:
        call    ser_nl

        ; 8259A IMRs
        mov     si, s_pic
        call    ser_str
        in      al, 0x21
        call    ser_hex8
        mov     al, '/'
        call    ser_chr
        in      al, 0xA1
        call    ser_hex8
        call    ser_nl

        ; unreal-mode dance for the IOAPIC MMIO read
        lgdt    [gdt_desc]
        mov     eax, cr0
        or      eax, 1
        mov     cr0, eax
        mov     bx, 0x10
        mov     es, bx
        mov     eax, cr0
        and     eax, 0xFFFFFFFE
        mov     cr0, eax
        mov     si, s_ioapic
        call    ser_str
        mov     eax, 1
        mov     [dword 0xFEC00000], eax
        mov     eax, [dword 0xFEC00010]
        call    ser_hex32
        call    ser_nl
        xor     ax, ax
        mov     es, ax

        mov     si, s_done
        call    ser_str
.hang:
        hlt
        jmp     .hang

; ---------------------------------------------------------------------------
a20_check:
        push    es
        push    bx
        mov     ah, [0x07D00]              ; 0x7C00 + 0x100 (DS base 0x7C00)
        mov     al, ah
        xor     al, 0x55
        mov     [0x07D00], al
        push    ax
        mov     bx, 0xFFFF
        mov     es, bx
        mov     bl, [es:0x0510]
        xor     ax, ax
        mov     es, ax
        pop     ax
        cmp     bl, al
        jne     .on
        mov     al, ah
        xor     al, 0xAA
        mov     [0x07D00], al
        push    ax
        mov     bx, 0xFFFF
        mov     es, bx
        mov     bl, [es:0x0510]
        xor     ax, ax
        mov     es, ax
        pop     ax
        cmp     bl, al
        je      .off
.on:
        mov     [0x07D00], ah
        mov     bx, 1
        jmp     .flags
.off:
        mov     [0x07D00], ah
        xor     bx, bx
.flags:
        test    bx, bx
        pop     bx
        pop     es
        ret

ser_init:
        mov     dx, COM1+1
        xor     al, al
        out     dx, al
        mov     dx, COM1+3
        mov     al, 0x80
        out     dx, al
        mov     dx, COM1
        mov     al, 1
        out     dx, al
        mov     dx, COM1+1
        xor     al, al
        out     dx, al
        mov     dx, COM1+3
        mov     al, 0x03
        out     dx, al
        mov     dx, COM1+2
        mov     al, 0xC7
        out     dx, al
        mov     dx, COM1+4
        mov     al, 0x0B
        out     dx, al
        ret

; ser_str: print the NUL string at ES:SI (DS stays 0 - the SAVE area
; and every absolute access keep their linear meaning; ES = 0x07C0
; carries the label segment)
ser_str:
        mov     al, [es:si]
        or      al, al
        jz      .d
        mov     bl, al
        mov     dx, COM1+5
.w:
        in      al, dx
        test    al, 0x20
        jz      .w
        mov     dx, COM1
        mov     al, bl
        out     dx, al
        inc     si
        jmp     ser_str
.d:
        ret

ser_chr:
        mov     bl, al
        mov     dx, COM1+5
.w2:
        in      al, dx
        test    al, 0x20
        jz      .w2
        mov     dx, COM1
        mov     al, bl
        out     dx, al
        ret

ser_nl:
        mov     al, 13
        call    ser_chr
        mov     al, 10
        jmp     ser_chr

ser_hex8:
        push    ax
        shr     al, 4
        call    .n
        pop     ax
        and     al, 15
        call    .n
        ret
.n:
        cmp     al, 10
        jb      .d2
        add     al, 'A'-10-'0'
.d2:
        add     al, '0'
        jmp     ser_chr

ser_hex16:
        push    ax
        mov     al, ah                ; high byte first
        mov     ah, al
        shr     al, 4
        call    .n2
        mov     al, ah
        and     al, 15
        call    .n2
        pop     ax
        push    ax
        mov     ah, al                ; then the low byte
        shr     al, 4
        call    .n2
        mov     al, ah
        and     al, 15
        call    .n2
        pop     ax
        ret
.n2:
        cmp     al, 10
        jb      .d3
        add     al, 'A'-10-'0'
.d3:
        add     al, '0'
        jmp     ser_chr

ser_hex32:
        push    eax
        shr     eax, 16
        call    ser_hex16
        pop     eax
        jmp     ser_hex16

; ---------------------------------------------------------------------------
s_banner:       db "PROBE-BIOS v1 (SeaBIOS El Torito entry state)", 13, 10, 0
s_ax:           db "AX=", 0
s_bx:           db " BX=", 0
s_cx:           db " CX=", 0
s_dx:           db " DX=", 0
s_si:           db "SI=", 0
s_di:           db " DI=", 0
s_bp:           db " BP=", 0
s_sp:           db " SP=", 0
s_seg:          db "CS/DS/ES/SS=", 0
s_flags:        db "FLAGS=", 0
s_drive:        db " DL=", 0
s_gdtr:         db "GDTR=", 0
s_idtr:         db "IDTR=", 0
s_a20on:        db "A20=on", 0
s_a20off:       db "A20=off", 0
s_pic:          db "PIC-IMR=", 0
s_ioapic:       db "IOAPIC-VER=", 0
s_done:         db "PROBE-BIOS done", 13, 10, 0

align 8
gdt_start:
        dq 0
        dw 0xFFFF, 0x0000
        db 0x00, 0x9A, 0xCF, 0x00               ; 0x08 code32
        dw 0xFFFF, 0x0000
        db 0x00, 0x92, 0xCF, 0x00               ; 0x10 data32
gdt_end:
gdt_desc:
        dw gdt_end - gdt_start - 1
        dd gdt_start

; The probe image is a whole El Torito no-emulation preload (like the
; OCRMECL composite) - the 0xAA55 signature is an MBR convention the
; no-emul path never checks, and the probe body legitimately spans
; past the first sector (linear 0x7E00+).  Pad to a 512 multiple.
align 512
times 4096-($-$$) db 0
