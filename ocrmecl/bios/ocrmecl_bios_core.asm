; SPDX-License-Identifier: Apache-2.0 */
; Copyright 2026 cubestudio-dev <cubestudio@qq.com>
; Open Cube OS - 3.0.0-WP-runtime-hardening-and-experience-rebase
; File: ocrmecl/bios/ocr_core.asm
; Purpose: OCRMECL BIOS-track core - the machine-state flattener that
;          runs BEFORE OCBL on the El Torito path (real mode, linear
;          0x7E00, entered from ocr_stage1 with SS:SP=0:0x7C00 and the
;          boot drive stored at linear 0x530).
;
;          The flatten sequence (contract: interface/boot_handoff.h):
;            1. A20 force-on (int 15h 2401 -> port 0x92, verified by
;               the double-pattern alias test, halt on failure)
;            2. temp GDT at 0x1000 + 32-bit fault stub at 0x1100 +
;               256-gate IDT at 0x2000 (all gates -> the stub; the
;               tables live BELOW 0x7C00 so they survive the OCBL move
;               and cover the stage1->core_head->ocbl_idt_setup window
;               that historically inherited the firmware IVT)
;            3. 8259A remap to 0x08/0x70 + full mask
;            4. unreal-mode dance (flat 4G DS/ES caches, base 0 - all
;               in-image labels stay linear-correct), then IOAPIC
;               all-RTE mask via MMIO at 0xFEC00000
;            5. QEMU/firmware leftover flatten: PIC EOI, PS/2 output
;               drain (bounded), DMA controller mask-all
;            6. backward-move the embedded OCBL.BIN to 0x7C00 through
;               a position-independent stub copied to 0x6000 (the only
;               live code below the destination), then far-jump into
;               it with the boot drive in DL - the exact El Torito
;               contract SeaBIOS gave us, minus the nanny state.
;
;          DS discipline: DS=ES=0 with org 0x7E00 -> labels are
;          linear addresses; everything the core touches (0x500-0x7BFF
;          scratch, 0x7E00-0x9FFF image, IOAPIC MMIO post-dance) is
;          reachable, and the unreal dance (base-0 flat caches) keeps
;          label addressing valid throughout.

                bits 16
                org 0x7E00

COM1            equ 0x3F8
IOAPIC_BASE     equ 0xFEC00000

; low-memory params.  LIVE-CAUGHT (the first boot test): the 0x500
; area is BIOS scratch - the int 15h A20 service (its SMM detour)
; overwrote 0x540+ with a boot-sector head copy and a timestamp
; between our write and the stub's read, and the move went to a
; garbage destination.  0x7000-0x702F is above every BIOS scratch
; range and below the stub window - the params live there.
P_DRIVE         equ 0x7000          ; u8  boot drive (stage1 wrote it)
P_SRC           equ 0x7010          ; u32 move source linear
P_CNT           equ 0x7014          ; u32 move byte count
P_RTECNT        equ 0x7020          ; u32 scratch (IOAPIC RTE count)
P_IDTR          equ 0x7030          ; 6 B  saved firmware IDTR (limit+base, real-mode ABI)

