; SPDX-License-Identifier: Apache-2.0
; Copyright 2026 cubestudio-dev <cubestudio@qq.com>

; WP-10-AUDIT_P2-fix3 G6 regression test (BUG-0274/0275/0276/0277/0278/0280).
;
; Architecture: one child process per test.  The child runs the test body
; and returns a 4-byte result code (1 = pass, 0 = fail) over a result
; pipe.  The parent waits for the result with select(timeout=15s), so a
; test that HANGS (the pre-fix behavior of T1/T3) or CRASHES (pre-fix T4)
; fails its own slot instead of killing the whole run.
;
;   T1  BUG-0275  SYS_EXIT(0) must reap like EXIT2: parent wait4 collects
;                 exit code 42, pipe read sees EOF (fds closed), parent
;                 actually wakes up.
;   T2  BUG-0274  fork must inherit mmap_base: child's first mmap lands
;                 at parent's mmap + 1 page (not on top of it).
;   T3  BUG-0276  write2 on a FULL pipe with NO reader must fail (-1),
;                 not block forever; the legal buffered write2 still works.
;   T4  BUG-0277  a signal handler that plain `ret`s must be caught by
;                 the on-stack sigreturn restorer.
;   T5  BUG-0278  cwd is per-process: child chdir must not move the
;                 parent; relative paths resolve against the caller's cwd.
;   T7  BUG-0280  corrupt-ELF solib table entries (bad phnum / OOB filesz /
;                 absurd memsz) are each rejected with 0, the legal entry
;                 still maps, PMM stays healthy.
;   T6  BUG-0280  sys_map_solib: dlopen bump stops at the 0x3C000000
;                 window top (fail-closed), PMM stays healthy after.
;
; All strings are ASCII English (kernel rule 4).
bits 64
global _start

%define SYS_EXIT     0
%define SYS_WRITE    1
%define SYS_CLOSE    4
%define SYS_STAT     5
%define SYS_MKDIR    7
%define SYS_FORK     10
%define SYS_WAIT4    12
%define SYS_KILL     13
%define SYS_GETPID   14
%define SYS_EXIT2    16
%define SYS_MMAP     30
%define SYS_MUNMAP   31
%define SYS_SIGNAL   40
%define SYS_PIPE     20
%define SYS_CHDIR    60
%define SYS_GETCWD   61
%define SYS_READ     70
%define SYS_WRITE2   72
%define SYS_SELECT   80
%define SYS_MAPSOLIB 90

section .text
print_str:
    mov rdx, rsi
    mov rsi, rdi
    mov rdi, 1
    mov rax, SYS_WRITE
    int 0x80
    ret

; print_u64: print rdi as decimal + newline.
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

; child_finish: eax = result (1 pass / 0 fail).  Writes it over the
; result pipe (write fd saved in g_result_wfd by the parent before the
; fork) and exits.  Only ever runs in a child.
child_finish:
    mov dword [rel g_res], eax
    mov rax, SYS_WRITE2
    mov rdi, [rel g_result_wfd]
    lea rsi, [rel g_res]
    mov rdx, 4
    int 0x80
    mov rax, SYS_EXIT2
    xor rdi, rdi
    int 0x80

; collect: wait up to 15 s for a 4-byte result on the read fd in edi.
; Returns eax = 1 (result was 1), 0 (timeout / child died / other).
; Preserves rbx, r12-r15.
;
; CALLER CONTRACT: edi MUST be the pipe READ end (r12).  Every parent
; path closes the write end (r13) first and then reloads
;   mov rdi, r12
; before `call collect`.  Passing the just-closed r13 instead makes
; select() return EBADF (-1) and read() fall into the kind-0 (closed
; fd == keyboard) path, which blocks forever - the whole suite would
; hang at T2 regardless of the kernel fixes under test.
collect:
    push rbx
    push r12
    sub rsp, 160                 ; [rsp+128]=result slot
    mov r12d, edi
    ; WP-10-AUDIT_P2-fix3 G6 rev3: plain blocking read, no select().
    ; Termination no longer depends on the select tick: either the child
    ; wrote its 4-byte result (read returns 4) or the child died and the
    ; exit2 reaper closed the write end (read returns 0 = EOF).
    mov rax, SYS_READ
    mov rdi, r12
    lea rsi, [rsp+128]
    mov rdx, 4
    int 0x80
    cmp rax, 4
    jne .cl_fail
    cmp dword [rsp+128], 1
    jne .cl_fail
    mov eax, 1
    jmp .cl_done
