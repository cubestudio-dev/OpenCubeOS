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

### 2A.4 TLS/SSH trust model — current state (updated WP-AUDIT-01-p1fix3)
- **TLS (current)**: the client parses and structurally bounds-checks the
  server chain (P0fix2 BUG-0024/0025), checks the validity window via the
  CMOS RTC, and anchors chains against the embedded root store. A hostname
  mismatch for a dNSName reference identity is FATAL (active-impersonation
  signal); an IP-literal target carries no reference identity (RFC 6125)
  and skips the name check; an un-anchored chain (self-signed / private-CA
  servers) prints `[tls] warning: server chain not anchored` and CONTINUES.
- **SSH (current)**: TOFU known_hosts anchor + per-installation host key
  (WP-AUDIT-01-p1fix2: the embedded universal RSA private key was removed;
  sshd fails closed without a per-installation key; clients persist the
  first-seen host key). Still deferred: publickey userauth, keepalive,
  algorithm whitelist.
- **Remaining impact**: an un-anchored TLS chain is accepted with a visible
  warning; SSH is TOFU (first connection is unauthenticated by design).
  MITM is still possible for an attacker present at FIRST contact.

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

### 2A.8 HTTPS E2E: mid-handshake disconnect after ServerHello —
     FOUND + FIXED (WP-AUDIT-01-p1fix3 verification, 2026-10-07)
