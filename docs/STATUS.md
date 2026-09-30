<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 cubestudio-dev <cubestudio@qq.com> -->

# Open Cube OS - WP-09 Project Status

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
- Shell: ssh, sshd, route, arp, firewall, tcpstats, dns (68 commands total)
- userprogs: mprotect_test.asm, p3_test.asm
- Test tools: tools/qemu_runner.py, sshd_test.py, paramiko_sshd.py, https_test_server.py

### Known / accepted behaviors

See docs/KNOWN_ISSUES.md §2A (7 items: close_notify, server Finished,
stale comment, no cert/host-key verification, paramiko probe, QEMU modexp
speed, crashlog tick display).

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
