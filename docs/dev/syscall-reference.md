<!-- SPDX-License-Identifier: Apache-2.0 -->
# Syscall reference (int 0x80, 54 numbers)

The complete ring-3 system-call surface of the OCOS kernel: entry
convention, the 54 registered numbers with parameters, returns and
error paths, and the `SYS_L1SVC` special case. Sources:
`ocos/services/sysapi/interface/svc_sysapi_syscall.h` (numbers),
`ocos/services/sysapi/src/svc_sysapi_syscall.c` (bodies),
`ocos/kernel/core_usermode.c` (dispatch),
`ocos/kernel/arch/x86_64/arch_idt*.c/S` (entry). Defines are
verbatim.

## 1. Entry path and ABI

- **Trigger**: ring-3 code executes `int 0x80`.
- **Processing**: IDT vector 128 is a DPL=3 interrupt gate
  (selector 0x18, flags 0xEE) so ring 3 may enter it;
  `arch_isr_stub_128` pushes a fake error code + the vector, the
  common stub pushes 15 GPRs, executes `cld`, aligns the stack and
  calls `arch_isr_dispatch`, which routes to
  `core_syscall_dispatch((u64*)frame)` in `core_usermode.c`.
- **Impact**: the handler runs on the task's kernel stack (rsp0
  from TSS); return value lands in `rax`; signal delivery happens
  after the handler returns but before `iretq`.

Register ABI:

```c
/* Syscall ABI: rax=core_syscall_num, rdi=arg1, rsi=arg2, rdx=arg3, r10=arg4.
 * Return: rax = result (>= 0 success, < 0 error). */
```

- The **5th argument rides in `r8`** and is only used by
  `SYS_L1SVC`; the entry path stashes it in `g_syscall_arg5`
  before the handler runs (`core_syscall_arg5()` reads it).
- In the interrupt frame (`arch_irq_frame_t`), `regs[5]` is `r10`
  and `regs[7]` is `r8`.
- There is **no** `sysenter`/`syscall`-instruction fast path -
  `int 0x80` only - and no per-task nesting; unknown or
  unregistered numbers return `-1`.
- Registration: `core_syscall_register(num, handler)` fills
  `g_syscalls[256]`; `usermode_init()` seeds numbers 0/1/2, then
  calls `core_syscall_wp08a_init()`, `core_syscall_wp08fix1_init()`
  and `core_syscall_l1svc_init()` (all in services/sysapi).

Ring-3 helpers live in `ocos/interface/l1_api_syscall.h`:
`syscall0..syscall4` (r10 as 4th) and `syscall5` (r8 as 5th, for
L1SVC).

## 2. The complete table (54 numbers)

Errors are returned as small negative values in `rax` (`-1` for
most paths; file descriptors/pids are `>= 0` on success). "Err"
describes the notable failure paths.