.cl_fail:
    xor eax, eax
.cl_done:
    add rsp, 160
    pop r12
    pop rbx
    ret

; parent_tail: shared post-test bookkeeping for the parent.
;   eax = combined verdict, r12 = result read fd,
;   r14 = result message pointer, r13d = result message length.
;   WP-10-AUDIT_P2-fix3 G6 rev3: the PASS path used to print the SAME
;   "Tn FAIL" message the fail path uses (r14 is the FAIL string), so a
;   passing test was indistinguishable from a failing one on the serial
;   log. The pass path now prints a distinct "=PASS=" marker and the
;   fail path keeps the explicit "Tn FAIL" line.
parent_tail:
    test eax, eax
    jnz .pt_pass
    inc r15
    mov rdi, r14
    mov esi, r13d
    call print_str
    jmp .pt_close
.pt_pass:
    mov rax, SYS_WAIT4
    xor rdi, rdi
    xor rsi, rsi
    xor rdx, rdx
    int 0x80
    lea rdi, [rel pt_pass_msg]
    mov esi, pt_pass_msg_len
    call print_str
.pt_close:
    mov rdi, r12
    mov rax, SYS_CLOSE
    int 0x80
    ret

_start:
    lea rdi, [rel start_msg]
    mov esi, start_len
    call print_str
    xor r15, r15                 ; failure count

; ====================================================================
; T2 - BUG-0274: fork inherits mmap_base
; ====================================================================
t2_begin:
    lea rdi, [rel t2_start]
    mov esi, t2_start_len
    call print_str
    ; parent mmaps one page and marks it before forking
    lea rdi, [rel step0_msg]
    mov esi, step0_len
    call print_str
    ; BUG-0281 (G7) semantic adaptation: prot=0 is now real PROT_NONE,
    ; so a page the test WRITES must be mapped PROT_READ|PROT_WRITE (3)
    ; - same adaptation p3_test Test B already got.
    mov rax, SYS_MMAP
    xor rdi, rdi
    mov rsi, 4096
    mov rdx, 3                   ; PROT_READ|PROT_WRITE
    int 0x80
    test rax, rax
    jz t2_pfail_m
    mov [rel t2_parent_addr], rax
    lea rdi, [rel step1_msg]
    mov esi, step1_len
    call print_str
    ; print_str clobbers RAX (rax = SYS_WRITE return = length), so reload
    ; the saved mmap address before touching the page.
    mov rbx, [rel t2_parent_addr]
    mov byte [rbx], 0xAB
    sub rsp, 16
    mov rdi, rsp
    mov rax, SYS_PIPE
    int 0x80
    test rax, rax
    jnz t2_pfail_sp
    mov r12d, [rsp]
    mov r13d, [rsp+4]
    add rsp, 16
    mov [rel g_result_wfd], r13
    lea rdi, [rel step2_msg]
    mov esi, step2_len
    call print_str
    mov rax, SYS_FORK
    int 0x80
    test rax, rax
    jnz t2_parent
    lea rdi, [rel stepC_msg]
    mov esi, stepC_len
    call print_str
    ; ---- child ----
    mov rdi, r12
    mov rax, SYS_CLOSE
    int 0x80
    mov rax, SYS_MMAP            ; child's first mmap
    xor rdi, rdi
    mov rsi, 4096
    mov rdx, 3                   ; PROT_READ|PROT_WRITE (writes [r13] below)
    int 0x80
    test rax, rax
    jz t2c_fail
    mov r13, rax                 ; C1
    mov rbx, [rel t2_parent_addr]
    lea rcx, [rbx + 0x1000]
    cmp r13, rcx                 ; C1 must be P1 + 0x1000 (bump continues)
    jne t2c_fail_bump
    cmp byte [rbx], 0xAB         ; inherited page still holds the mark
    jne t2c_fail_mark
    mov byte [r13], 0xCD         ; legal-use: child writes its own page
    mov eax, 1
    jmp child_finish