; OCBL stage1 runs in REAL MODE until its own PE switch and therefore
; uses 4-byte IVT entries (offset:segment) addressed through IDTR, not
; the 16-byte gate format of our 256-gate table at 0x2000.  Handing our
; table over while stage1 is still in real mode makes every int 13h /
; int 15h in stage1 read a 4-byte window out of the MIDDLE of a 16-byte
; gate and vector into garbage.  Observed live under QEMU 10.2.1:
; int 15h AX=2401 resolved to 0000:8E00 (gate 0x15's bytes 4..7) and
; the machine executed OCBL-core fragments, then the zero page, then
; VGA memory; the resulting #UD storm wrote a wide value to COM1 and
; tripped QEMU's 16550 assertion (size == 1 && addr < 8).
; So: SAVE the firmware's real-mode IDTR before we install ours, and
; RESTORE it in the move stub immediately before the far jump - OCBL
; then sees exactly the real-mode vector table the firmware gave us,
; which is the El Torito contract we promise in interface/boot_handoff.h.

T_GDT           equ 0x1000          ; 40 bytes
T_FAULT         equ 0x1100          ; 12 bytes
T_IDT           equ 0x2000          ; 4096 bytes
T_STUB          equ 0x6000          ; transient move stub

start:
        cli
        cld
        xor     ax, ax
        mov     ds, ax                      ; labels are linear
        mov     es, ax

        mov     si, s_core
        call    ser_str
        mov     al, 'F'                     ; F3: flatten start
        call    vga_mark

        ; ---- 0. validate the packer patch block --------------------
        ;       {0x7DA0 magic, +4 ocbl_size, +8 stage1+core size}
        cmp     dword [0x7DA0], 0x4D52434F   ; 'OCRM'
        jne     .no_payload
        mov     eax, [0x7DA4]                ; OCBL.BIN byte size
        mov     [P_CNT], eax
        mov     eax, [0x7DA8]                ; stage1+core (padded)
        add     eax, 0x7C00                  ; = OCBL.BIN linear base
        mov     [P_SRC], eax
        jmp     .patch_ok
.no_payload:
        mov     si, s_nopay
        call    ser_str
.hang_p:
        hlt
        jmp     .hang_p
.patch_ok:

        ; ---- 1. A20 force-on ---------------------------------------
        mov     ax, 0x2401
        int     0x15
        call    a20_check
        jnz     .a20_on
        in      al, 0x92
        or      al, 0x02
        and     al, 0xFE                     ; bit 0 = fast reset: never
        out     0x92, al
        call    a20_check
        jnz     .a20_on
        mov     si, s_a20
        call    ser_str
.hang_a:
        hlt
        jmp     .hang_a
.a20_on:
        mov     si, s_a20on
        call    ser_str
        mov     al, 'A'                     ; F3: A20 verified on
        call    vga_mark

        ; ---- 2. temp GDT at 0x1000, fault stub at 0x1100, ---------
        ;          256-gate IDT at 0x2000
        mov     si, gdt_template
        mov     di, T_GDT
        mov     cx, 20                       ; 40 bytes
        rep movsw
        mov     si, fault_bytes
        mov     di, T_FAULT
        mov     cx, 6                        ; 12 bytes
        rep movsw
        ; 256 sixteen-byte gates: off_lo=0x1100 sel=0x08 ist=0
        ; type=0x8E, off_mid/off_hi/rsvd zero.  64-bit-format gates:
        ; fully valid in the long-mode window; in the 32-bit window
        ; the odd vectors escalate via #DF (gate 8 reads the even
        ; half) to the same marked halt instead of a reset loop.
        mov     di, T_IDT
        mov     cx, 256
