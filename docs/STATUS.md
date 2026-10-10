<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 cubestudio-dev <cubestudio@qq.com> -->

# Open Cube OS - Project Status

Updated 2026-10-09 at WP-10-AUDIT_P2-fix3. All 15 work packages are
COMPLETE; the auxiliary batches are shipped; the P2 audit batch fix3 is
COMPLETE - all 53 items landed on main and released (see Audit batches
below).

## Work-package coverage (15/15 COMPLETE)

| WP | Scope | Status | Details |
|---|---|---|---|
| WP-01 | Boot + framebuffer + text rendering | COMPLETE | docs/EXTENSIONS.md |
| WP-02 | Interrupts + timer + keyboard | COMPLETE | docs/EXTENSIONS_WP02.md |
| WP-03 | PMM + VMM + kernel heap | COMPLETE | docs/EXTENSIONS_WP03.md; detailed record below |
| WP-04 | Scheduler + sync + user mode | COMPLETE | docs/EXTENSIONS_WP04.md |
| WP-05 | Shell enhancements + file system | COMPLETE | docs/EXTENSIONS_WP05.md |
| WP-06 | Network protocol stack | COMPLETE | docs/EXTENSIONS_WP06.md |
| WP-07 | Disk subsystem | COMPLETE | docs/EXTENSIONS_WP07.md |
| WP-08 | Syscalls + dynamic linking + user shell + tools | COMPLETE | docs/EXTENSIONS_WP08a/b/cd.md |
| WP-09 | SSH (client+server) + TLS 1.3/1.2 + crypto core | COMPLETE | docs/EXTENSIONS_WP09.md; detailed record below |
| WP-10a | Storage drivers: AHCI / NVMe / ATA DMA / virtio-blk | COMPLETE | docs/EXTENSIONS_WP10a.md |
| WP-10b | NIC drivers: e1000e / igb / ixgbe / RTL8139 / 8168 / 8125 / 810x / BCM57xx | COMPLETE | docs/EXTENSIONS_WP10b.md |
| WP-10u | In-system A/B update + rollback | COMPLETE | docs/EXTENSIONS_WP10u.md |
| WP-10c | Sound cards: HDA / AC'97 / SB16 / ES1370 / virtio / USB audio | COMPLETE | docs/EXTENSIONS_WP10c.md |
| WP-10d | USB host stack: UHCI / OHCI / EHCI / XHCI + HID/MSC/serial/audio | COMPLETE | docs/EXTENSIONS_WP10d.md |
| WP-10-wp08fix1 | Shell line editor + nano/vi editors + VFS link/symlink + SYS 96-102 | COMPLETE | docs/EXTENSIONS_WP10-wp08fix1.md |

The L1 interface numbering 1-138 closes at WP-10-wp08fix1
(docs/INTERFACES.md is the single-page index).

## Auxiliary batches (shipped, not counted as WPs)

- **WP-09-fix5** (2026-10-02): `/etc/opencube.conf` + `checkupdate` -
  detailed record below.
- **Project restructure** (WP-10-project_restructure): the kernel/ +
  drivers/ + net/ + fs/ + shell/ + l1/ + libs/ layout; the Makefile bakes
  the version string from `git describe --tags` at build time.
- **WP-10d-fix2**: power management (shutdown/suspend/halt/reboot) +
  structured help (`help -a`, `help -w`).
- **Rule-9 self-hosting** (WP-10c-selfhost): in-system `abdisk` /
  `install` / `grub-install` / `abcfg` - docs/EXTENSIONS_SELFHOST.md.

## Audit batches

- **WP-AUDIT-01**: P0 batch 1 (BUG-0001..0020) + P0 batch 2
  (BUG-0021..0041) + p1fix1..p1fix4 - ALL FIXED.
- **WP-10-AUDIT_P2-fix1** - 53 P2 items (BUG-0136..0188) - shipped.
- **WP-10-AUDIT_P2-fix2** - 53 P2 items (BUG-0189..0241) - shipped;
  **fix2b** (embed-chain repair + doc consistency) - shipped.