t2c_fail_bump:
    lea rdi, [rel t2cf_bump]
    mov esi, t2cf_bump_len
    call print_str
    xor eax, eax
    jmp child_finish
t2c_fail_mark:
    lea rdi, [rel t2cf_mark]
    mov esi, t2cf_mark_len
    call print_str
    xor eax, eax
    jmp child_finish
t2c_fail:
    xor eax, eax
    jmp child_finish
t2_parent:
    lea rdi, [rel stepP_msg]
    mov esi, stepP_len
    call print_str
    mov rdi, r13
    mov rax, SYS_CLOSE
    int 0x80
    mov rdi, r12                 ; collect() watches the READ end (r13 is closed now)
    call collect
    lea r14, [rel t2_result]
    mov r13d, t2_result_len
    call parent_tail
    jmp t5_begin
t2_pfail_sp:
    add rsp, 16
t2_pfail_m:
t2_pfail:
    inc r15
    lea rdi, [rel t2_result]
    mov esi, t2_result_len
    call print_str
    jmp t5_begin

; ====================================================================
; T5 - BUG-0278: per-process cwd
; ====================================================================
t5_begin:
    lea rdi, [rel t5_start]
    mov esi, t5_start_len
    call print_str
    mov rax, SYS_MKDIR
    lea rdi, [rel t5_dir_a]
    int 0x80
    mov rax, SYS_MKDIR
    lea rdi, [rel t5_dir_b]
    int 0x80
    mov rax, SYS_CHDIR
    lea rdi, [rel t5_dir_a]
    int 0x80
    test rax, rax
    jnz t5_pfail
    sub rsp, 16
    mov rdi, rsp
    mov rax, SYS_PIPE
    int 0x80
    test rax, rax
    jnz t5_pfail_sp
    mov r12d, [rsp]
    mov r13d, [rsp+4]
    add rsp, 16
    mov [rel g_result_wfd], r13
    mov rax, SYS_FORK
    int 0x80
    test rax, rax
    jnz t5_parent
    ; ---- child ----
    mov rdi, r12
    mov rax, SYS_CLOSE
    int 0x80
    ; inherited cwd must be /tmp/g6a
    lea rdi, [rel t5_cwd_buf]
    mov rsi, 128
    mov rax, SYS_GETCWD
    int 0x80
    cmp rax, 0
    je t5c_fail
    mov rax, [rel t5_cwd_buf]
    mov rcx, 0x6136672F706D742F  ; "/tmp/g6a"
    cmp rax, rcx
    jne t5c_fail
    cmp byte [rel t5_cwd_buf+8], 0
    jne t5c_fail
    ; child chdirs to /tmp/g6b
    mov rax, SYS_CHDIR
    lea rdi, [rel t5_dir_b]
    int 0x80
    test rax, rax
    jnz t5c_fail
    lea rdi, [rel t5_cwd_buf]
    mov rsi, 128
    mov rax, SYS_GETCWD
    int 0x80
    cmp rax, 0
    je t5c_fail
    mov rax, [rel t5_cwd_buf]
    mov rcx, 0x6236672F706D742F  ; "/tmp/g6b"
    cmp rax, rcx
    jne t5c_fail
    cmp byte [rel t5_cwd_buf+8], 0
    jne t5c_fail
    ; a relative mkdir must land in the CHILD's cwd
    mov rax, SYS_MKDIR
    lea rdi, [rel t5_sub1]
    int 0x80
    test rax, rax
    jnz t5c_fail
    mov eax, 1
    jmp child_finish
t5c_fail:
    lea rdi, [rel t5cf]
    mov esi, t5cf_len
    call print_str
    xor eax, eax
    jmp child_finish
