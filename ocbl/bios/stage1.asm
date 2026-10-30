; SPDX-License-Identifier: Apache-2.0
; Copyright 2026 cubestudio-dev <cubestudio@qq.com>
; Open Cube OS - 3.0.0-WP05-ocbl
; File: ocbl/bios/stage1.asm
; Purpose: OCBL stage 1 - a 512-byte real-mode boot sector, built in two
;          flavors from this one source (nasm -D choice):
;
;            OCBL_ELTORITO (default) - the ISO El Torito no-emulation image.
;              xorriso's mkisofs emulation pins no load segment (0x0000),
;              and SeaBIOS (verified live in QEMU, WP05 B1) loads the
;              image at 0000:0x7C00, exactly like an MBR.  The whole image
;              (stage1 + core, boot-load-size sectors) is preloaded, so
;              the core sits at linear 0x7E00 from the start.
;
;            OCBL_MBR - the boot sector ocbl-install writes to disks.
;              BIOS loads it at 0x7C00; the installer patched the core
;              image's start LBA and sector count into the patch block,
;              stage1 raw-reads the core to linear 0x7E00 with int 13h
;              extensions (one reset+retry), then proceeds.
;
;          Entry check: both BIOS forms are accepted -
;              0000:0x7C00 (SeaBIOS El Torito seg-0, classic MBR) or
;              0x07C0:0x0000 (segment-style boot).  Anything else halts
;              with a serial message (addressing constants would break).
;
;          Data segment: DS = 0x07C0 at entry (org-0 labels are then
;          linear-correct); ES = 0 for the absolute low-memory stores.
;
;          Both flavors write {boot drive, flavor, entry CS} to linear
;          0x520..0x523 for the 64-bit core, then: serial banner, A20
;          enable + verify (int 15h 2401 + port 0x92), GDT load, PE
;          switch, far jump to the 32-bit core head at 0x7E00.
;
;          A20 verify: two independent write-tests (orig^0x55, orig^0xAA)
;          against the 0xFFFF:0x0510 alias; A20 is OFF only when BOTH
;          patterns alias (a single coincidence is not trusted - the
;          reverse of the classic false-positive, fixed live in B1 when
;          a 0x55 byte at 0x500 produced a false negative).
;
;          A20 methods: BIOS int 15h 2401, then port 0x92.  The keyboard-
;          controller fallback is intentionally absent (declared): every
;          machine that can boot El Torito CD, and every disk our
;          installer targets, postdates port 0x92 hardware; KBC-era
;          machines also implement int 15h 2401.
;
;          Patch block at image offset 0x1A0 (MBR flavor only; zeros in
;          the El Torito build - also clear of the xorriso
;          -boot-info-table write area at 8..0x47):
;            +0x1A0 u32 magic 'OCBL' (0x4C42434F)
;            +0x1A4 u32 core_lba_lo, +0x1A8 u32 core_lba_hi
;            +0x1AC u16 core_sectors (512-byte units)
;
;          The MBR flavor is deliberately SILENT (no serial code or
;          strings): a 512-byte sector carrying a raw-read path plus a
;          patch block has no room for diagnostics - the same tradeoff
;          GRUB's boot.img and ISOLINUX's stage1 make.  Failures halt;
;          the core speaks once it is in, and ocbl-install verifies the
;          install on the writer side.
;
;          Bytes 446..509 stay zero in the image; ocbl-install copies the
;          real partition table over them. 510..511 = 0xAA55.

                bits 16
                org 0

%ifndef OCBL_MBR
%define OCBL_ELTORITO
%endif

%define PATCH            0x1A0
%define PATCH_LBA_LO    (PATCH + 4)
%define PATCH_LBA_HI    (PATCH + 8)
%define PATCH_SECTORS   (PATCH + 12)

COM1            equ 0x3F8

start:
        cli
        cld
        xor     ax, ax
        mov     ss, ax
        mov     sp, 0x7C00                  ; stack below the load area
        mov     es, ax                       ; ES=0: absolute low stores
        mov     [es:0x520], dl               ; boot drive for the core
%ifdef OCBL_MBR
        mov     byte [es:0x521], 1
%else
        mov     byte [es:0x521], 0
%endif
        ; entry form check (before DS is touched): accept
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
%ifdef OCBL_ELTORITO
        mov     si, s_base
        call    ser_str
%endif
.hang_b:
        hlt
        jmp     .hang_b
.base_ok:
        mov     [es:0x522], cs               ; the 64-bit core prints it
        mov     ax, 0x07C0
        mov     ds, ax                       ; data labels now linear-correct
%ifdef OCBL_ELTORITO
        call    ser_init
%endif

%ifdef OCBL_MBR
        ; ---- disk flavor: the core is NOT preloaded - raw-read it
        mov     ax, 0x0060                    ; DAP at linear 0x600
        mov     es, ax
        xor     si, si
        mov     word [es:si+0], 0x0010
        mov     word [es:si+2], 0
        mov     ax, [PATCH_SECTORS]
        mov     word [es:si+4], ax
        mov     word [es:si+6], 0x7E00       ; target offset
        mov     word [es:si+8], 0x0000       ; target segment -> 0x7E00
        mov     ax, [PATCH_LBA_LO]
        mov     word [es:si+10], ax
        mov     ax, [PATCH_LBA_HI]
        mov     word [es:si+12], ax
        mov     word [es:si+14], 0
        mov     ah, 0x42
        int     0x13                          ; dl = boot drive from entry
        jnc     .read_ok
        xor     ax, ax                        ; reset + one retry
        int     0x13
        mov     ah, 0x42
        int     0x13
        jnc     .read_ok
