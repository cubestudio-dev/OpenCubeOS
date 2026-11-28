<!-- SPDX-License-Identifier: Apache-2.0 -->
# Ring 3 and the syscall surface

How user code reaches the kernel: one instruction (`int 0x80`), 54
syscall numbers, one 5-argument service door (`SYS_L1SVC`), and the
output-capture mechanism that makes 208 service operations return text.
The ABI contract is mirrored in `ocos/interface/`
([private-headers.md](private-headers.md)); the process/ELF side lives
in `ocos/kernel/core_usermode.c` (1518 lines).

## The entry path

Ring 3 executes `int 0x80`. The IDT gate for vector 128 carries flags
**0xEE (DPL=3)** so the instruction is legal from user mode (selector
0x18, interrupt gate - interrupts off during the handler). The chain:

```
int 0x80
  -> arch_isr_stub_128        (pushes fake error code + vector number)
  -> arch_isr_common          (pushes 15 GPRs, cld, 16-byte align)
  -> arch_isr_dispatch        (frame = 22 u64s, arch_irq_frame_t)
  -> core_syscall_dispatch((u64*)frame)     [core_usermode.c]
```

**ABI**: `rax` = syscall number; arguments in `rdi, rsi, rdx, r10`
(4th) and **`r8` (5th)**; return in `rax`; unknown or unregistered
numbers return -1. The 5th-argument slot is used by exactly one
syscall - `SYS_L1SVC`'s `cap` - and is stashed in `g_syscall_arg5`
before the handler runs (frame slot `regs[7]`; `regs[5]` is r10).
There is no `sysenter`/`syscall`-instruction fast path - int 0x80
only - and no per-task nesting: syscalls never recurse in one task.
Signal delivery happens after the handler returns, before `iretq`.

Registration: `core_syscall_register(num, handler)` into
`g_syscalls[256]`; `usermode_init()` seeds 0/1/2 then calls
`core_syscall_wp08a_init()` + `core_syscall_wp08fix1_init()` +
`core_syscall_l1svc_init()` (the bodies live in
`ocos/services/sysapi/src/svc_sysapi_syscall.c`).

## The complete syscall table (54 numbers)

From `ocos/services/sysapi/interface/svc_sysapi_syscall.h`:

| # | Name | Purpose |
|---|---|---|
| 0 | `SYS_EXIT` | legacy exit (now runs the unified reaper too) |
| 1 | `SYS_WRITE` | console write with per-page mapping check |
| 2 | `SYS_WRITE_AND_EXIT` | write then exit |
| 3 | `SYS_OPEN` | (path, flags) |
| 4 | `SYS_CLOSE` | (fd) |
| 5 | `SYS_STAT` | (path, struct ush_stat*) |
| 6 | `SYS_READDIR` | (path, idx, buf) |
| 7 | `SYS_MKDIR` | (path) |
| 8 | `SYS_RMDIR` | (path) |
| 9 | `SYS_UNLINK` | (path) |
| 10 | `SYS_FORK` | full frame save + address-space copy + fork-child path |
| 11 | `SYS_EXECVE` | (name/argv) - gzip registry lookup + ELF load |
| 12 | `SYS_WAIT4` | (pid, &status) |
| 13 | `SYS_KILL` | (pid, sig) - guarded procs refuse every signal |
| 14 | `SYS_GETPID` | |
| 15 | `SYS_GETPPID` | |
| 16 | `SYS_EXIT2` | modern exit (crt0 uses this) |
| 20 | `SYS_PIPE` | (int pipefd[2]) |
| 30 | `SYS_MMAP` | per-process anonymous bump allocator in [0x3C000000, 0x3FFF0000) |
| 31 | `SYS_MUNMAP` | (addr, len) |
| 32 | `SYS_MPROTECT` | (addr, len, prot) - W^X for ld.so |
| 33 | `SYS_BRK` | (addr) |
| 40 | `SYS_SIGNAL` | (sig, handler) |
| 41 | `SYS_SIGACTION` | (sig, act) |
| 42 | `SYS_SIGRETURN` | restores the saved 22-u64 signal frame |
| 50 | `SYS_DUP` | (fd) |
| 51 | `SYS_DUP2` | (oldfd, newfd) |
| 60 | `SYS_CHDIR` | per-process cwd |
| 61 | `SYS_GETCWD` | |
| 62 | `SYS_IOCTL` | |
| 70 | `SYS_READ` | (fd, buf, len) |
| 72 | `SYS_WRITE2` | fd-aware write (pipes/redirect) |
| 73 | `SYS_READLINE` | kernel line editor |
| 74 | `SYS_GETCH` | raw key, no echo, non-blocking poll |
| 75 | `SYS_UPTIME` | ms since boot |
| 76 | `SYS_MEMINFO` | fills 3 u64 (total/used/free) from PMM |
| 80 | `SYS_SELECT` | |
| 81 | `SYS_POLL` | |
| 90 | `SYS_MAP_SOLIB` | dlopen kernel side (WP-08b) |
| 96 | `SYS_SYMLINK` | (target, linkpath) |
| 97 | `SYS_READLINK` | |
| 98 | `SYS_LINK` | hard link |
| 99 | `SYS_CHMOD` | |
| 100 | `SYS_CHOWN` | |
| 101 | `SYS_NETCMD` | (op, arg, out, cap); ops: IFCONFIG=1 PING=2 NETSTAT=3 WGET=4 |
| 102 | `SYS_PS` | (buf, cap) -> struct ush_ps_entry[] |
| 103 | `SYS_SOCKET` | (type SOCK_TCP=1/SOCK_UDP=2) |
| 104 | `SYS_NETBIND` | |
| 105 | `SYS_NETCONNECT` | |
| 106 | `SYS_NETACCEPT` | |
| 107 | `SYS_NETSEND` | |
| 108 | `SYS_NETRECV` | |
| 109 | `SYS_NETCLOSE` | |
| 110 | `SYS_L1SVC` | (cat, op, arg, out, cap) - the service door |

The socket syscalls take **net socket ids, not process fds** - they do
not participate in dup/select/poll/fork (documented in
`services/ipc`); the NETCMD bridge keeps the historical ush built-ins
(ifconfig/ping/netstat/wget) on the same captured bodies.

## SYS_L1SVC - the single service door

`sys_l1svc(cat, op, arg, out)` with `cap = core_syscall_arg5()` (the
r8 slot), implemented in `svc_sysapi_syscall.c`:

- `out == 0 && cap == 0` is the OCIRE fire-and-forget spelling ->
  renders into a 256-byte kernel scratch buffer, copy-back skipped.
- `cap` must be 1..32768 (raised from 8192 in WP06g2 so the bilingual
  registry listing fits); `out` is validated by `valid_user_ptr`.
- `arg` is copied into a 256-byte kernel buffer - **except cat OCIRE
  (18), which gets a dedicated 4096-byte stack buffer** for the log
  agent's appends.
- Before dispatch: `sti` ... `svc_framework_dispatch(...)` ... `cli` -
  device-wait loops inside op bodies are tick-driven, not dead spins.
- A service's non-zero exit status is still a successful call; only an
  unrouteable category/op (or empty out with rc&lt;0) returns -1.
  Everything else copies the NUL-terminated text back and returns 0.

Routing is by category string against the `.oc_svcs` table (the
linker-section registry - [hidden-mechanisms.md](hidden-mechanisms.md)).
**20 categories, 208 ops**, numbering append-only, mirrored
byte-for-byte into ring 3 via `tools/gen_l1svc_ops.py`:

| Cat | Name | Folder | Ops | Cat | Name | Folder | Ops |
|---|---|---|---|---|---|---|---|
| 1 | time | services/time | 4 | 11 | watchdog | services/watchdog | 1 |
| 2 | block | services/block | 23 | 12 | crypto | services/crypto | 2 |
| 3 | nic | services/net | 20 | 13 | l1 | services/l1 | 3 |
| 4 | snd | services/audio | 17 | 14 | display | services/display | 17 |
| 5 | power | services/power | 8 | 15 | input | services/display | 2 |
| 6 | ota | services/selfhost | 16 | 16 | camera | services/camera | 10 |
| 7 | fs | services/fs | 6 | 17 | usb | services/usb | 9 |
| 8 | net | services/net | 14 | 18 | ocire | services/ocembed | 2 |
| 9 | health | services/health | 3 | 19 | diag | services/diag | 39 |
| 10 | klog | services/klog | 3 | 20 | tui | services/tui | 9 |