.gate:
        mov     word [es:di+0], T_FAULT
        mov     word [es:di+2], 0x0008
        mov     byte [es:di+4], 0
        mov     byte [es:di+5], 0x8E
        mov     word [es:di+6], 0
        mov     dword [es:di+8], 0
        add     di, 16
        loop    .gate
        ; SAVE the firmware's real-mode IDTR first - the move stub
        ; restores it before handing off (see P_IDTR).  Read before
        ; our own LIDT overwrites it.
        sidt    [P_IDTR]
        lgdt    [gdtr]
        lidt    [idtr]
        mov     si, s_idt
        call    ser_str
        mov     al, 'G'                     ; F3: GDT + 256-gate IDT
        call    vga_mark

        ; ---- 3. 8259A remap to 0x08/0x70, all masked ---------------
        mov     al, 0x11                     ; ICW1: init, cascade, ICW4
        out     0x20, al
        out     0xA0, al
        mov     al, 0x08                     ; ICW2 master vector base
        out     0x21, al
        mov     al, 0x70                     ; ICW2 slave vector base
        out     0xA1, al
        mov     al, 0x04                     ; ICW3 master: slave at IRQ2
        out     0x21, al
        mov     al, 0x02                     ; ICW3 slave identity
        out     0xA1, al
        mov     al, 0x01                     ; ICW4: 8086 mode
        out     0x21, al
        out     0xA1, al
        mov     al, 0xFF                     ; OCW1: mask everything
        out     0x21, al
        out     0xA1, al
        mov     si, s_pic
        call    ser_str
        mov     al, 'P'                     ; F3: 8259A remapped+masked
        call    vga_mark

        ; ---- 4. unreal-mode dance: flat 4G DS/ES caches, base 0 ----
        ;          (CS untouched - the classic mixed-state dance; IF=0
        ;          held, IVT never referenced)
        mov     eax, cr0
        or      eax, 1
        mov     cr0, eax
        mov     bx, 0x10                     ; data32 flat from T_GDT
        mov     ds, bx
        mov     es, bx
        mov     eax, cr0
        and     eax, 0xFFFFFFFE
        mov     cr0, eax
        ; real mode again, DS/ES caches flat 4G base 0 - labels stay
        ; linear-correct AND MMIO above 1 MiB is reachable.

        ; ---- 4b. IOAPIC: read the RTE count, mask every entry ------
        mov     eax, 1                       ; IOREGSEL = version reg
        mov     [dword IOAPIC_BASE], eax
        mov     eax, [dword IOAPIC_BASE+0x10]
        shr     eax, 16
        and     eax, 0xFF                    ; max redirection entry
        inc     eax                          ; = RTE count
        mov     [P_RTECNT], eax
        xor     ecx, ecx
.rte:
        mov     eax, ecx
        shl     eax, 1
        add     eax, 0x10                    ; low-half reg of RTE ecx
        mov     [dword IOAPIC_BASE], eax
        mov     eax, 0x00010000              ; masked, fixed, vector 0
        mov     [dword IOAPIC_BASE+0x10], eax
        mov     eax, ecx
        shl     eax, 1
        add     eax, 0x11                    ; high-half reg
        mov     [dword IOAPIC_BASE], eax
        xor     eax, eax                     ; destination 0
        mov     [dword IOAPIC_BASE+0x10], eax
        inc     ecx
        cmp     ecx, [P_RTECNT]
        jb      .rte
        mov     si, s_ioapic
        call    ser_str
        mov     eax, [P_RTECNT]
        call    ser_dec
        mov     si, s_rte
        call    ser_str
        mov     al, 'I'                     ; F3: IOAPIC all-RTE masked
        call    vga_mark

        ; ---- 5. QEMU/firmware leftover flatten ----------------------
        mov     al, 0x20                     ; EOI both PICs
        out     0x20, al
        out     0xA0, al
        mov     cx, 16                       ; PS/2 output drain, bounded
.ps2:
        in      al, 0x64
        test    al, 1
        jz      .ps2done
        in      al, 0x60
        loop    .ps2
.ps2done:
        mov     al, 0x0F                     ; DMA mask all channels
        out     0x0F, al                     ;  controller 1 (0-3)
        out     0xDE, al                     ;  controller 2 (4-7)
        mov     si, s_flat
        call    ser_str
        mov     al, 'L'                     ; F3: leftovers flattened
        call    vga_mark

        ; ---- 6. the OCBL move: stub at 0x6000, backward copy -------
        mov     si, stub_bytes
        mov     di, T_STUB
        mov     cx, (stub_end - stub_bytes + 1) / 2
        rep movsw
        mov     si, s_move
        call    ser_str
        mov     eax, [P_CNT]
        call    ser_dec
        mov     si, s_bytes
        call    ser_str
        mov     al, 'M'                     ; F3: the OCBL move begins
        call    vga_mark
        jmp     0x0600:0x0000                ; into the move stub (CS=0x600 -> linear 0x6000)

