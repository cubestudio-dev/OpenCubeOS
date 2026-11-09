; SPDX-License-Identifier: Apache-2.0
; Copyright 2026 cubestudio-dev <cubestudio@qq.com>
;
; WP-10-AUDIT_P2-fix3 G6 regression (BUG-0279b): fork under address-space
; copy OOM must FAIL, not spawn a half-copied child.
;
;   1. The parent mmaps ~60 MiB (15,360 pages, inside the per-process
;      mmap window below the stack region).
;   2. It then forks up to 8 times. Every child parks on a pipe read
;      (the write end stays open in the parent), so each fork must deep-
;      copy the full ~60 MiB image. On a 512 MiB machine the PMM runs
;      out during one of the copies.
;         fixed:   copy_user_address_space tears the partial copy down
;                  and fork returns -1 -> no child, parent counts the
;                  clean failure, PMM still consistent.
;         bug:     the partial AS was returned as success -> the child
;                  runs on a broken image; when the parent later closes
;                  the pipe the child wakes, touches its (uncopied) top
;                  stack page and dies by #PF exception (wait4 status
;                  128+14 = 142).
;   3. Parent closes the write end, wait4()s every child and requires
;      status == 0 from each; then munmaps its 60 MiB and verifies PMM
;      still services an mmap (no leak from the failure path).
;
; PASS  = every spawned child reported status 0, PMM healthy afterwards,
;         and at least one fork failed cleanly with -1.
; FAIL  = any child status != 0 (half-product child died), or the
;         post-OOM mmap fails (frames leaked by the failure path).
; NOTE  = if the box had RAM for all 8 children the run prints
;         "inconclusive" and exits 0 (assertion not exercised).
bits 64
global _start

%define SYS_EXIT     0
%define SYS_WRITE    1
%define SYS_CLOSE    4
%define SYS_MKDIR    7
%define SYS_FORK     10
%define SYS_WAIT4    12
%define SYS_EXIT2    16
%define SYS_MMAP     30
%define SYS_MUNMAP   31
%define SYS_PIPE     20
%define SYS_READ     70
%define SYS_WRITE2   72

%define MMAP_PAGES   15360        ; 60 MiB, ends at 0x3FCC0000 < 0x3FFF0000
%define MAX_FORKS    8

section .text
print_str:
    mov rdx, rsi
    mov rsi, rdi
    mov rdi, 1
    mov rax, SYS_WRITE
    int 0x80
    ret

print_u64:
    sub rsp, 24
    lea rcx, [rsp+20]
    mov byte [rcx], 10
    inc rcx
    mov rsi, 10
    mov rax, rdi
    test rax, rax
    jnz .pu_loop
    dec rcx
    mov byte [rcx], '0'
    jmp .pu_print
.pu_loop:
    test rax, rax
    jz .pu_print
    xor rdx, rdx
    div rsi
    add dl, '0'
    dec rcx
    mov [rcx], dl
    jmp .pu_loop
.pu_print:
    lea rdx, [rsp+21]
    sub rdx, rcx
    mov rsi, rcx
    mov rdi, 1
    mov rax, SYS_WRITE
    int 0x80
    add rsp, 24
    ret

_start:
    lea rdi, [rel start_msg]
    mov esi, start_len
    call print_str

    ; ---- the park pipe: children block on its read end forever ----
    sub rsp, 16
    mov rdi, rsp
    mov rax, SYS_PIPE
    int 0x80
    test rax, rax
    jnz setup_fail_sp
    mov r13d, [rsp]              ; park read fd
    mov r14d, [rsp+4]            ; park write fd
    add rsp, 16
    mov [rel park_r], r13
    mov [rel park_w], r14

    ; ---- allocate the 60 MiB image the children must copy ----
    xor r15, r15                 ; page counter
    xor r12, r12                 ; first mmap address
.alloc:
    mov rax, SYS_MMAP
    xor rdi, rdi
    mov rsi, 4096
    xor rdx, rdx
    int 0x80
    test rax, rax
    jz setup_fail
    test r12, r12
    jnz .alloc_have
    mov r12, rax
.alloc_have:
    mov byte [rax], 0x5A         ; touch every page so it is real
    inc r15
    cmp r15, MMAP_PAGES
    jb .alloc
    mov [rel mmap_first], r12

    lea rdi, [rel setup_msg]
    mov esi, setup_len
    call print_str
    mov rdi, r15
    call print_u64

    ; ---- fork until an OOM failure or MAX_FORKS children ----
    xor r15, r15                 ; children spawned
    xor rbx, rbx                 ; forks attempted
    xor r13, r13                 ; clean fork failures (-1)
.forkloop:
    cmp rbx, MAX_FORKS
    jae g6_forkdone
    inc rbx
    mov rax, SYS_FORK
    int 0x80
    cmp rax, -1
    jne .forked
    inc r13
    lea rdi, [rel forkfail_msg]
    mov esi, forkfail_len
    call print_str
    jmp .forkloop