| # | Name | Parameters | Returns / error path |
|---|---|---|---|
| 0 | `SYS_EXIT` | `(code)` | legacy exit; now runs the unified reaper `user_process_reap_resources` before EXITED |
| 1 | `SYS_WRITE` | `(buf, len)` | console write; per-page mapping check on the user buffer (kernel-address probes are fenced - see `p0leak`) |
| 2 | `SYS_WRITE_AND_EXIT` | `(buf, len)` | write then exit (the crt0-fallback spelling) |
| 3 | `SYS_OPEN` | `(path, flags)` | fd >= 0 / -1; flags RDONLY/WRONLY/RDWR/CREAT/APPEND/TRUNC |
| 4 | `SYS_CLOSE` | `(fd)` | 0 / -1 (bad fd) |
| 5 | `SYS_STAT` | `(path, struct ush_stat*)` | 0 / -1; kernel memcpy's `sizeof(struct ush_stat)` into the caller buffer |
| 6 | `SYS_READDIR` | `(path, idx, buf)` | 0 / -1; fills `struct ush_dirent` |
| 7 | `SYS_MKDIR` | `(path)` | 0 / -1 (exists, no parent) |
| 8 | `SYS_RMDIR` | `(path)` | 0 / -1 (not empty) |
| 9 | `SYS_UNLINK` | `(path)` | 0 / -1 (busy, unsupported by fs) |
| 10 | `SYS_FORK` | `()` | full frame save + address-space copy; child returns 0 via `enter_ring3_fork`, parent returns child pid |
| 11 | `SYS_EXECVE` | `(name/argv)` | replaces the image; gzip registry lookup (`core_userprog_lookup`) + ELF load; -1 unknown program / bad ELF |
| 12 | `SYS_WAIT4` | `(pid, &status)` | reaped pid / -1 (no such child) |
| 13 | `SYS_KILL` | `(pid, sig)` | 0 / -1; **guarded procs refuse every signal** (ocire_guard) |
| 14 | `SYS_GETPID` | `()` | pid |
| 15 | `SYS_GETPPID` | `()` | parent pid |
| 16 | `SYS_EXIT2` | `(code)` | modern exit (crt0 uses this) |
| 20 | `SYS_PIPE` | `(int pipefd[2])` | 0 / -1 (table full, 32 pipes max) |
| 30 | `SYS_MMAP` | `(addr/len/prot...)` | anonymous bump allocator in [0x3C000000, 0x3FFF0000), 16-region ownership table |
| 31 | `SYS_MUNMAP` | `(addr, len)` | 0 / -1 |
| 32 | `SYS_MPROTECT` | `(addr, len, prot)` | 0 / -1; W^X for ld.so |
| 33 | `SYS_BRK` | `(addr)` | new brk / old brk (query when addr=0) |
| 40 | `SYS_SIGNAL` | `(sig, handler)` | old handler / -1 |
| 41 | `SYS_SIGACTION` | `(sig, act)` | 0 / -1 |
| 42 | `SYS_SIGRETURN` | `()` | restores the saved 22-u64 frame via the on-stack restorer trampoline |
| 50 | `SYS_DUP` | `(fd)` | new fd / -1 |
| 51 | `SYS_DUP2` | `(oldfd, newfd)` | newfd / -1 |
| 60 | `SYS_CHDIR` | `(path)` | 0 / -1; per-process cwd |
| 61 | `SYS_GETCWD` | `(buf, size)` | 0 / -1 |
| 62 | `SYS_IOCTL` | `(fd, req, arg)` | 0 / -1 |
| 70 | `SYS_READ` | `(fd, buf, len)` | bytes read (0 = EOF; `read(0,buf,0)==0` - the p2test read0 probe) / -1 |
| 72 | `SYS_WRITE2` | `(fd, buf, len)` | fd-aware write (pipes/redirect kinds 1=VFS 2=pipe-read 3=pipe-write) / -1 |
| 73 | `SYS_READLINE` | `(buf, maxlen)` | kernel line editor (echo, history keys); 0 / -1 |
| 74 | `SYS_GETCH` | `()` | raw key code (no echo, non-blocking poll), 0 when none |
| 75 | `SYS_UPTIME` | `()` | u64 milliseconds since boot |
| 76 | `SYS_MEMINFO` | `(buf)` | fills 3 u64 (total/used/free bytes) from PMM; 0 |
| 80 | `SYS_SELECT` | `(...)` | ready count / -1 |
| 81 | `SYS_POLL` | `(...)` | ready count / -1 |
| 90 | `SYS_MAP_SOLIB` | `(name...)` | dlopen mapping (WP-08b); handle / -1 |
| 96 | `SYS_SYMLINK` | `(target, linkpath)` | 0 / -1 |
| 97 | `SYS_READLINK` | `(path, buf, cap)` | length / -1 |
| 98 | `SYS_LINK` | `(oldpath, newpath)` | 0 / -1 (ramfs maintains nlink) |
| 99 | `SYS_CHMOD` | `(path, mode)` | 0 / -1 |
| 100 | `SYS_CHOWN` | `(path, uid, gid)` | 0 / -1 |
| 101 | `SYS_NETCMD` | `(op, arg, out, cap)` | 0 / -1; op codes below |
| 102 | `SYS_PS` | `(buf, cap)` | entry count / -1; fills `struct ush_ps_entry[]` |
| 103 | `SYS_SOCKET` | `(type SOCK_TCP=1/SOCK_UDP=2)` | socket id / -1 (32 sockets) |
| 104 | `SYS_NETBIND` | `(sock, ip_le, port)` | 0 / -1 |
| 105 | `SYS_NETCONNECT` | `(sock, ip_le, port)` | 0 / -1 |
| 106 | `SYS_NETACCEPT` | `(lsock, &ip, &port)` | accepted sock / -1 |
| 107 | `SYS_NETSEND` | `(sock, buf, len)` | bytes / -1 (would-block = -1, all-or-nothing rtx accept) |
| 108 | `SYS_NETRECV` | `(sock, buf, maxlen)` | bytes / -1 |
| 109 | `SYS_NETCLOSE` | `(sock)` | 0 / -1 |
| 110 | `SYS_L1SVC` | `(cat, op, arg, out, cap)` | 0 / -1 - see section 4 |

`SYS_NETCMD` operation codes (must match the ush mirror):

```c
#define NETCMD_IFCONFIG  1
#define NETCMD_PING      2
#define NETCMD_NETSTAT   3
#define NETCMD_WGET      4
```