; ---------------------------------------------------------------------------
; a20_check: ZF=0 when A20 is ON.  OFF only if BOTH patterns alias
; (the WP05 B1 double-pattern discipline; 0x500/0xFFFF:0x0510).
; ---------------------------------------------------------------------------
a20_check:
        push    es
        push    bx
        mov     ah, [0x0500]                 ; original byte (DS=0)
        mov     al, ah
        xor     al, 0x55                     ; pattern 1
        mov     [0x0500], al
        push    ax                           ; {orig, p1}
        mov     bx, 0xFFFF
        mov     es, bx
        mov     bl, [es:0x0510]              ; alias read 1
        xor     ax, ax
        mov     es, ax
        pop     ax                           ; al = p1
        cmp     bl, al
        jne     .on                          ; no alias: definitely ON
        mov     al, ah
        xor     al, 0xAA                     ; pattern 2
        mov     [0x0500], al
        push    ax
        mov     bx, 0xFFFF
        mov     es, bx
        mov     bl, [es:0x0510]              ; alias read 2
        xor     ax, ax
        mov     es, ax
        pop     ax                           ; al = p2
        cmp     bl, al
        je      .off                         ; both aliased: really OFF
.on:
        mov     [0x0500], ah                 ; restore original
        mov     bx, 1                        ; ZF=0 -> ON
        jmp     .flags
.off:
        mov     [0x0500], ah
        xor     bx, bx                       ; ZF=1 -> OFF
.flags:
        test    bx, bx
        pop     bx
        pop     es
        ret

; ---------------------------------------------------------------------------
; serial helpers (COM1 polled; DS base 0 - labels linear)
; ---------------------------------------------------------------------------
; ser_str: print DS:SI NUL string to COM1 - BOUNDED (WP-boot-stack F1).
; A dead/blocked line burns the per-char budget once, is declared
; dead, and every later print returns immediately - a broken serial
; console must never hang the flatten.
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

; ser_dec: print EAX as unsigned decimal (flat DS, works pre/post
; dance) - F1 bounded (bh is the per-digit budget; ebx only carries
; the constant divisor 10 here, so bh is free). On timeout the pushed
; digits are drained so the stack stays balanced.
ser_dec:
        push    eax
        push    ebx
        push    ecx
        push    edx
        cmp     byte [ser_dead], 1
        je      .out
        mov     ebx, 10
        xor     ecx, ecx
.div:
        xor     edx, edx
        div     ebx
        push    edx
        inc     ecx
        test    eax, eax
        jnz     .div
.pr:
        pop     eax
        add     al, '0'
        mov     bl, al
        mov     bh, 0xFF                   ; per-digit wait budget
        mov     dx, COM1+5
.w2:
        in      al, dx
        test    al, 0x20
        jnz     .go2
        dec     bh
        jnz     .w2
        mov     byte [ser_dead], 1         ; timeout: line declared dead
        dec     cx                          ; this iteration did not finish
.drain:                                     ; unwind the pushed digits so
        pop     eax                         ; the stack stays balanced on
        loop    .drain                      ; the failure return path
        jmp     .out
.go2:
        mov     dx, COM1
        mov     al, bl
        out     dx, al
        loop    .pr
.out:
        pop     edx
        pop     ecx
        pop     ebx
        pop     eax
        ret

; ---------------------------------------------------------------------------
; F3: vga_mark - AL = char -> VGA text row 24, next column, attr 0x0F.
; The stage1 marks 'R'/'B' own columns 0-1; the core continues at 2.
; Preserves every register; works with the linear DS discipline.
; WP-compat-and-security-hardening B4: the marks are OFF in the
; default build (the SBFAJPILM residue on row 24 was debug-only);
; -DOCRMECL_VGA_MARKS from the makefile re-enables the real body.
; The call sites stay as-is either way (a call + ret is free).
; ---------------------------------------------------------------------------
%ifdef OCRMECL_VGA_MARKS
vga_mark:
        push    bx
        push    ax
        mov     bx, 0xB800
        mov     fs, bx                     ; FS only - NEVER ES/DS: after
        mov     bl, [vga_col]              ; the unreal dance ES carries the
        xor     bh, bh                     ; flat 4G cache and a real-mode
        shl     bx, 1                      ; ES reload (pop es!) would reset
        mov     ah, 0x0F                   ; its base, misdirecting the move
        mov     [fs:bx+0x0F00], ax         ; stub's a32 rep movsb
        inc     byte [vga_col]
        pop     ax
        pop     bx
        ret