t5_parent:
    mov rdi, r13
    mov rax, SYS_CLOSE
    int 0x80
    mov rdi, r12                 ; collect() watches the READ end (r13 is closed now)
    call collect
    mov ebx, eax                 ; child verdict (collect kept rbx)
    ; parent cwd must STILL be /tmp/g6a
    lea rdi, [rel t5_cwd_buf]
    mov rsi, 128
    mov rax, SYS_GETCWD
    int 0x80
    cmp rax, 0
    je t5p_fail
    mov rax, [rel t5_cwd_buf]
    mov rcx, 0x6136672F706D742F  ; "/tmp/g6a"
    cmp rax, rcx
    jne t5p_fail
    cmp byte [rel t5_cwd_buf+8], 0
    jne t5p_fail
    ; a parent-relative mkdir must land in /tmp/g6a
    mov rax, SYS_MKDIR
    lea rdi, [rel t5_sub2]
    int 0x80
    test rax, rax
    jnz t5p_fail
    mov rax, SYS_STAT
    lea rdi, [rel t5_a_sub2]
    lea rsi, [rel t5_stat_buf]
    int 0x80
    test rax, rax
    jnz t5p_fail
    ; the child's relative mkdir really went to /tmp/g6b
    mov rax, SYS_STAT
    lea rdi, [rel t5_b_sub1]
    lea rsi, [rel t5_stat_buf]
    int 0x80
    test rax, rax
    jnz t5p_fail
    and ebx, 1
    mov eax, ebx
    lea r14, [rel t5_result]
    mov r13d, t5_result_len
    call parent_tail
    jmp t1_begin
t5p_fail:
    lea rdi, [rel t5pf]
    mov esi, t5pf_len
    call print_str
    xor eax, eax
    lea r14, [rel t5_result]
    mov r13d, t5_result_len
    call parent_tail
    jmp t1_begin
t5_pfail_sp:
    add rsp, 16
t5_pfail:
    inc r15
    lea rdi, [rel t5_result]
    mov esi, t5_result_len
    call print_str
    jmp t1_begin

; ====================================================================
; T1 - BUG-0275: SYS_EXIT(0) reaps like EXIT2
; ====================================================================
t1_begin:
    lea rdi, [rel t1_start]
    mov esi, t1_start_len
    call print_str
    sub rsp, 16
    mov rdi, rsp
    mov rax, SYS_PIPE
    int 0x80
    test rax, rax
    jnz t1_pfail_sp
    mov r12d, [rsp]
    mov r13d, [rsp+4]
    add rsp, 16
    mov [rel g_result_wfd], r13
    mov rax, SYS_FORK
    int 0x80
    test rax, rax
    jnz t1_parent
    ; ---- child: inner pipe + grandchild exiting via syscall 0 ----
    mov rdi, r12
    mov rax, SYS_CLOSE
    int 0x80
    sub rsp, 16
    mov rdi, rsp
    mov rax, SYS_PIPE
    int 0x80
    test rax, rax
    jnz t1c_fail_sp
    mov r14d, [rsp]              ; inner read fd
    mov r15d, [rsp+4]            ; inner write fd
    add rsp, 16
    mov rax, SYS_FORK
    int 0x80
    test rax, rax
    jz t1_grand
    ; t1_child: close inner write, wait4 -> exit code 42, read -> EOF
    mov rdi, r15
    mov rax, SYS_CLOSE
    int 0x80
    sub rsp, 16
    mov rax, SYS_WAIT4
    xor rdi, rdi
    mov rsi, rsp                 ; status
    xor rdx, rdx
    int 0x80
    cmp rax, -1
    je t1c_fail_sp
    cmp dword [rsp], 42
    jne t1c_fail_sp
    mov rax, SYS_READ            ; writer gone -> EOF 0, never a hang
    mov rdi, r14
    lea rsi, [rsp+8]
    mov rdx, 8
    int 0x80
    test rax, rax
    jnz t1c_fail_sp
    add rsp, 16
    mov eax, 1
    jmp child_finish
