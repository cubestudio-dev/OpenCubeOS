# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS — Known Issues, Design Limitations, and TODOs

# Known Issues (WP-09)

This document lists all known issues, design limitations, and TODOs in
Open Cube OS as of WP-09. Each item has: description, impact, cause,
and plan.

## 1. Previously Unfixed Bugs — ALL FIXED in P7

### 1.1 heaptest hang — FIXED (P7), re-fixed (WP-09-fix1)
- **Root cause**: `kfree` backward coalescing path returned without calling
  `heap_lock_release(irq_flags)`, leaving the heap spinlock locked. The next
  `kmalloc`/`kfree` would spin forever on `heap_lock_acquire`.
- **Fix**: Added `heap_lock_release(irq_flags)` before the `return` in the
  backward coalesce path.
- **WP-09-fix1 (re-review BUG-001, P0)**: The P7 fix only covered the
  backward-coalesce path. Two MORE early-return paths in `kfree` had the
  same lock leak: double-free detection (`magic == HEAP_MAGIC_FREE`) and
  invalid-magic detection. `memtest` (which deliberately double-frees)
  printed PASS and then permanently locked the whole shell. Both paths now
  release the lock before returning.
- **Test**: `heaptest` now completes: "Heap overhead test (100 allocs): ... PASS";
  `memtest` completes AND the shell stays interactive afterwards
  (`memtest` -> `uname -a` answers).

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
  + HTTPS via TLS 1.2), TCP server (kernel sshd/net_accept), route/arp/
  firewall/tcpstats. No UDP, no TCP keepalive, no window scaling.
- **Impact**: UDP-based services unavailable.
- **Rationale**: Client-focused TCP stack covers wget/HTTPS/DNS/DHCP/SSH.

## 2A. WP-09 Known Issues / Accepted Behaviors (all verified 2026-09-30)

### 2A.1 TLS: kernel does not send close_notify on shutdown — accepted
- **Description**: `tls_close()` issues a plain TCP FIN without a TLS
  close_notify alert. The test server reports
  `unwrap (close_notify): [SSL: UNEXPECTED_EOF_WHILE_READING]`.
- **Impact**: Server-side logs show a benign EOF error; data integrity is
  unaffected (every record is MAC-verified).
- **Plan**: Send close_notify in a future WP-10+ batch.

### 2A.2 TLS: server Finished not cryptographically verified — by design
- **Description**: The kernel reads the server's encrypted Finished record
  but does not decrypt/verify its verify_data (server-to-client application
  records ARE decrypted and MAC-verified).
- **Impact**: No confirmation that the server holds the same master secret
  from the Finished message itself; practical assurance comes from the
  MAC verification on every server application record.
- **Rationale**: Avoids implementing AES-CBC decrypt for handshake records
  in the minimal client.

### 2A.3 tls.c header comment stale — doc-only
- **Description**: The file header says "Server-side encrypted records ...
  accepted but NOT decrypted", but `tls_recv()` does decrypt + MAC-verify
  application data and alerts.
- **Impact**: Documentation confusion only; behavior is correct.
- **Plan**: Fix the comment in the next code-touching batch.

### 2A.4 TLS/SSH: certificate and host-key verification skipped — by design (test phase)
- **Description**: TLS accepts any certificate; SSH accepts any host key.
- **Impact**: MITM is possible in untrusted networks.
- **Rationale**: WP-09 targets protocol correctness; trust-on-first-use /
  CA verification is deferred (see batch-A assessment: host key persistence,
  known_hosts, publickey auth, keepalive, algorithm whitelist).

### 2A.5 paramiko test-server direct-mode probe error — test-harness only
- **Description**: `paramiko_sshd.py` probes the channel with an immediate
  `recv()`; the kernel sends nothing before exec, so the probe logs
  `channel error: Socket is closed`.
- **Impact**: None on the kernel; exec data flows via
  `check_channel_exec_request` (verified).

### 2A.6 SSH performance — QEMU DH modexp ~26-30 s
- **Description**: 2048-bit modexp in QEMU TCG takes 26-30 s per operation
  (2 ops per KEX). On real hardware this is orders of magnitude faster.