vga_col:        db 2                        ; stage1 wrote columns 0-1
%else
vga_mark:
        ret
%endif
ser_dead:       db 0                        ; 1 = COM1 declared dead

; ---------------------------------------------------------------------------
; the move stub: copied to linear 0x6000 (CS=0x0060), runs with the
; flat DS/ES caches from the dance.  Backward copy so the overlapping
; destination never clobbers unread source; the stub itself lives below
; 0x7C00 and survives the whole move.
; ---------------------------------------------------------------------------
stub_bytes:
        ; FORWARD copy: dst (0x7C00) < src (stage1+core boundary) - the
        ; memmove direction rule.  The write pointer trails the read
        ; pointer by exactly (src-dst) bytes, so a source byte is read
        ; before any write can touch it; the stub itself lives below
        ; 0x7C00 and is never in either range.  (LIVE-CAUGHT: the first
        ; implementation copied BACKWARD - each iteration then read a
        ; source byte an earlier write had already clobbered, a
        ; cascading self-corruption that put stub fragments at 0x7C00.)
        cli
        cld
        mov     esi, [dword P_SRC]
        mov     edi, 0x7C00                  ; dst: a design constant
        mov     ecx, [dword P_CNT]
        a32 rep movsb                         ; ascending, flat caches
        ; Restore the firmware's REAL-MODE IDTR before the jump: OCBL
        ; stage1 runs in real mode (4-byte IVT entries), and the
        ; 16-byte-gate table we installed at 0x2000 is unreadable in
        ; that format.  DS/ES are flat base-0 caches here, so P_IDTR
        ; is linear-addressable.  (Root cause of the QEMU 10.2.1
        ; serial_ioport_write abort on the BIOS track.)
        lidt    [P_IDTR]
        mov     dl, [P_DRIVE]                ; boot drive passthrough
        jmp     0x0000:0x7C00                ; the El Torito contract
stub_end:

; ---------------------------------------------------------------------------
; data
; ---------------------------------------------------------------------------
s_core:         db "OCRMECL core: flatten start", 13, 10, 0
s_nopay:        db "OCRMECL: no OCBL payload (bad patch block)", 13, 10, 0
s_a20on:        db "OCRMECL: A20 on", 13, 10, 0
s_a20:          db "OCRMECL: A20 stuck off", 13, 10, 0
s_idt:          db "OCRMECL: gdt+idt(256 gates) at 0x1000/0x2000", 13, 10, 0
s_pic:          db "OCRMECL: 8259A remapped+masked", 13, 10, 0
s_ioapic:       db "OCRMECL: IOAPIC masked (", 0
s_rte:          db " rte)", 13, 10, 0
s_flat:         db "OCRMECL: leftovers flattened (eoi ps2 dma)", 13, 10, 0
s_move:         db "OCRMECL: moving OCBL ", 0
s_bytes:        db " bytes -> 0x7C00, handing off", 13, 10, 0

align 8
gdt_template:                                   ; -> copied to 0x1000
        dq 0                                    ; 0x00 null
        dw 0xFFFF, 0x0000                       ; 0x08 code32: limit, base
        db 0x00, 0x9A, 0xCF, 0x00               ;  base_mid, access, gran
        dw 0xFFFF, 0x0000                       ; 0x10 data32: limit, base
        db 0x00, 0x92, 0xCF, 0x00               ;  base_mid, access, gran

gdtr:   dw 23                                  ; 3 entries - 1
        dd T_GDT

idtr:   dw 0xFFF
        dd T_IDT

fault_bytes:                                    ; -> copied to 0x1100
        db 0x66, 0xBA, 0xF8, 0x03              ; mov dx, 0x03F8
        db 0xB0, 0x21                           ; mov al, '!'
        db 0xEE                                 ; out dx, al
        db 0xFA                                 ; cli
        db 0xF4                                 ; hlt
        db 0xEB, 0xFD                           ; jmp -2
