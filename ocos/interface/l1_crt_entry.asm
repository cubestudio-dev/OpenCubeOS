; SPDX-License-Identifier: Apache-2.0
; Copyright 2026 cubestudio-dev <cubestudio@qq.com>
; Open Cube OS 3.0.0-WP04 - shared ring-3 crt0.
;
; The kernel execve entry leaves the user stack as
;     [rsp] = argc, [rsp+8] = argv[0], ... (NULL-terminated), envp
; A compiled C _start() cannot read that layout directly: the compiler
; is free to insert its stack-frame prologue (sub rsp, N) BEFORE any
; inline asm, which desynchronizes the [rsp] reads - the WP04 G5
; live-catch: `sub rsp, 0x10` moved argc reads to garbage.  This
; trampoline runs before any prologue exists: it copies argc/argv
; into the first-argument registers and tail-calls the app's
; app_main(argc, argv).  The app returns its exit code in RAX and
; crt0 maps it straight to SYS_EXIT2.
;
; Every folder app links this exactly once (gen_userprogs.py adds
; build/crt0.o to the folder link line); the app source defines
;     int app_main(int argc, char **argv)
; and never defines _start itself.
bits 64
global _start
extern app_main

section .text
_start:
    mov  rdi, [rsp]              ; argc
    lea  rsi, [rsp + 8]          ; argv
    and  rsp, -16                ; SysV: align before call
    call app_main
    mov  edi, eax                ; exit code
    mov  eax, 16                 ; SYS_EXIT2
    int  0x80
    ; unreachable
    hlt

section .note.GNU-stack noalloc noexec nowrite progbits