.forked:
    test rax, rax
    jz .child
    inc r15                      ; parent: one more parked child
    jmp .forkloop                ; parent: next fork
.child:
    ; ---- child: park on the pipe, then prove the image is complete ----
    mov rdi, [rel park_w]
    mov rax, SYS_CLOSE
    int 0x80                     ; parent's close is the only EOF source
    mov rdi, [rel park_r]
    sub rsp, 32
    mov rsi, rsp
    mov rdx, 8
    mov rax, SYS_READ
    int 0x80                     ; blocks until parent closes park_w
    ; touch the top user stack page (the LAST region a partial copy
    ; reaches) - pre-fix this is an unmapped page -> #PF -> status 142
    push rax
    pop rax
    ; touch this child's own copy of the mmap region
    mov rbx, [rel mmap_first]
    cmp byte [rbx], 0x5A
    jne child_bad
    add rsp, 32
    lea rdi, [rel childok_msg]
    mov esi, childok_len
    call print_str
    mov rax, SYS_EXIT2
    xor rdi, rdi
    int 0x80
child_bad:
    add rsp, 32
    lea rdi, [rel childbad_msg]
    mov esi, childbad_len
    call print_str
    mov rax, SYS_EXIT2
    mov rdi, 5
    int 0x80

g6_forkdone:
    ; ---- parent: wake the parked children, collect every status ----
    mov rdi, [rel park_w]
    mov rax, SYS_CLOSE
    int 0x80
    mov rdi, [rel park_r]
    mov rax, SYS_CLOSE
    int 0x80
    xor r14, r14                 ; bad statuses
.collect:
    cmp r15, 0
    je .collected
    sub rsp, 16
    mov rax, SYS_WAIT4
    xor rdi, rdi
    mov rsi, rsp
    xor rdx, rdx
    int 0x80
    cmp rax, -1
    je .collect_done_sp
    cmp dword [rsp], 0
    je .collect_next
    inc r14
    lea rdi, [rel baddone_msg]
    mov esi, baddone_len
    call print_str
    mov edi, [rsp]
    call print_u64
.collect_next:
    add rsp, 16
    dec r15
    jmp .collect
.collect_done_sp:
    add rsp, 16
.collected:
    ; ---- free the 60 MiB and verify PMM still works ----
    mov rdi, [rel mmap_first]
    mov rsi, MMAP_PAGES * 4096
    mov rax, SYS_MUNMAP
    int 0x80
    mov rax, SYS_MMAP
    xor rdi, rdi
    mov rsi, 4096
    xor rdx, rdx
    int 0x80
    mov r12, rax
    test rax, rax
    jz oom_fail
    mov rax, SYS_MUNMAP
    mov rdi, r12
    mov rsi, 4096
    int 0x80

    ; ---- verdict ----
    test r14, r14
    jnz oom_fail
    test r13, r13
    jz .inconclusive
    lea rdi, [rel ok_msg]
    mov esi, ok_len
    call print_str
    mov rdi, r13
    call print_u64
    lea rdi, [rel pass_msg]
    mov esi, pass_len
    call print_str
    mov rax, SYS_EXIT2
    xor rdi, rdi
    int 0x80
.inconclusive:
    lea rdi, [rel inconc_msg]
    mov esi, inconc_len
    call print_str
    mov rax, SYS_EXIT2
    xor rdi, rdi
    int 0x80
oom_fail:
    lea rdi, [rel fail_msg]
    mov esi, fail_len
    call print_str
    mov rax, SYS_EXIT2
    mov rdi, 1
    int 0x80
setup_fail_sp:
    add rsp, 16
setup_fail:
    lea rdi, [rel setupfail_msg]
    mov esi, setupfail_len
    call print_str
    mov rax, SYS_EXIT2
    xor rdi, rdi
    int 0x80

section .data
park_r:       dq 0
park_w:       dq 0
mmap_first:   dq 0

section .rodata
start_msg:     db "g6_oom: start", 10
start_len      equ $ - start_msg
setup_msg:     db "g6_oom: mmap pages = "
setup_len      equ $ - setup_msg
forkfail_msg:  db "g6_oom: fork failed cleanly (-1)", 10
forkfail_len   equ $ - forkfail_msg
childok_msg:   db "g6_oom: child image intact", 10
childok_len    equ $ - childok_msg
childbad_msg:  db "g6_oom: child image INCOMPLETE", 10
childbad_len   equ $ - childbad_msg
baddone_msg:   db "g6_oom: child exited abnormal, status = "
baddone_len    equ $ - baddone_msg
ok_msg:        db "g6_oom: clean fork failures = "
ok_len         equ $ - ok_msg
pass_msg:      db "g6_oom: PASS", 10
pass_len       equ $ - pass_msg
inconc_msg:    db "g6_oom: inconclusive - no fork OOM in 8 tries", 10
inconc_len     equ $ - inconc_msg
fail_msg:      db "g6_oom: FAIL", 10
fail_len       equ $ - fail_msg
setupfail_msg: db "g6_oom: setup failed", 10
setupfail_len  equ $ - setupfail_msg