.hang_r:                                ; MBR stage1 is deliberately silent
        hlt                                   ; (512B budget; the industry
        jmp     .hang_r                       ;  pattern - GRUB boot.img and
                                                ;  ISOLINUX stage1 carry no
                                                ;  strings; core speaks after)
.read_ok:
        xor     ax, ax
        mov     es, ax
%endif

        ; ---- early serial banner (El Torito flavor only)
%ifdef OCBL_ELTORITO
        mov     si, s_banner
        call    ser_str
%endif

        ; ---- A20: BIOS, then port 0x92, verify after each
        mov     ax, 0x2401
        int     0x15
        call    a20_check
        jnz     .a20_on
        in      al, 0x92
        or      al, 0x02
        and     al, 0xFE                      ; bit 0 = fast reset: never
        out     0x92, al
        call    a20_check
        jnz     .a20_on
%ifdef OCBL_ELTORITO
        mov     si, s_a20
        call    ser_str
%endif
.hang_a:
        hlt
        jmp     .hang_a
.a20_on:

        ; ---- 32-bit protected mode, far jump to the core head
        lgdt    [gdt_desc]
        mov     eax, cr0
        or      eax, 1
        mov     cr0, eax
        jmp     0x08:0x7E00

; ---------------------------------------------------------------------------
; helpers (all data via DS = 0x07C0)
; ---------------------------------------------------------------------------

; a20_check: ZF=0 when A20 is ON.  OFF only if BOTH patterns alias.
a20_check:
        push    ds
        push    es
        push    bx
        xor     ax, ax
        mov     ds, ax
        mov     ah, [0x0500]                  ; original byte
        mov     al, ah
        xor     al, 0x55                      ; pattern 1
        mov     [0x0500], al
        push    ax                             ; {orig, p1}
        mov     bx, 0xFFFF
        mov     ds, bx
        mov     bl, [0x0510]                  ; alias read 1
        xor     ax, ax
        mov     ds, ax
        pop     ax                             ; al = p1
        cmp     bl, al
        jne     .on                            ; no alias: definitely ON
        mov     al, ah
        xor     al, 0xAA                      ; pattern 2
        mov     [0x0500], al
        push    ax
        mov     bx, 0xFFFF
        mov     ds, bx
        mov     bl, [0x0510]                  ; alias read 2
        xor     ax, ax
        mov     ds, ax
        pop     ax                             ; al = p2
        cmp     bl, al
        je      .off                           ; both aliased: really OFF
.on:
        mov     [0x0500], ah                  ; restore original
        mov     bx, 1                          ; ZF=0 -> ON
        jmp     .flags
.off:
        mov     [0x0500], ah
        xor     bx, bx                         ; ZF=1 -> OFF
.flags:
        test    bx, bx
        pop     bx
        pop     es
        pop     ds
        ret

%ifdef OCBL_ELTORITO
; ser_init: 16550 COM1 at 115200 8N1, polled.
ser_init:
        mov     dx, COM1+1
        xor     al, al
        out     dx, al
        mov     dx, COM1+3
        mov     al, 0x80
        out     dx, al                          ; DLAB
        mov     dx, COM1
        mov     al, 1
        out     dx, al
        mov     dx, COM1+1
        xor     al, al
        out     dx, al
        mov     dx, COM1+3
        mov     al, 0x03
        out     dx, al                          ; 8N1
        mov     dx, COM1+2
        mov     al, 0xC7
        out     dx, al                          ; FIFO
        mov     dx, COM1+4
        mov     al, 0x0B
        out     dx, al                          ; DTR|RTS|OUT2
        ret

; ser_str: print DS:SI NUL string to COM1.
ser_str:
        lodsb
        or      al, al
        jz      .done
        mov     bl, al
        mov     dx, COM1+5
.w:
        in      al, dx
        test    al, 0x20
        jz      .w
        mov     dx, COM1
        mov     al, bl
        out     dx, al
        jmp     ser_str
.done:
        ret
%endif

; ---------------------------------------------------------------------------
; data (DS = 0x07C0, org-0 offsets are linear-correct)
; ---------------------------------------------------------------------------
%ifdef OCBL_ELTORITO
s_banner:       db "OCBL stage1 ok", 13, 10, 0
s_a20:          db "OCBL stage1: A20 stuck off", 13, 10, 0
s_base:         db "OCBL stage1: bad entry form", 13, 10, 0
%endif

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
        dd (0x7C00 + gdt_start)                  ; base 0x7C00 (entry check)

; patch block (offset 0x1A0, MBR flavor)
times PATCH-($-$$) db 0
%ifdef OCBL_MBR
        dd 0x4C42434F                          ; 'OCBL' magic
        dd 0                                   ; core_lba_lo
        dd 0                                   ; core_lba_hi
        dw 0                                   ; core_sectors
%else
        times 14 db 0
%endif

; partition-table area + boot signature
times 446-($-$$) db 0
        times 64 db 0
        dw 0xAA55