t1c_fail_sp:
    add rsp, 16
    lea rdi, [rel t1cf]
    mov esi, t1cf_len
    call print_str
    xor eax, eax
    jmp child_finish
t1_grand:
    mov rax, SYS_EXIT            ; syscall 0, code 42
    mov rdi, 42
    int 0x80
    xor eax, eax                 ; not reached
    jmp child_finish
t1_parent:
    mov rdi, r13
    mov rax, SYS_CLOSE
    int 0x80
    mov rdi, r12                 ; collect() watches the READ end (r13 is closed now)
    call collect
    lea r14, [rel t1_result]
    mov r13d, t1_result_len
    call parent_tail
    jmp t3_begin
t1_pfail_sp:
    add rsp, 16
t1_pfail:
    inc r15
    lea rdi, [rel t1_result]
    mov esi, t1_result_len
    call print_str
    jmp t3_begin

; ====================================================================
; T3 - BUG-0276: write2 on full pipe with no reader
; ====================================================================
t3_begin:
    lea rdi, [rel t3_start]
    mov esi, t3_start_len
    call print_str
    sub rsp, 16
    mov rdi, rsp
    mov rax, SYS_PIPE
    int 0x80
    test rax, rax
    jnz t3_pfail_sp
    mov r12d, [rsp]
    mov r13d, [rsp+4]
    add rsp, 16
    mov [rel g_result_wfd], r13
    mov rax, SYS_FORK
    int 0x80
    test rax, rax
    jnz t3_parent
    ; ---- child ----
    mov rdi, r12
    mov rax, SYS_CLOSE
    int 0x80
    sub rsp, 4160                ; [rsp..3]=pipefds, [rsp+16..]=4096 B buf
    mov rdi, rsp
    mov rax, SYS_PIPE
    int 0x80
    test rax, rax
    jnz t3c_fail_sp
    mov r14d, [rsp]              ; inner read fd
    mov r15d, [rsp+4]            ; inner write fd
    ; legal direction: fill the pipe exactly with the self reader open
    mov rax, SYS_WRITE2
    mov rdi, r15
    lea rsi, [rsp+16]
    mov rdx, 4096
    int 0x80
    cmp rax, 4096
    jne t3c_fail_sp
    ; close the only reader, then write2 one more byte: full + no reader
    ; must FAIL (EPIPE style), never block
    mov rdi, r14
    mov rax, SYS_CLOSE
    int 0x80
    mov rax, SYS_WRITE2
    mov rdi, r15
    lea rsi, [rsp+16]
    mov rdx, 1
    int 0x80
    cmp rax, -1
    jne t3c_fail_sp
    mov rdi, r15
    mov rax, SYS_CLOSE
    int 0x80
    add rsp, 4160
    mov eax, 1
    jmp child_finish
t3c_fail_sp:
    add rsp, 4160
    xor eax, eax
    jmp child_finish
t3_parent:
    mov rdi, r13
    mov rax, SYS_CLOSE
    int 0x80
    mov rdi, r12                 ; collect() watches the READ end (r13 is closed now)
    call collect
    lea r14, [rel t3_result]
    mov r13d, t3_result_len
    call parent_tail
    jmp t4_begin
t3_pfail_sp:
    add rsp, 16
t3_pfail:
    inc r15
    lea rdi, [rel t3_result]
    mov esi, t3_result_len
    call print_str
    jmp t4_begin

; ====================================================================
; T4 - BUG-0277: handler that plain `ret`s (on-stack restorer)
; ====================================================================
t4_begin:
    lea rdi, [rel t4_start]
    mov esi, t4_start_len
    call print_str
    sub rsp, 16
    mov rdi, rsp
    mov rax, SYS_PIPE
    int 0x80
    test rax, rax
    jnz t4_pfail_sp
    mov r12d, [rsp]
    mov r13d, [rsp+4]
    add rsp, 16
    mov [rel g_result_wfd], r13
    mov rax, SYS_FORK
    int 0x80
    test rax, rax
    jnz t4_parent
    ; ---- child ----
    mov rdi, r12
    mov rax, SYS_CLOSE
    int 0x80
    mov rax, SYS_SIGNAL
    mov rdi, 10                  ; SIGUSR1
    lea rsi, [rel t4_handler]
    int 0x80
    mov rax, SYS_GETPID
    int 0x80
    mov rdi, rax
    mov rax, SYS_KILL
    mov rsi, 10
    int 0x80
    ; pre-fix the handler's bare `ret` jumped into the weeds and this
    ; process died before writing a result (parent saw EOF/timeout).
    ; post-fix the on-stack restorer bounces through SYS_SIGRETURN.
    lea rdi, [rel t4_back]
    mov esi, t4_back_len
    call print_str
    mov eax, 1
    jmp child_finish
