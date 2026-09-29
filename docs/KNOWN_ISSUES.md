# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS — Known Issues, Design Limitations, and TODOs

# Known Issues (WP-08-p7)

This document lists all known issues, design limitations, and TODOs in
Open Cube OS as of WP-08-p7. Each item has: description, impact, cause,
and plan.

## 1. Previously Unfixed Bugs — ALL FIXED in P7

### 1.1 heaptest hang — FIXED (P7)
- **Root cause**: `kfree` backward coalescing path returned without calling
  `heap_lock_release(irq_flags)`, leaving the heap spinlock locked. The next
  `kmalloc`/`kfree` would spin forever on `heap_lock_acquire`.
- **Fix**: Added `heap_lock_release(irq_flags)` before the `return` in the
  backward coalesce path.
- **Test**: `heaptest` now completes: "Heap overhead test (100 allocs): ... PASS"

### 1.2 ush pipe race (echo abc | cat) — FIXED (P7)
- **Root cause**: Same as §1.1 — the stuck heap lock caused `fork` to hang
  (fork calls `kmalloc` for page table allocation), which caused the pipe
  timing to be non-deterministic. With the heap lock properly released,
  fork works correctly, and the pipe producer/consumer timing is reliable.
- **Fix**: Same as §1.1 (heap_lock_release fix).
- **Test**: `echo abc | cat` × 10 → 10/10 PASS

### 1.3 heap cross-page coalescing (P2-44) — FIXED (P7)
- **Root cause**: The P6 attempt to add cross-page coalescing failed because
  the heap lock was stuck (same bug as §1.1). With the lock fixed, the
  `g_pool_bases[]` tracking + `in_same_pool()` check now works correctly.
- **Fix**: Added `g_pool_bases[]` to track contiguous pool base addresses.
  `in_same_pool()` checks if two adjacent blocks are in the same contiguous
  pool. Backward and forward coalescing now use `same_page || same_pool`.
- **Test**: `heaptest` (100 allocs + frees with coalescing) completes PASS.

### 1.4 execve BSS handling — FIXED (P6)
- **Fix**: `sys_execve` now maps `memsz > filesz` (BSS pages) by allocating
  + zero-filling extra pages, same as `user_process_create`.

## 2. Design Limitations (With Rationale)

### 2.1 No per-process cwd
- **Description**: The kernel has a single global cwd (`g_cwd` in shell.c).
  All user processes share the same cwd.
- **Impact**: Multiple ush sessions would share the same cwd.
- **Rationale**: Per-process cwd requires a `cwd` field in `user_proc_t`
  and modifying `vfs_resolve` to use the per-process cwd. Planned for WP-09.
- **Workaround**: Use absolute paths in scripts.

### 2.2 VFS is in-memory only (ramfs)
- **Description**: The root filesystem is an in-memory ramfs.
- **Impact**: Files created in ramfs are lost on reboot.
- **Rationale**: The kernel boots from ISO (read-only). A ramfs root allows
  the shell to create temporary files without a disk.

### 2.3 No virtual terminal / TTY layer
- **Description**: No TTY abstraction. `sys_read(0, ...)` reads from the
  keyboard queue directly.
- **Impact**: No job control with Ctrl+Z, no line editing in pipe stdin.
- **Rationale**: A full TTY layer is a significant feature. The current
  direct keyboard→console model is simpler.

### 2.4 No SMP (single-core only)
- **Description**: The kernel uses a single CPU core.
- **Impact**: Only one CPU core is used on multi-core systems.
- **Rationale**: SMP adds significant complexity (per-CPU stacks, IPI, TLB
  shootdown, lock ordering).

### 2.5 No paging to disk (no swap)
- **Description**: Physical memory is allocated from PMM. No swap mechanism.
- **Impact**: If physical memory is exhausted, allocations fail.
- **Rationale**: Swap requires a backing store, paging daemon, and complex
  page-replacement algorithms. Not needed for 256 MiB QEMU.

### 2.6 Network stack is minimal
- **Description**: e1000 + TCP/IP supports DHCP, DNS, ping (ICMP), wget (HTTP
  GET). No TCP server, no UDP, no TLS.
- **Impact**: Can't run a network server.
- **Rationale**: Client-only stack is sufficient for wget/DNS/DHCP.

## 3. TODOs (By Work Package)

### WP-09 (Not Started)
- Per-process cwd
- TTY layer with line editing
- TCP server (listen/accept)
- UDP support
- Swap/paging to disk
- SMP support
- Full ext4 write support
- FAT32 LFN write support
- Signal masking (sigprocmask)
- Process groups / sessions
- Async I/O (aio)
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
