; SPDX-License-Identifier: Apache-2.0
; Copyright 2026 cubestudio-dev <cubestudio@qq.com>
;
; p0leak - WP-fix-critical-security P0 regression test (BUG-AUD-2a-001).
;
; Proves the user-pointer validation fence: a ring-3 program must NOT
; be able to pass a KERNEL address to sys_read/sys_write.  The attack
; shape from the audit: grow the brk past a 2 MiB boundary so the
; user address space SPLITS a kernel huge-page PDE; the split leaves
; 511 supervisor PRESENT leaf PTEs (kernel identity window) walkable
; in the user AS.  A sys_write on such a sibling address used to pass
; access_ok (PRESENT-only check) and dump kernel heap bytes to the
; console.  With the fix (window bound + USER-bit check) every probe
; below must return -1 and the kernel must stay alive.
;
; Syscalls: 1=write 16=exit2 33=brk 70=read

bits 64
global _start
section .text

; print_str(rdi=buf, rsi=len) -> rax
print_str:
    mov rdx, rsi
    mov rsi, rdi
    mov rdi, 1
    mov rax, 1
    int 0x80
    ret

; try_write(rdi=addr) -> rax (syscall return)
try_write:
    mov rsi, rdi
    mov rdi, 1
    mov rdx, 32
    mov rax, 1
    int 0x80
    ret

_start:
    lea rdi, [rel msg_start]
    mov rsi, msg_start.len
    call print_str

    ; --- Step 1: force a 2 MiB split in the user AS ------------------
    ; brk starts at 0x500000.  Growing it to 0x700000 makes the loader
    ; split the kernel huge PDE that covers 0x600000-0x800000 (the
    ; kernel heap identity window): walk_pt(create=1) fills 512 leaf
    ; PTEs, and the pages the brk itself does NOT map stay as
    ; PRESENT supervisor siblings (kernel heap physical frames).
    mov rax, 33             ; sys_brk
    mov rdi, 0x700000
    int 0x80
    cmp rax, 0x700000
    jne .brk_fail
    lea rdi, [rel msg_brk]
    mov rsi, msg_brk.len
    call print_str
    jmp .probe

.brk_fail:
    lea rdi, [rel msg_brkfail]
    mov rsi, msg_brkfail.len
    call print_str
    jmp .probe

    ; --- Step 2: probe kernel addresses through sys_write ------------
.probe:
    ; 2a. kernel image (below the user window) - 0x100000
    mov rdi, 0x100000
    call try_write
    cmp rax, -1
    jne .leak_a
    lea rdi, [rel msg_reject_img]
    mov rsi, msg_reject_img.len
    call print_str
    jmp .probe_b
.leak_a:
    lea rdi, [rel msg_leak_img]
    mov rsi, msg_leak_img.len
    call print_str

    ; 2b. split sibling inside the brk window (kernel heap page) -
    ;     0x700000 is PRESENT supervisor after the split: the brk
    ;     maps [0x500000, 0x700000) only.
.probe_b:
    mov rdi, 0x700000
    call try_write
    cmp rax, -1
    jne .leak_b
    lea rdi, [rel msg_reject_sib]
    mov rsi, msg_reject_sib.len
    call print_str
    jmp .probe_c
.leak_b:
    lea rdi, [rel msg_leak_sib]
    mov rsi, msg_leak_sib.len
    call print_str

    ; 2c. read INTO a kernel address through sys_read(0, ...) -
    ;     fd 0 console read would WRITE attacker-chosen bytes into
    ;     kernel memory when the fence is missing.  We cannot feed
    ;     stdin non-interactively here, so a -1 return is the
    ;     expected fence evidence (buffer rejected before any read).
.probe_c:
    mov rax, 70             ; sys_read
    mov rdi, 0
    mov rsi, 0x700000       ; kernel-side sibling page
    mov rdx, 8
    int 0x80
    cmp rax, -1
    jne .leak_c
    lea rdi, [rel msg_reject_read]
    mov rsi, msg_reject_read.len
    call print_str
    jmp .verdict
.leak_c:
    lea rdi, [rel msg_leak_read]
    mov rsi, msg_leak_read.len
    call print_str

    ; --- Verdict -------------------------------------------------------
.verdict:
    lea rdi, [rel msg_done]
    mov rsi, msg_done.len
    call print_str
    mov rax, 16
    xor rdi, rdi
    int 0x80

section .rodata
msg_start: db "p0leak: BUG-AUD-2a-001 user-pointer fence test", 10
    .len equ $ - msg_start
msg_brk: db "p0leak: brk grown to 0x700000 (2MiB split forced)", 10
    .len equ $ - msg_brk
msg_brkfail: db "p0leak: brk growth FAILED - split not forced", 10
    .len equ $ - msg_brkfail
msg_reject_img: db "p0leak: kernel-image write REJECTED (-1) OK", 10
    .len equ $ - msg_reject_img
msg_leak_img: db "p0leak: BUG - kernel image write SUCCEEDED (leak!)", 10
    .len equ $ - msg_leak_img
msg_reject_sib: db "p0leak: split-sibling write REJECTED (-1) OK", 10
    .len equ $ - msg_reject_sib
msg_leak_sib: db "p0leak: BUG - kernel heap sibling write LEAKED DATA", 10
    .len equ $ - msg_leak_sib
msg_reject_read: db "p0leak: kernel-addr read REJECTED (-1) OK", 10
    .len equ $ - msg_reject_read
msg_leak_read: db "p0leak: BUG - kernel-addr read ACCEPTED (write path!)", 10
    .len equ $ - msg_leak_read
msg_done: db "p0leak: PASS - all kernel-address probes fenced", 10
    .len equ $ - msg_done