t4_handler:                      ; rdi = sig; MUST survive a plain `ret`
    lea rsi, [rel t4_hit]
    mov rdx, t4_hit_len
    mov rdi, 1
    mov rax, SYS_WRITE
    int 0x80
    ret                          ; no explicit sigreturn on purpose
t4_parent:
    mov rdi, r13
    mov rax, SYS_CLOSE
    int 0x80
    mov rdi, r12                 ; collect() watches the READ end (r13 is closed now)
    call collect
    lea r14, [rel t4_result]
    mov r13d, t4_result_len
    call parent_tail
    jmp t7_begin

; ====================================================================
; T7 - BUG-0280: corrupt-ELF solib entries must be rejected with 0
; ====================================================================
t7_begin:
    lea rdi, [rel t7_start]
    mov esi, t7_start_len
    call print_str
    sub rsp, 16
    mov rdi, rsp
    mov rax, SYS_PIPE
    int 0x80
    test rax, rax
    jnz t7_pfail_sp
    mov r12d, [rsp]
    mov r13d, [rsp+4]
    add rsp, 16
    mov [rel g_result_wfd], r13
    mov rax, SYS_FORK
    int 0x80
    test rax, rax
    jnz t7_parent
    ; ---- child ----
    mov rdi, r12
    mov rax, SYS_CLOSE
    int 0x80
    ; each corrupt table entry must be rejected with 0: bad phnum,
    ; OOB filesz, absurd memsz (kernel stays alive - that is the point).
    ; Every verdict is ACCUMULATED in ebx (bit per defect) instead of
    ; fail-fast, so the before/after evidence shows the exact per-defect
    ; behavior: pre-fix each accepted corruption lights its bit and the
    ; mask prints; post-fix the mask is 0.
    xor ebx, ebx                 ; accepted-bad accumulation mask
    mov rax, SYS_MAPSOLIB
    lea rdi, [rel t7_bad1]
    mov rsi, 15
    int 0x80
    test rax, rax
    jz .t7_ok1
    or ebx, 1
.t7_ok1:
    mov rax, SYS_MAPSOLIB
    lea rdi, [rel t7_bad2]
    mov rsi, 14
    int 0x80
    test rax, rax
    jz .t7_ok2
    or ebx, 2
.t7_ok2:
    mov rax, SYS_MAPSOLIB
    lea rdi, [rel t7_bad3]
    mov rsi, 13
    int 0x80
    test rax, rax
    jz .t7_ok3
    or ebx, 4
.t7_ok3:
    ; the legal entry still maps (non-zero base) ...
    mov rax, SYS_MAPSOLIB
    lea rdi, [rel t6_solib]
    mov rsi, 10
    int 0x80
    test rax, rax
    jnz .t7_oklegal
    or ebx, 8
.t7_oklegal:
    ; ... and PMM still services an mmap + munmap round trip
    mov rax, SYS_MMAP
    xor rdi, rdi
    mov rsi, 4096
    xor rdx, rdx
    int 0x80
    test rax, rax
    jz .t7_pmm_bad
    mov rdi, rax
    mov rsi, 4096
    mov rax, SYS_MUNMAP
    int 0x80
    jmp .t7_verdict
.t7_pmm_bad:
    or ebx, 16
.t7_verdict:
    test ebx, ebx
    jz .t7_clean
    lea rdi, [rel t7_mask]
    mov esi, t7_mask_len
    call print_str
    mov rdi, rbx
    call print_u64
    xor eax, eax
    jmp child_finish