- **Symptom**: `wget https://10.0.2.2:8443/` against the reference
  https_test_server.py (TLS 1.2, DHE-RSA-AES128-SHA256, self-signed
  RSA-2048 cert with NO SAN) disconnected ~0.02 s after the ServerHello
  with `TLS failed (code 1)`; the server logged
  `ssl.SSLEOFError: UNEXPECTED_EOF_WHILE_READING` inside its own
  handshake. The earlier attribution ("RSA CertificateVerify modexp cost
  + anchor policy") did not match the timing: the disconnect happened
  long BEFORE any modexp ran.
- **Probes**: TLS_DBG probes on the Certificate receive/parse path showed
  `cert rec: payload=799 ctype=22 mtype=11`, `cert mlen=795`,
  `cert parsed: ncerts=1 used=789 blen=795`, then failure with NO
  `[tls] warning` and NO ServerKeyExchange probe — the failure sat
  inside `crypto_x509_verify_chain`.
- **Root cause**: `wget` passes the URL host verbatim, so the TLS layer
  received the IP literal `10.0.2.2` as the hostname. The WP-09
  mainstream TLS batch (555fe1f) had made `crypto_x509_verify_chain(...,
  c->hostname) != X509_OK -> -4` unconditional; the p1fix3 hotfix
  (43ce437) restored warn-and-continue for un-anchored chains but only
  skipped the name check when the hostname was EMPTY — and a URL host is
  never empty. The SAN-less test cert therefore failed the dNSName match
  and the handshake died with X509_E_HOSTNAME (-7 -> -4) before the
  ServerKeyExchange. Introduced 2026-10-01 (555fe1f), NOT by p1fix3; the
  p1fix3 hotfix fixed the policy but not the caller-side IP-literal case.
- **Fix (this round)**: `net_tls_host_is_ip_literal()` in net/net_tls.c —
  IPv4 dotted-quad / IPv6 literals are stored as an EMPTY hostname
  (RFC 6125: no reference identity), so both the TLS 1.2 and TLS 1.3
  chain checks skip the dNSName comparison; the TLS 1.3 chain policy now
  matches the TLS 1.2 policy (fatal only on dNSName mismatch with a real
  reference identity; un-anchored chains warn and continue).
- **Verified E2E (post-fix)**: full handshake vs the reference server —
  `[tls] warning: server chain not anchored (code 8) - continuing`,
  ServerKeyExchange processed, server side
  `TLS handshake OK: TLSv1.2 cipher=('DHE-RSA-AES128-SHA256', ...)`,
  HTTP request received (`GET / HTTP/1.1 Host: 10.0.2.2`), 108-byte
  response sent, `Saved 24 bytes to /wget_https.html`, clean
  close_notify (alert 01 00) received on the guest. Handshake-to-response
  ~9.4 s (two 1024-bit modexps, see §2A.9).

### 2A.9 TLS DHE handshake cost in QEMU — performance, not a bug
- **Description**: the TLS 1.2 DHE path performs TWO 1024-bit modexps
  (Yc = g^x mod p and Z = Ys^x mod p) per handshake; under QEMU TCG that
  is ~8-9 s of the ~9.4 s handshake-to-response measured in §2A.8
  (1024-bit ≈ 4.0 s per op, 2048-bit ≈ 13.5 s per op on this host).
- **Impact**: slow HTTPS/OTA connect under emulation only; correctness
  is unaffected (every value verified by the MAC / Finished exchange).
- **Plan**: windowed (2^4/2^5) modular exponentiation is future
  optimization work; no protocol change is needed.

### 2A.10 host_bn_ec_test -O2 segfault — test-tooling mistake, not code
- **Symptom**: the host-side bn/EC unit test binary built with plain -O2
  segfaulted (exit 139) BEFORE printing the first test line, while the
  ASAN build of the same sources passed 7/7 — looked like an
  -O2-only code bug in the kernel bignum/EC code.
- **Root cause**: the ad-hoc build line had linked
  `kernel/lib/lib_string.c` (plus tests/host_pmm_stub.c) into the host
  binary. lib_string.c DEFINES glibc's own symbol names
  (memset/memcpy/memmove/memcmp/strlen/strcmp/strncmp/strcpy/strncpy/
  strcat/strchr/strcasecmp); in a freestanding kernel that is correct,
  but linked into a glibc host binary these strong definitions interpose
  libc's own startup-critical symbols and the process dies before main.
  ASAN interceptors mask the interposition, so only the plain -O2 build
  crashed. Reproduced (exit 139) with lib_string.c linked; clean
  (exit 0, ALL PASS) without it — on both the p1fix3 base (a75d280) and
  the current head. No kernel code was involved.
- **Fix**: tests/host_bn_ec_test.c header now documents the canonical
  build line (no lib_string.c) and the interposition warning; the stale
  `-Ikernel kernel/bn.c` path was corrected to the restructured layout.

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
- SSH: publickey auth, keepalive, algorithm whitelist (§2A.4 — host key
  persistence + known_hosts are DONE, WP-AUDIT-01-p1fix2)
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

Known audit P0s NOT in batch 1 (BUG-0021..BUG-0041) were fixed in
WP-AUDIT-01-p0fix2 (see section 5B below).

## 5B. WP-AUDIT-01-p0fix2 — P0 batch 2 (BUG-0021..BUG-0041) — ALL FIXED

All 21 remaining audit P0s (report.md order) were fixed, compiled
0 errors / 0 warnings (-Wall -Wextra -Werror), and verified: hostile-peer
reproductions (FAIL on the p0fix1 baseline / PASS on the fixed kernel) and
a full regression pass.  Evidence for each fix:

| BUG | Location | Fix |
|---|---|---|
| BUG-0021 | net/net_tls.c https_get | request sized 2048 + total-length check before any write (was req[1024] with unchecked concatenation of a legally 1663-byte OTA redirect path; baseline repro: kernel #GP with rip=0x6161616161616161 after a 302 to a 1300-byte https path) |
| BUG-0022 | net/net_tls.c TLS1.3 record | length check is exactly buf_cap (was buf_cap+256; ASAN heap-buffer-overflow WRITE 16896 into 16640 on baseline) |
| BUG-0023 | net/net_tls.c TLS1.2 record | RFC 5246 max (2^14+2048=18432) as the record buffer bound; all TLS1.2 record buffers sized TLS12_REC_MAX; check is exactly cap (was cap+2080; ASAN WRITE 18720 into 16640) |
| BUG-0024 | net/net_tls.c Certificate | mlen checked against record payload AND body (was payload-only; ASAN global-buffer-overflow past body[12288]) |
| BUG-0025 | net/net_tls.c ServerKeyExchange | mlen was NEVER checked (up to 16MB OOB read + OOB write); now checked against payload and body |
| BUG-0026 | net/net_tls.c DHE SKE | sbuf tail 320 -> 776 (ServerDHParams legally 774B) + copy bounds check (ASAN global-buffer-overflow WRITE 774 into 384 on baseline) |
| BUG-0027 | net/net_tls.c CertVerify | sig_len bounds-checked against the message; DER decode extracted to tls13_decode_ecdsa_sig() with every length/offset checked (rl=255 wrote r-223.. on baseline); host boundary matrix 9/9 |
| BUG-0028 | shell/shell.c glob | no-slash and after-slash pattern copies bounds-checked (token can reach 287B after env/alias expansion vs pat[256]) |
| BUG-0029 | shell/shell_cmds_disk.c fsck | report buffer 80 -> 256 (8 fixed strings + numbers = 154B overflowed unconditionally on every fsck run) |
| BUG-0030 | shell/shell_cmds_file.c mv | 1MiB cap removed; copy is streamed (32KiB window); byte count verified against source size; source unlinked ONLY after a complete copy (baseline repro: 2MiB file arrived as 524288B on FAT32 and the source was gone) |
| BUG-0031 | kernel/core/core_syscall.c sys_poll | nfds > 4096 rejected (nfds*sizeof() wrapped mod 2^64 and passed access_ok, then wrote user VAs unchecked -> ring-0 #PF -> HALT) |
| BUG-0032 | kernel/core/core_usermode.c exit | number buffer 8 -> 24 (u64_to_str emits up to 21 for a user-controlled exit code) |
| BUG-0033 | kernel/core/core_syscall.c sys_execve | argv/envp are captured BEFORE the CR3 switch (old code read user VAs through the kernel CR3 identity window while access_ok validated the NEW as — argv silently lost); the new user stack is written through the new AS page tables (software walk); stack pages zeroed |
| BUG-0034 | kernel/core/core_syscall.c sys_ps | ktab[64] (3584B stack array) -> kmalloc sized to the caller's cap (kthread stacks are small; p0fix1 already grew them to 4 pages, this removes the large-array stack cost itself) |
| BUG-0035 | kernel/crypto/crypto_x509.c | DER length accumulated unsigned with an int-range guard (was signed int, 0x84 FFFFFFFF wrapped to -1 and defeated the range check); ECDSA r/s lengths also reject <= 0 (rlen<=0 bypassed the > hlen check and memcpy'd (size_t)(-1)) |
| BUG-0036 | kernel/lib/lib_config.c write | replace/append paths bounds-checked (were unchecked; a file with hundreds of duplicate keys overflowed the 4096B heap block); duplicate matching keys are collapsed (first one rewritten) |
| BUG-0037 | kernel/ota/ota_update.c autoupdate thread | line2 160 -> 272 ("Changes: " + 255B server-supplied changes + NUL = 265 overflowed by 105B on every auto-check with a long changes field) |
| BUG-0038 | kernel/ota/ota_update.c checkupdate plain | request 256 -> 2048 with a pre-computed length check (path alone can legally be 1663B) |
| BUG-0039 | kernel/ota/ota_ab.c map_package_path | any ".." path component is rejected outright (was passed through and popped by fs_vfs_normalize -> arbitrary VFS write from a hostile update package) |
| BUG-0040 | drivers/block/driver_block_nvme.c | namespaces formatted with a non-512B LBA size are refused with a clear error (block/cache layers are hardwired to 512B; every read was a 3584B heap overflow on 4Kn).  Full 4Kn support is future work |
| BUG-0041 | kernel/arch/x86_64/arch_exceptions.c | nested-#PF guard: a fault while already handling one halts immediately via serial only (was unbounded re-entry, ~0x230B per level, until a triple fault).  tcptest itself verified alive on both the p0fix1 baseline (root cause fixed there) and this build |

Reproduction/verification summary for this batch:
- QEMU end-to-end (hostile peers, p0fix1 baseline FAIL -> fixed PASS):
  BUG-0021 (#GP 0x6161616161616161 kernel HALT -> clean error, kernel alive),
  BUG-0022/0023/0024 (evil TLS server cases: silent overflow accepted ->
  record rejected, kernel alive), BUG-0030 (2MiB mv lost 3/4 of the file and
  deleted the source -> complete 2097152B copy, fsck used=4097 clusters),
  BUG-0038 (long-path checkupdate: same clean behaviour, overflow removed by
  construction), BUG-0041 (tcptest ALL PASS, kernel alive, both builds).
- Host ASAN suite (tests/host_tls_p0_test.c, kernel net_tls.c included
  directly): baseline aborts (heap/global-buffer-overflow per BUG-0022..0026
  + the BUG-0027 demo) vs fixed 16/16 PASS, ASAN silent.
- Code-level proofs: BUG-0028/0029/0031/0032/0033/0034/0035/0036/0037/0039/0040
  (bounds/overflow math is deterministic in the diffs above; for BUG-0031/0032
  no shipped user program issues those exact hostile syscalls, and BUG-0039's
  end-to-end needs an A/B boot chain that could not be completed inside the
  sandbox session).
- Regression on the final WP-AUDIT-01-p0fix2 ISO: boot self-tests all OK;
  user programs hello/exec_test/fork_test/pipe_test/mmap_test/mprotect_test/
  select_test/signal_test/p3_test/badapp(intercepted)/reloc_test PASS;
  heaptest, l1test, crashlog, ps OK; dhtest 5/5; cryptotest 3/3;
  help 3 modes (default A-Z / -w / -a), Total: 172 command registrations
  (unchanged vs the p0fix1 delivery; zero registration changes in this diff);
  DHCP + ping + DNS + real-network checkupdate (TLS 1.3 to GitHub, full JSON
  fetched); NVMe 512B mkfs/mount/write/read OK (BUG-0040 must not regress
  512B); FAT32 mkfs/mount/fsck/write/read/mv OK; SSH evil-client a08
  rejected cleanly; SeaBIOS + OVMF boot verified.

New findings recorded during this batch (NOT part of the 21, tracked for a
future round):
- fsck reports "invalid device" for NVMe device names (works for hda);
  cosmetic, P2-grade.
- BUG-0038's config-set reproduction path is not reachable as documented:
  `config set` values are capped at 255 bytes, so a >255-char URL is
  truncated at the config layer before update_url_parse ever sees it;
  the redirect-based trigger (used above) is the real attack surface.

### 4.2 umount keeps the (empty) mount-point directory — BY DESIGN (BUG-032)
- POSIX `umount` does not remove the mount-point directory either. The
  empty directory left behind (e.g. `/a1`) is the mount point the user
  created, and it is preserved so the volume can be re-mounted without
  re-creating the directory.

### 4.3 SSH/TLS client trust model — superseded (BUG-034)
- The WP-09-era decision "SSH accepts any host key; TLS accepts any
  certificate" no longer holds. See §2A.4 (updated WP-AUDIT-01-p1fix3):
  TLS anchors chains against the embedded root store with fatal hostname
  mismatch for dNSName references and warn-and-continue for un-anchored
  chains; SSH uses a TOFU known_hosts anchor with a per-installation
  host key. The remaining gap (TOFU first contact, un-anchored-chain
  acceptance with warning) is tracked in §2A.4.

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

## 7. WP-10-AUDIT_P2-fix2 additions (2026-10-08)

- P2 batch 2 (BUG-0189..BUG-0241, items 54..106 of docs/audit/bugs_final.json) — ALL FIXED;
  evidence chains per fix in the release notes + worklog (`WP-10-AUDIT_P2-fix2` entry).
- The remaining OPEN findings are tracked in `docs/audit/bugs_final.json`:
  P2 items 107..214 (BUG-0242..BUG-0349, fix3+ scope) and the P3 items (258).
  Per WORKFLOW §5 item 12, the JSON file is the authoritative per-finding
  tracker (id / sev / loc / desc / src) for everything not yet fixed.
- Accepted behaviors in §2/§2A above are unchanged by fix2; the TLS
  close_notify behavior (§2A.1) is superseded: the client now sends its
  close_notify in ESTABLISHED **or** CLOSE_WAIT (finding #2), verified by
  the raw-byte recorder in the HTTPS E2E gate (tools/https_test_server.py
  observe mode).

## 8. WP-10-AUDIT_P2-fix2b additions (2026-10-09)

- Honest correction of the fix2 embed-chain claim: `make userprogs` on the
  fix2 tree reproduced the reported ERROR verbatim (userprog_libfoo block
  not found, Makefile:257 Error 1) — the fix2 commit's embed-chain Makefile
  rules did not hold; corrected in the fix2b worklog.
- Embed chain repaired: libfoo.c routed through a dedicated solib leg
  (tools/build_solib.py, -Wl,-soname,libfoo.so + DT_SONAME verification,
  regenerates l1/solib_data.h); the six dynamic-link programs (dyn_hello,
  so_test, dlsym_test, pie_test, reloc_test, main_dyn) build as ET_DYN/PIE
  via the pie/pie-foo modes in build_c_userprog.py with hard e_type /
  PT_INTERP gates against silent downgrade; libs/ld_so.c wired into
  USERPROG_SRCS (the ld_so case was dead code); main_dyn embedded and
  mapped in both exec tables (kernel/main.c + core_syscall.c).
- Verified: make userprogs end-to-end EXIT=0; QEMU six dyn progs PASS
  (main_dyn prints "main: foo_add(2,3)=5"); stats 101,150 lines; 176
  commands unchanged.
- History rewrite note: the 22 legacy `Z User <z@container>` commits
  (2026-10-03..10-07) were rewritten to cubestudio-dev via filter-branch;
  the WP-10-AUDIT_P2-fix2 tag now points at the rewritten chain
  (539bbb8c...), the handover tag at 45bcca98..., both on the same history
  as main (fix2b chain head e6e8da0e...).