- **Impact**: Slow SSH connect under emulation only.

### 2A.7 crashlog tick display — cosmetic
- **Description**: Boot-time self-test exceptions show `@tick=0`; earlier
  records noted `@tick=1`. Tick granularity at boot differs.
- **Impact**: Display only; the 3 intentional exceptions (#DE/#UD/#PF)
  themselves are expected and unchanged.

## 3. TODOs (By Work Package)

### WP-09 (Done — shipped)

Shipped in WP-09: crypto core (AES-128/SHA-256/HMAC/DH modexp/random),
SSH client + server (group14-sha256, aes128-cbc, hmac-sha2-256,
rsa-sha2-256, password auth, session exec), TLS 1.2 client
(DHE-RSA-AES128-CBC-SHA256), HTTPS wget, route/arp/firewall/tcpstats/dns
commands, mprotect syscall test, p3_test user program.

Carried over from the old WP-09 TODO list (NOT done, deferred):
- Per-process cwd
- TTY layer with line editing
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

### WP-10+ (Next)
- TLS: send close_notify on shutdown (§2A.1)
- TLS: verify server Finished (§2A.2)
- TLS/SSH: host key persistence + known_hosts, publickey auth, keepalive,
  algorithm whitelist (§2A.4)
- Code comment refresh: tls.c header (§2A.3)

### Future (Post WP-10)
- Network: FTP client, SFTP
- Graphics: GUI toolkit, window manager
- Audio: sound card driver
- USB: host controller driver
- Multi-core: SMP scheduler
- Virtualization: KVM-like hypervisor

## 4. Known Issues Recorded During the WP-09 Fix Rounds

### 4.1 FAT32 mount point occasionally disappears — **FIXED (WP-AUDIT-01-p0fix1, BUG-0002 / A12-002)**
- **Observed once** during the WP-09 re-review (1 in 4 runs): right after
  `mkfs.fat32 hda` -> `fatmount hda /mnt` -> `ls`/`mounts`/`fatstat`/`sync`,
  a subsequent `write /mnt/big.txt AAAA` failed with `open failed` and
  `ls /mnt` reported `no such path: /mnt`. The same sequence passed in 3
  re-runs. Root cause not located (suspected mount-table/heap race).
  We chose NOT to blind-patch; the issue is recorded here for the next
  investigation round.
- **ROOT CAUSE (WP-AUDIT-01, A12-002)**: `fs_vfs_rmdir`/`fs_vfs_unlink`
  did not refuse mount points, and `fs_ramfs_rmdir` treated a mounted
  FAT32 root (a child of a ramfs directory) as a ramfs node — freeing the
  live FAT32 context through `fs_ramfs_inode_t::data` and detaching the
  mount root while the mount table entry stayed in use. Whether the node
  still had cached children (i.e. whether any file had been listed first)
  decided pass/fail — hence the "1 in 4 runs" non-determinism.
- **Fix (WP-AUDIT-01-p0fix1)**: fs_vfs_rmdir/unlink now reject mount-point
  targets; fs_vfs_umount refuses while nested mounts exist;
  fs_ramfs_rmdir additionally verifies `child->fs_type == ramfs`.
- **Test**: `mount fat32 hda /mnt` -> `rmdir /mnt` -> "rmdir: failed";
  `ls /` still lists `mnt`; `umount /mnt` works; kernel stays alive.

## 5. WP-AUDIT-01-p0fix1 — P0 batch 1 (BUG-0001..BUG-0020) — ALL FIXED

Fixed per the WP-AUDIT-01 audit report (docs/audit/report.md §10.1,
first 20 P0 findings in report order). Each fix: patched code, 0-error
0-warning build, QEMU reproduction (FAIL before / PASS after), and the
WP-09..wp08fix1 regression suite re-run (18/18 userprog set, dhtest 5/5,
cryptotest 3/3, SSH both directions, checkupdate live OTA).

| BUG | Location | Fix |
|---|---|---|
| BUG-0001 | fs/fs_vfs.c mount message | bounds-checked msg[80] construction (was unbounded strcpy chain) |
| BUG-0002 | fs/fs_vfs.c + fs/fs_ramfs.c | rmdir/unlink refuse mount points; umount refuses with nested mounts; ramfs rmdir verifies node ownership (BUG-019 root cause) |
| BUG-0003 | fs/fs_ext4.c mount | reject zero blocks/inodes-per-group + oversized log_block_size (was #DE halt) |
| BUG-0004 | fs/fs_fat32.c | BPB validation (512B sectors, reserved>=1, num_fats>=1, clusters>0) + cluster bounds in read/write paths |
| BUG-0005 | net/net_core.c tcptest | 2×16.6KB connection structs moved off the 16KB boot stack to the heap (audit RUN-01 100% crash) |
| BUG-0006 | kernel/core/core_sched.c | kthread stacks 1 page -> 4 contiguous pages (TX path + IRQ net_poll need ~6-11KB) |
| BUG-0007..0013 | net/net_ssh.c client | packet length capped to the 4096B receive page; banner loop bounded; name-list overflow-safe bounds; negative ks_len/f_len rejected; exchange-hash input bounds-checked; encrypted receive checked before reading |
| BUG-0014..0020 | net/net_sshd.c server | same receive-page cap; negative e_len rejected; name-list bounds; hash-input size pre-check; four-buffer overflow guarded; USERAUTH negative lengths rejected; CHANNEL_OPEN_FAILURE buffer sized to the actual reply (43B) |

Known audit P0s NOT in this batch (BUG-0021..BUG-0041: TLS, syscall,
crypto/PRNG, OTA, block 4Kn, tcptest-infra) remain OPEN and are tracked
in docs/audit/bugs_final.json.

### 4.2 umount keeps the (empty) mount-point directory — BY DESIGN (BUG-032)
- POSIX `umount` does not remove the mount-point directory either. The
  empty directory left behind (e.g. `/a1`) is the mount point the user
  created, and it is preserved so the volume can be re-mounted without
  re-creating the directory.

### 4.3 SSH client does not verify host keys; TLS client does not verify
     certificates — BY DESIGN for the L0 scope (BUG-034)
- Both clients were built for the WP-09 E2E scope (encryption + MAC +
  exec/download). Certificate/host-key verification requires a trust
  store, clock sanity (validity windows) and persistence (known_hosts),
  which are WP-10+ work items (see "WP-10+ (Next)" below). Headers
  document this decision at the call sites.

### 4.4 ush `df` Size column — PARTIAL (BUG-026)
- The user-space shell cannot read the kernel mount table from ring 3.
  It reports real observed data (files found + summed sizes from
  sys_stat). Directory entries legitimately report size 0, so the
  aggregated "Size" can be 0 on an empty-ish root. The kernel shell's
  `df` shows the full mount table.

## 5. WP-09-fix5 additions (2026-10-02)

### 5.1 FAT32 create of non-8.3 names was impossible — FIXED (WP-09-fix5)
- `fat32_create_entry` ran `fat32_encode_short_name()` BEFORE the LFN
  check, so names that do not fit 8.3 (extension longer than 3 chars,
  e.g. `opencube.conf`, or any lowercase name) were rejected with -1
  before the LFN alias path could run. Reading such files (created by
  mtools/other OSes) worked; creating them in-system did not. The fix
  builds the 8.3 alias directly in the LFN path (`XXXXXX~1`). Verified:
  `config_test` 7/7 (includes delete + recreate of `/etc/opencube.conf`)
  and `touch /etc/longname.txt`.
- Remaining limitation (documented, not fixed here): the generated `~1`
  alias does not de-duplicate against existing aliases; two long names
  with the same first six characters in one directory collide on FAT
  (real FAT drivers add a numeric suffix, that machinery is not ported).

### 5.2 Public-internet TLS endpoints reject the kernel TLS client —
     KNOWN LIMITATION (WP-09 scope, unchanged by fix5)
- The kernel TLS client offers TLS 1.2 with `DHE-RSA-AES128-SHA256`
  (1024-bit DH) only. Public servers (modern stacks) require ECDHE/AEAD
  or TLS 1.3 and answer the ClientHello with an alert. `checkupdate`
  therefore works against any server configured for the kernel cipher
  (e.g. tools/update_server.py, TLS 1.2 DHE-RSA-AES128-SHA256) but not
  against arbitrary public HTTPS URLs. DNS + TCP to the default URL
  succeed; the alert is answered cleanly (no crash, explicit
  `connect failed` message). Extending the TLS cipher suite is WP-10+
  work.

## 6. WP-09 mainstreaming additions (2026-10)

### 6.1 Display-layer scroll-edge page fault (pre-existing, exposed by long sessions)
- **Symptom**: kernel #PF (#14, error_code=2, write) at `cr2=0xFD1CF480`,
  rip inside `oc_renderer_default_draw_glyph`, when a command's output
  happens to land on the screen rows near y=592 in a session that has
  scrolled to the bottom. The framebuffer is 800x600x32 (1,920,000 bytes,
  0xFD000000..0xFD1D4C00); the faulting address sits inside the framebuffer
  extent but in an unmapped 4 KiB page (0xFD1CF000).
- **Analysis**: the boot identity-map covers the framebuffer with 2 MiB
  pages, but somewhere between boot and the crashing context the effective
  mapping of that page is lost; the renderer itself bounds-checks
  (xpix/ypix vs width/height) and only reaches row 592 legitimately.
  Root cause is in the AS/page-table layer, not the renderer.
- **Impact**: rare — only hits when output lands on those rows; every
  scripted test that triggers it retried with slightly different output
  geometry passes.
- **Status**: open; workaround for scripted tests is keeping per-command
  output short (tcptest prints a single line). Fix belongs to WP-10+
  (vmm/fb mapping audit).

# Known Issues (rule-9-selfhost batch, 2026-10-03)

## KI-SH-01: nic_rw_test intermittently FAILs on rtl8139 (~1 in 5 runs)
- **Location**: kernel/nic_test_cmds.c (nic_rw_test command) + kernel/nic_rtl8139.c RX path.
- **Symptom**: `[nic_rw_test] nic_send => FAIL (length mismatch)` roughly once in five
  sessions; rtl8139_test itself (link up + TX ARP + RX ARP through the real poll loop)
  passes every run. e1000 sessions unaffected.
- **Repro**: `OC_NETDEV=rtl8139 qemu... nic_rw_test` repeatedly.
- **Status**: OPEN — pre-existing (also present before this batch; the batch did not
  touch any NIC code). Suspected RX ring race between the test's send/recv round-trip
  and the poll cadence. Tracked here per WORKFLOW section 5 item 12; fix is a separate
  batch (rule 3: no drive-by fixes).

## KI-SH-02: abdisk/install write the BIOS boot path only
- **Location**: kernel/disk_setup.c (grub_install_device).
- **Symptom**: UEFI machines cannot boot from an abdisk/install disk without the
  installer ISO (the written boot.img/core.img are GRUB i386-pc BIOS images).
- **Repro**: OC_BOOT_DISK=1 with OVMF instead of SeaBIOS.
- **Status**: OPEN by design for this batch — the ISO itself is BIOS+UEFI bootable and
  documents the path (TRY-IT 4b). A UEFI in-system install needs an ESP + x86_64-efi
  GRUB image; separate batch.

## KI-SH-03: OVMF (UEFI) may boot an attached IDE HDD instead of the CD
- **Location**: OVMF firmware BootOrder, not kernel code.
- **Symptom**: With `-drive if=ide` holding an installed disk, OVMF may ignore `-boot d`
  and try the HDD's GRUB (which then stalls at the menu). Using AHCI attachment
  (`-device ich9-ahci -device ide-hd`) behaves as expected.
- **Status**: OPEN (test-environment quirk, not a kernel bug); ab_partition_test 7/7
  verified under UEFI with AHCI attachment.
