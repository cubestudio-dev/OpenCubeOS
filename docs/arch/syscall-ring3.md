<!-- SPDX-License-Identifier: Apache-2.0 -->
# Ring 3: syscalls, ELF loading, processes

## The syscall ABI

`kernel/core/core_syscall.h`: `rax` = syscall number, `rdi`/`rsi`/
`rdx`/`r10` = arguments, `rax` = result (>= 0 success, < 0 error).
53 syscall numbers are defined (verify:
`grep -c '#define SYS_' kernel/core/core_syscall.h`).

## User programs

`kernel/core/core_usermode.c` + assembly trampolines: ring-3 entry,
fork (`arch_enter_ring3_fork.S`), the ELF loader, the process table,
signals. `userprogs/` ships the test fleet - hello, fork, exec, pipe,
signal, select, mmap variants, and the dynamic-linking family.

## Dynamic linking - a real in-tree ld.so

`libs/ld_so.c` is a shared-object loader: `run so_test`, `run
dlsym_test`, `run pie_test`, `run reloc_test`, `run dyn_hello`,
`run g7mmap` all exercise it. L1 modules load the same way at
runtime - see [../l1/solib-loader.md](../l1/solib-loader.md).

## How a user program runs

`run <name>` looks up the embedded program header (built from
`userprogs/` at kernel build time through `tools/embed_userprog.py` /
`tools/build_c_userprog.py`) or a file on a mounted filesystem, loads
the ELF, and enters ring 3. The program syscalls back for I/O;
when it exits, its slot returns to the process table.

## Faults from ring 3

A user-mode #PF that is not growth escalates to a per-process kill
(not a kernel panic); a user-mode #DE/#UD likewise. The E2E suites
cover all of this (`int3_user` runs from the shell as a test).