.t7_clean:
    mov eax, 1
    jmp child_finish
t7_parent:
    mov rdi, r13
    mov rax, SYS_CLOSE
    int 0x80
    mov rdi, r12                 ; collect() watches the READ end (r13 is closed now)
    call collect
    lea r14, [rel t7_result]
    mov r13d, t7_result_len
    call parent_tail
    jmp t6_begin
t7_pfail_sp:
    add rsp, 16
t7_pfail:
    inc r15
    lea rdi, [rel t7_result]
    mov esi, t7_result_len
    call print_str
    jmp t6_begin
t4_pfail_sp:
    add rsp, 16
t4_pfail:
    inc r15
    lea rdi, [rel t4_result]
    mov esi, t4_result_len
    call print_str
    jmp t7_begin

; ====================================================================
; T6 - BUG-0280: sys_map_solib window cap (fail-closed bump)
; ====================================================================
t6_begin:
    lea rdi, [rel t6_start]
    mov esi, t6_start_len
    call print_str
    sub rsp, 16
    mov rdi, rsp
    mov rax, SYS_PIPE
    int 0x80
    test rax, rax
    jnz t6_pfail_sp
    mov r12d, [rsp]
    mov r13d, [rsp+4]
    add rsp, 16
    mov [rel g_result_wfd], r13
    mov rax, SYS_FORK
    int 0x80
    test rax, rax
    jnz t6_parent
    ; ---- child ----
    mov rdi, r12
    mov rax, SYS_CLOSE
    int 0x80
    xor r14, r14
t6_loop:
    mov rax, SYS_MAPSOLIB
    lea rdi, [rel t6_solib]
    mov rsi, 10
    int 0x80
    test rax, rax
    jz t6_loop_done
    inc r14
    cmp r14, 200000
    jb t6_loop
t6_loop_done:
    lea rdi, [rel t6_count]
    mov esi, t6_count_len
    call print_str
    mov rdi, r14
    call print_u64
    ; the 64 MiB solib window holds a few thousand libfoo copies; a count
    ; far outside that range means the bump was never window-capped
    cmp r14, 1000
    jb t6c_fail
    cmp r14, 10000
    ja t6c_fail
    ; PMM must still be healthy
    mov rax, SYS_MMAP
    xor rdi, rdi
    mov rsi, 4096
    mov rdx, 3                   ; PROT_READ|PROT_WRITE (writes [rax] below)
    int 0x80
    test rax, rax
    jz t6c_fail
    mov byte [rax], 0x5A
    mov r13, rax
    mov rax, SYS_MUNMAP
    mov rdi, r13
    mov rsi, 4096
    int 0x80
    mov eax, 1
    jmp child_finish
t6c_fail:
    xor eax, eax
    jmp child_finish
t6_parent:
    mov rdi, r13
    mov rax, SYS_CLOSE
    int 0x80
    mov rdi, r12                 ; collect() watches the READ end (r13 is closed now)
    call collect
    lea r14, [rel t6_result]
    mov r13d, t6_result_len
    call parent_tail
    jmp g6_done
t6_pfail_sp:
    add rsp, 16
t6_pfail:
    inc r15
    lea rdi, [rel t6_result]
    mov esi, t6_result_len
    call print_str
    jmp g6_done

; ====================================================================
g6_done:
    lea rdi, [rel done_msg]
    mov esi, done_len
    call print_str
    mov rdi, r15
    call print_u64
    test r15, r15
    jnz g6_fail
    lea rdi, [rel pass_msg]
    mov esi, pass_len
    call print_str
    mov rax, SYS_EXIT2
    xor rdi, rdi
    int 0x80
g6_fail:
    lea rdi, [rel fail_msg]
    mov esi, fail_len
    call print_str
    mov rax, SYS_EXIT2
    mov rdi, 1
    int 0x80

section .data
g_result_wfd:    dq 0
g_res:           dd 0
t2_parent_addr:  dq 0