## 3. Underlying structures ring 3 shares

On-wire structs a syscall caller must lay out exactly (from
`ocos/interface/l1_fs_api.h`; MUST match kernel `vfs.h`,
`VFS_NAME_LEN=64` - the kernel memcpy's `sizeof(struct)`):

```c
#define USH_TYPE_FILE 1
#define USH_TYPE_DIR  2
#define USH_TYPE_DEV  3
struct ush_dirent { char name[64]; int type; u64 inode; };
struct ush_stat  { int type; u64 size; u32 mode; u32 uid; u32 gid; u32 nlink; char name[64]; };
```

The `SYS_PS` ABI row (`l1_api_syscall.h`):

```c
struct ush_ps_entry {i32 tid; i32 state; i32 priority; char name[32]; u64 cpu_time_ticks;}
```

Signals (core_usermode.c): SIGHUP 1, SIGINT 2, SIGQUIT 3,
SIGKILL 9, SIGSEGV 11, SIGUSR1 10, SIGUSR2 12, SIGTERM 15,
SIGCHLD 17, NSIG 32. Pending-signal delivery happens on syscall
return; SIGSEGV is also delivered from ring-3 exception kills; a
3-delivery ping-pong guard then kills the process.

## 4. SYS_L1SVC (110) in detail

`sys_l1svc(cat, op, arg, out)` with `cap = core_syscall_arg5()`:

1. `out == 0 && cap == 0` is allowed (the OCIRE fire-and-forget
   spelling): the text renders into a 256-byte kernel scratch
   buffer (`kout_scratch`, static, task-context only because
   syscalls never nest in one task), copy-back skipped.
2. Otherwise `cap` must be **1..32768**; `out` is validated by
   `valid_user_ptr` before any write-back.
3. `arg` is copied into a **256-byte kernel buffer** - except
   `cat == L1SVC_CAT_OCIRE` which gets a dedicated **4096-byte
   stack buffer** (the log agent appends up to 4 KiB in one
   LOG_APPEND).
4. `sti` is executed, then
   `svc_framework_dispatch(L1SVC_CAT_NAME(cat), op, arg, out, cap)`, then `cli` (device-wait loops inside op bodies are
   tick-driven).
5. The service's rendered text was captured via `core_kcap_run_fn`
   (or written directly by buffer-render dispatchers); a service's
   own non-zero exit status is still a successful call - only an
   unrouteable cat/op (or empty `out` with `rc < 0`) returns -1.
6. On success the NUL-terminated text is copied back and 0 is
   returned.

The 20 categories and 208 ops reachable through this door are
tabulated in [interface-dictionary.md](interface-dictionary.md);
the full call path (ring-3 wrapper -> int 0x80 -> kcap capture ->
copy-back) is dissected in [call-chains.md](call-chains.md).

## 5. Kernel-side wrapper services (ipc family)

The `services/ipc` folder provides ring-0 callers for the same
surfaces so kernel code (self-tests, l1test) can exercise them
without a user process:

- **wp8a (interfaces 33-40)**: `sys_proc_fork/exec/wait/exit/ getpid/getppid`, `pipe_create`, `fd_dup/fd_dup2`,
  `signal_register/send/return`, `sys_mem_mmap/munmap/mprotect/ brk`, `sys_chdir/getcwd/ioctl/select/poll`; plus the ring-3
  socket wrappers `l1_socket/l1_net_bind/connect/accept/send/recv/ close` (net socket ids, NOT process fds - they do not
  participate in dup/select/poll/fork).
- **wp8cd (survivors 53-57)**: `tool_register`/`tool_list`
  (embedded tool registry), `job_create` (spawns the named embedded
  program and binds the job to the actual pid - no "highest live
  pid" guessing), `job_list`, `job_control` (0=fg waits, 1=bg,
  2=kill through the unified reaper).

## 6. What is deliberately NOT a syscall

- **No shared memory**: processes get private address spaces
  (fork = full copy; the COW PTE flag exists but the copy is
  eager); `SYS_MMAP` is a per-process anonymous bump allocator.
- **No `user_process_kill(pid)`**: the function is deleted with a
  tombstone comment (three invariant violations); the unified
  reaper `user_process_kill_by_tid` is the only sanctioned kill
  path, reached via SIGKILL/`SYS_KILL` and exception exits.
- **No privileged port I/O**: the TSS IOPB denies every port
  [0,256) from ring 3 (the PRIV_TEST root-cause fix) - `in`/`out`
  in ring 3 fault instead of leaking hardware.
- **No L0 shell escape**: services are reached only through
  L1SVC/NETCMD text; there is no "run arbitrary kernel function"
  door.