- **WP-10-AUDIT_P2-fix3** - COMPLETE: 53 P2 items (BUG-0242..0294) fixed
  in nine groups (G1..G9) across parallel fix lanes, all merged to main
  (G1 98a8dc7, G2 25304ec, G3 a8c48ab, G4 48b5655, G5 1353e71, G6
  51d1edb, G7 c1f68a7, G8 65a55eb, G9 2df3cf7), plus the kernel-#UD
  root-cause fix riding the G6 lane (6153593): sys_fork never set the
  child task's rsp0 (the WP-04 BUG-029 TSS fix covered only the exec
  path), so the TSS kept the parent's kernel-stack top while a forked
  child ran in ring 3 - every child interrupt pushed onto the parent's
  stack and the parent later popped child data as return addresses
  (kernel #UD inside g_procs[] after a child exits). Verified: 71/71
  regression + g6_test 18/18 (T1/T4/T6-collect no longer isolated) +
  fstest + SSH E2E 5/5 + HTTPS E2E 7/7. `docs/audit/bugs_final.json`
  remains the authoritative per-finding tracker.
  Regression baseline: 18/18 + the dyn/FS/net/crypto gates (boot banner
  self-reports 176 commands).

---

## Detailed records (historical, kept as written)

## WP-09: Security transport (SSH client+server, TLS 1.2/HTTPS, crypto core)

**Status: COMPLETE.** All acceptance criteria met (2026-09-30).
Full evidence with real outputs: docs/VERIFICATION_BATCH_B.md,
docs/verification/*.log, VERIFICATION_REPORT.md §10.

### Acceptance criteria

| Criterion | Status | Evidence (real outputs) |
|---|---|---|
| Compiles with no errors / warnings | OK | `make clean` all: 0 errors 0 warnings (-Wall -Wextra -Werror) |
| 18/18 full QEMU regression | OK | boot banner "WP-09 ready", uname, 12 user progs, p3_test, heaptest (overhead%=5), l1test, crashlog (3 self-test exceptions) |
| dhtest 5/5 | OK | g^0/1^x + group14 truth e/K2 + determinism IDENTICAL + scale sweep 8..256 |
| SSH client ↔ paramiko server | OK | K[:8]=2f4130816e935c6a byte-identical (len=256), exec round trip |
| paramiko client ↔ kernel sshd | OK | 4/4 checks PASS, exec captured 32 bytes, session finished cleanly |
| HTTPS E2E (TLS 1.2, 0x0067) | OK | dual-side logs; encrypted GET; MAC-verified; body = hello-from-opencube-tls |
| ISO reproducible | OK | opencube.elf SHA256 0b330b69... (byte-identical to batch-14 Release src) |

### WP-09 scope shipped

- kernel/crypto.{c,h} + dh_scale_vectors.h (AES-128, SHA-256, HMAC, DH modexp, random)
- kernel/ssh.{c,h} (client), kernel/sshd.c + sshd_rsa_key.h (server)
- kernel/tls.{c,h} (TLS 1.2 client) + HTTPS in wget
- Shell: ssh, sshd, route, arp, firewall, tcpstats, dns (78 commands total)
- userprogs: mprotect_test.asm, p3_test.asm
- Test tools: tools/qemu_runner.py, sshd_test.py, paramiko_sshd.py, https_test_server.py

### Known / accepted behaviors

See docs/KNOWN_ISSUES.md §2A. Current state (2026-10-09): close_notify is
sent in ESTABLISHED or CLOSE_WAIT (superseded by WP-10-AUDIT_P2-fix2,
KNOWN_ISSUES §7); the server Finished IS cryptographically verified; the
X.509 chain + SAN hostname verification is live and fails closed; SSH
verifies the host-key signature over H with a TOFU anchor and implements
publickey auth - see §2A.2/§2A.4 for the exact trust model and what
remains deferred (keepalive, algorithm negotiation whitelist).

## WP-03: Physical memory + Virtual memory + Kernel heap

**Status: COMPLETE.** All acceptance criteria met.

### Acceptance criteria

| Criterion | Status | Evidence |
|---|---|---|
| Compiles with no errors / warnings | OK | `make` clean with `-Wall -Wextra -Werror` |
| Bootable ISO generated | OK | `build/opencube.iso`, hybrid BIOS+UEFI |
| BIOS boot succeeds | OK | SeaBIOS -> GRUB -> kernel -> `oc>` prompt |
| UEFI boot succeeds | OK | OVMF -> GRUB EFI -> kernel -> `oc>` prompt |
| PMM: alloc/free 4KB page frames | OK | `memtest`: alloc 10 pages OK, free OK |
| VMM: create AS, map/unmap/protect | OK | `vmtest`: create AS, map, is_mapped=1, protect, unmap, is_mapped=0, PASS |
| Kernel heap: kmalloc/kfree/krealloc | OK | `memtest`: kmalloc 5 blocks, krealloc, kfree, double-free detection |
| Shell survives `memtest` (WP-09-fix1) | OK | `memtest` PASS -> `uname -a` answers (kfree early-return lock leak fixed) |
| Real page fault handling | OK | #PF handler calls vmm_handle_page_fault, distinguishes legal/illegal |
| Memory stats correct | OK | `mem`: 65504 pages, 234 used, 65173 free |
| Extension interfaces available | OK | PMM, VMM, heap, shell command registration |
| Interface docs complete | OK | `docs/EXTENSIONS_WP03.md` |
| Screenshot evidence | OK | `build/shot-bios-wp03.png`, `build/shot-uefi-wp03.png` |
| em dash fix | OK | Serial output verified 100% ASCII |

### Test results (BIOS boot + serial commands)

```
oc> mem
Physical memory:
  total: 268304384 bytes (65504 pages)
  used:  958464 bytes (234 pages)
  free:  266948608 bytes (65173 pages)
  fragments: 2

oc> heap
Kernel heap:
  size:  16384 bytes
  alloc: 0 bytes (0 blocks)
  free:  16184 bytes (5 blocks)
  overhead: 200 bytes

oc> memtest
PMM test: alloc 10 pages OK, free OK
Heap test: kmalloc 5 blocks OK, krealloc OK, kfree OK, double-free OK
PASS

oc> vmtest
VMM test: created AS, map_page = 0, is_mapped = 1, protect = 0, unmap OK, is_mapped = 0
PASS

oc> frag
PMM: free_pages=65171 fragments=3 avg_frag=21723
Heap: free_blocks=8 free_bytes=16064
```

### What works

- **PMM**: Bitmap allocator, 128 KiB bitmap covering 4 GiB / 4 KiB pages. Parses multiboot2 mmap. Reserves kernel image, framebuffer, mbi, bitmap. O(n) scan with last-scan hint.
- **VMM**: 4-level page table walk. Create/destroy address spaces (copies kernel PML4 entries). Map/unmap/protect pages. Page fault handler with stack growth + heap growth + illegal detection.
- **Heap**: Free-list allocator with first-fit + coalescing. 32-byte block header (magic/size/prev/next). Double-free detection (magic check). kmalloc/kzalloc/kfree/krealloc. Allocator hook.
- **Shell**: 13 registered commands (help/stats/exc/timer/echo/clear/halt/mem/heap/vmmap/vmtest/memtest/frag). L1 can register new commands via `shell_register_command()`.
- **Page fault**: Real #PF handler reads CR2, calls `vmm_handle_page_fault()`. Legal faults (heap region, stack growth) are handled. Illegal faults print diagnostic and halt.
- **em dash fix**: All 36 kernel source files scanned, all em dash (U+2014) replaced with ASCII hyphen. Serial output verified 100% ASCII.

### What's NOT in WP-03

- No higher-half kernel mapping (kernel still at identity-mapped 0-4 GiB).
- No user-space address spaces (VMM can create them but no ring 3 yet).
- No memory-mapped files (the fault handler framework supports it but no FS yet).
- No copy-on-write (the flag exists but no COW fault handler).
- No swap / demand paging.
- No slab allocator (just free-list).

## BUG-040: Global Arrays Without Locks

Several global arrays (g_timers, g_irq_slots, ready queues, etc.) are accessed
without explicit locking. This is safe in the current design because:
1. Single-CPU: only one core accesses these arrays.
2. Interrupts are disabled (CLI) during critical sections.
3. The scheduler disables interrupts before modifying ready queues.

When the kernel is ported to SMP, these will need proper spinlocks.

---

## WP-09-fix5: system configuration file + update check (2026-10-02)

**Status: COMPLETE.** `/etc/opencube.conf` — the first user-editable
system configuration file — plus `checkupdate` (HTTP/HTTPS, JSON
manifest), non-blocking `auto_check` boot check, `config`/`edit` shell
commands, and the `oc_ext_config_*` / `oc_ext_check_update*` L1
interfaces.

| Criterion | Status |
|---|---|
| Compiles 0 errors / 0 warnings (-Wall -Wextra -Werror) | OK |
| ISO builds; BIOS + UEFI boot | OK (config mounted from FAT32 /etc on both) |
| /etc/opencube.conf exists with English defaults | OK (update_url + auto_check=no) |
| checkupdate over http:// and https:// | OK (real fetches, server-side logs) |
| same/new version output format | OK (Current version is up to date. / New version available.) |
| Error paths (config missing, invalid prefix, network, JSON, size) | OK (one-line English messages) |
| auto_check yes/no boot behaviour | OK (async thread, never blocks, skip+log when network not ready) |
| 13-case test matrix with real outputs | OK (docs/VERIFICATION_FIX5.md) |
| WP-09 regression unchanged | OK (18/18, dhtest 5/5, cryptotest 3/3, SSH both ways, HTTPS E2E) |
| Documentation | OK (docs/CONFIG.md, docs/VERIFICATION_FIX5.md, docs/EXTENSIONS_WP09.md §9, docs/KNOWN_ISSUES.md §5) |

Honest limitation recorded: public TLS endpoints reject the kernel TLS
client (WP-09 cipher scope); fix5 works against any server configured
for the documented kernel cipher (tools/update_server.py).
(Superseded 2026-10 by the mainstreamed TLS client: public HTTPS
endpoints verified working - docs/KNOWN_ISSUES.md §5.2.)
