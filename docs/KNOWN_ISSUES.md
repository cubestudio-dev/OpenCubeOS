# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS — Known Issues, Design Limitations, and TODOs

# Known Issues (WP-08-p5)

This document lists all known issues, design limitations, and TODOs in
Open Cube OS as of WP-08-p5. Each item has: description, impact, cause,
and plan.

## 1. Unfixed Bugs (Technical Reasons)

### 1.1 ush pipe with certain builtins is non-deterministic
- **Description**: `echo abc | cat` sometimes produces no output. `echo abc | grep abc` usually works but can also fail. `cat file | grep pattern` works more reliably.
- **Impact**: Pipe commands in ush may silently produce no output.
- **Cause**: Race condition in the fork+pipe mechanism. The producer child (echo/cat) may exit before the consumer child (cat/grep) is scheduled. When the producer exits, `user_process_reap_resources` closes all pipe fds, decrementing `writer_count`. If `writer_count` reaches 0 before the consumer reads, the consumer gets EOF immediately. The pipe buffer data IS preserved (pipe slots are not freed on close), but the consumer may read 0 bytes if the scheduler hasn't run the consumer yet.
- **Plan**: Redesign the pipe lifecycle: (a) don't decrement writer_count on child exit until the parent explicitly closes the pipe, or (b) use a reference-counted pipe that persists until ALL references are closed, or (c) add a `pipe_notransfer` flag that keeps the pipe alive until sys_wait4 reaps the child.

### 1.2 Heap cross-page coalescing not implemented
- **Description**: `kfree` only coalesces adjacent free blocks within the same 4 KiB page. Blocks in different pages of the same contiguous pool are not merged.
- **Impact**: Heap fragmentation in multi-page pools. A large allocation may fail even though the total free space is sufficient.
- **Cause**: The heap doesn't track pool base addresses. It can't distinguish "two blocks in the same contiguous pool" from "two blocks in separate single-page pools that happen to be VA-adjacent."
- **Plan**: Add a pool-base registry (`g_pool_bases[]`) that `kfree` can check to determine if two adjacent blocks are in the same pool.

### 1.3 execve doesn't handle BSS (memsz > filesz)
- **Description**: `sys_execve` maps PT_LOAD segments using only `filesz`, ignoring `memsz`. BSS pages (the difference) are not allocated.
- **Impact**: Programs loaded via execve that have BSS data will page-fault when accessing BSS variables. (Currently mitigated because ush's pipe children inherit BSS from the parent via fork, not execve.)
- **Cause**: The `sys_execve` implementation was written before BSS handling was added to `user_process_create`. The BSS-handling code was not backported.
- **Plan**: Copy the BSS-handling loop from `user_process_create` into `sys_execve`.

## 2. Design Limitations (With Rationale)

### 2.1 No per-process cwd
- **Description**: The kernel has a single global cwd (`g_cwd` in shell.c). All user processes share the same cwd.
- **Impact**: Multiple ush sessions would share the same cwd. Forked children inherit the parent's cwd but can't change it independently.
- **Rationale**: The VFS was designed for a single-shell environment. Per-process cwd would require adding a `cwd` field to `user_proc_t` and modifying `vfs_resolve` to use the per-process cwd. This is planned for a future WP.
- **Workaround**: Use absolute paths in scripts.

### 2.2 VFS is in-memory only (ramfs)
- **Description**: The root filesystem is an in-memory ramfs. FAT32/exFAT/ext4 are supported for disk-based filesystems, but the root `/` is ramfs.
- **Impact**: Files created in ramfs are lost on reboot. Disk filesystems must be mounted explicitly.
- **Rationale**: The kernel boots from ISO (read-only). A ramfs root allows the shell to create temporary files without a disk. Disk filesystems are available via `mount` for persistent storage.

### 2.3 No virtual terminal / TTY layer
- **Description**: There is no TTY abstraction. `sys_read(0, ...)` reads from the keyboard queue directly. `sys_write(1, ...)` writes to the framebuffer console.
- **Impact**: No job control with Ctrl+Z (SIGTSTP), no line editing in pipe stdin, no terminal modes.
- **Rationale**: A full TTY layer is a significant feature. The current direct keyboard→console model is simpler and sufficient for a single-user shell.

### 2.4 No SMP (single-core only)
- **Description**: The kernel uses a single CPU core. No IPI, no per-CPU data structures, no spinlock-based scheduler.
- **Impact**: Only one CPU core is used even on multi-core systems.
- **Rationale**: SMP adds significant complexity (per-CPU stacks, IPI, TLB shootdown, lock ordering). The kernel is designed for single-core simplicity.

### 2.5 No paging to disk (no swap)
- **Description**: Physical memory is allocated from PMM. There is no swap mechanism.
- **Impact**: If physical memory is exhausted, allocations fail (no swap fallback).
- **Rationale**: Swap requires a backing store (disk), a paging daemon, and complex page-replacement algorithms. Not needed for a 256 MiB QEMU environment.

### 2.6 Network stack is minimal
- **Description**: The e1000 driver + TCP/IP stack supports: DHCP, DNS, ping (ICMP), wget (HTTP GET). No TCP server (listen/accept), no UDP, no TLS.
- **Impact**: Can't run a network server. Can only make outgoing HTTP requests.
- **Rationale**: A full network stack with server support is a large feature. The current client-only stack is sufficient for wget/DNS/DHCP.

## 3. TODOs (By Work Package)

### WP-09 (Not Started)
- Per-process cwd
- TTY layer with line editing
- TCP server (listen/accept)
- UDP support
- Swap/paging to disk
- SMP support
- Full ext4 write support (currently read-only for ext4)
- FAT32 LFN write support (read works, write creates 8.3 only)
- Signal masking (sigprocmask)
- Process groups / sessions
- Real-time signals
- Async I/O (aio)
- Timer wheel / hrtimer
- Kernel module loading
- User management (multi-user)
- Permission model (uid/gid)

### Future (Post WP-09)
- Network: TLS, SSH, FTP
- Graphics: GUI toolkit, window manager
- Audio: sound card driver
- USB: host controller driver
- Multi-core: SMP scheduler
- Virtualization: KVM-like hypervisor