Cat 18 is the guard-only OCIRE channel (ops 4 LOG_APPEND / 5
KLOG_SNAPSHOT) - frozen ABI against `ocire/agent/ocire_agent.c`,
refused for every non-guarded caller
([hidden-mechanisms.md](hidden-mechanisms.md)).

## Output capture - why old command bodies still work

Most op bodies are the v1/v2.2.0 shell command bodies that print via
`screen_console_puts`. To serve them through L1SVC, dispatch wraps them
in `core_kcap_run_fn(fn, arg, out, cap)` (`ocos/kernel/core_kcap.c`):
while a capture is active, **the capture hook IS the console hook**
(the console driver holds exactly one hook slot), collecting characters
into the caller's buffer and suppressing both the framebuffer draw and
the serial tee. Captures stack to 4 levels; only the innermost active
capture accepts data; the base hook (the serial-encoder tee) is
reinstalled when the last capture ends; overflow is marked, never
silently truncated.

Three routing styles per op (documented in the l1 files):

1. **captured** - one-shot text ops (the majority).
2. **direct** - `out[0]=0; return fn(arg);` for live console sessions:
   `sshd`, all 9 TUI ops ([tui-engine.md](tui-engine.md)), `ime -i`,
   and `power sleep` (a captured multi-second park would swallow every
   console line printed meanwhile - found live during a supervised
   boot).
3. **buffer-render** - the service writes into `out` with its own
   `put()` helpers (`l1`, `klog`, `health`, `diag`, `fs`, `time`,
   `watchdog` dispatchers).

## Processes, ELF, and the ring-3 world

- **Entry**: `execve` leaves `[rsp]=argc, [rsp+8]=argv`; every app
  links `ocos/interface/l1_crt_entry.asm` which reads them safely and calls
  `app_main(argc, argv)`, exiting via `SYS_EXIT2`.
- **ELF loading**: static ET_EXEC and ET_DYN (PIE) with ASLR, plus the
  dynamic path - PT_INTERP detection, ld.so mapped at 0x10000000,
  `.dynamic`/GOT/PLT relocation by the user-space ld.so, DT_NEEDED
  resolution against the embedded solib table
  ([../l1/solib-loader.md](../l1/solib-loader.md)).
- **Program registry**: `core_userprog_lookup(name, &size)` finds one
  of the 284 gzip-embedded programs ([hidden-mechanisms.md](hidden-mechanisms.md));
  `SYS_EXECVE` inflates on first use.
- **Signals**: SIGHUP 1, SIGINT 2, SIGQUIT 3, SIGKILL 9, SIGUSR1 10,
  SIGSEGV 11, SIGUSR2 12, SIGTERM 15, SIGCHLD 17, NSIG 32; pending
  delivery on syscall return; SIGSEGV delivery from ring-3 exceptions
  with a 3-delivery ping-pong guard, then kill
  ([interrupts.md](interrupts.md)).
- **Pipes** (kernel-side): 32 pipes, 4096-byte buffers, fd kinds in
  `user_proc_t.fds[]` (0=free, 1=VFS fd, 2=pipe-read, 3=pipe-write),
  per-end wait queues; `pipe_close_fd` is exported for the unified
  reaper.
- **mmap**: a per-process anonymous bump allocator in
  [0x3C000000, 0x3FFF0000) with a 16-region ownership table - no
  shared memory syscall; fork is an eager copy (`VMM_FLAG_COW` is
  defined but unused).
- **Job control** (services/ipc): `job_create` spawns the named
  embedded program and binds the job to the actual pid (no
  "highest live pid" guessing - BUG-0249); `job_control` 0=fg wait,
  1=bg, 2=kill via the unified reaper.

The L1 developer view of everything above:
[../l1/overview.md](../l1/overview.md) and
[../l1/surface-map.md](../l1/surface-map.md).