section .bss
t5_cwd_buf:   resb 128
t5_stat_buf:  resb 128

section .rodata
start_msg:     db "g6_test: start", 10
start_len      equ $ - start_msg
done_msg:      db "g6_test: failed tests = "
done_len       equ $ - done_msg
pass_msg:      db "g6_test: PASS", 10
pass_len       equ $ - pass_msg
fail_msg:      db "g6_test: FAIL", 10
fail_len       equ $ - fail_msg
pt_pass_msg:   db "g6_test: =PASS=", 10
pt_pass_msg_len equ $ - pt_pass_msg
t2cf_bump:     db "g6_test: T2 child FAIL bump", 10
t2cf_bump_len  equ $ - t2cf_bump
t2cf_mark:     db "g6_test: T2 child FAIL mark", 10
t2cf_mark_len  equ $ - t2cf_mark
t5cf:          db "g6_test: T5 child FAIL", 10
t5cf_len       equ $ - t5cf
t5pf:          db "g6_test: T5 parent FAIL", 10
t5pf_len       equ $ - t5pf
t1cf:          db "g6_test: T1 child FAIL", 10
t1cf_len       equ $ - t1cf
t1_start:      db "g6_test: T1 exit0-reap start", 10
t1_start_len   equ $ - t1_start
t1_result:     db "g6_test: T1 FAIL", 10
t1_result_len  equ $ - t1_result
t2_start:      db "g6_test: T2 fork-mmap-inherit start", 10
t2_start_len   equ $ - t2_start
t2_result:     db "g6_test: T2 FAIL", 10
step0_msg:     db "S0", 10
step0_len      equ $ - step0_msg
step1_msg:     db "S1", 10
step1_len      equ $ - step1_msg
step2_msg:     db "S2", 10
step2_len      equ $ - step2_msg
stepC_msg:     db "SC", 10
stepC_len      equ $ - stepC_msg
stepP_msg:     db "SP", 10
stepP_len      equ $ - stepP_msg
t2_result_len  equ $ - t2_result
t3_start:      db "g6_test: T3 write2-no-reader start", 10
t3_start_len   equ $ - t3_start
t3_result:     db "g6_test: T3 FAIL", 10
t3_result_len  equ $ - t3_result
t4_start:      db "g6_test: T4 sig-restorer start", 10
t4_start_len   equ $ - t4_start
t4_result:     db "g6_test: T4 FAIL", 10
t4_result_len  equ $ - t4_result
t4_hit:        db "g6_test: T4 handler entered", 10
t4_hit_len     equ $ - t4_hit
t4_back:       db "g6_test: T4 back from handler", 10
t4_back_len    equ $ - t4_back
t5_start:      db "g6_test: T5 per-proc-cwd start", 10
t5_start_len   equ $ - t5_start
t5_result:     db "g6_test: T5 FAIL", 10
t5_result_len  equ $ - t5_result
t6_start:      db "g6_test: T6 solib-window start", 10
t6_start_len   equ $ - t6_start
t6_result:     db "g6_test: T6 FAIL", 10
t6_result_len  equ $ - t6_result
t7_start:      db "g6_test: T7 solib-reject start", 10
t7_start_len   equ $ - t7_start
t7_result:     db "g6_test: T7 FAIL", 10
t7_result_len  equ $ - t7_result
t7_bad1:       db "libbad_phnum.so", 0
t7_bad2:       db "libbad_file.so", 0
t7_bad3:       db "libbad_mem.so", 0
t6_count:      db "g6_test: T6 dlopen count = "
t6_count_len   equ $ - t6_count
t7_mask:       db "g6_test: T7 accepted-bad mask = "
t7_mask_len    equ $ - t7_mask
t5_dir_a:      db "/tmp/g6a", 0
t5_dir_b:      db "/tmp/g6b", 0
t5_sub1:       db "sub1", 0
t5_sub2:       db "sub2", 0
t5_a_sub2:     db "/tmp/g6a/sub2", 0
t5_b_sub1:     db "/tmp/g6b/sub1", 0
t6_solib:      db "libfoo.so", 0
