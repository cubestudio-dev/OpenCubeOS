<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 cubestudio-dev <cubestudio@qq.com> -->

# Open Cube OS - Project Status

Updated 2026-10-14 at WP-10-camera_misc. All 19 work packages are
COMPLETE; the auxiliary batches are shipped; the P2 audit batches fix1
through fix4 are COMPLETE - all 214 P2 items landed on main and released
(see Audit batches below).

## Work-package coverage (19/19 COMPLETE)

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
| WP-10-input_chipset | Input devices + chipset: I2C-HID touchpad / PCI-PCIe / ACPI / SMBus / I2C | COMPLETE | docs/EXTENSIONS_WP10-input_chipset.md; record below |
| WP-10-timer_rtc | Time sources: CMOS RTC (IRQ+alarm) / HPET / ACPI PM timer / TSC | COMPLETE | docs/EXTENSIONS_WP10-timer_rtc.md; record below |
| WP-10-wireless_bluetooth | 802.11 core + 7 wireless families + Bluetooth stack + SDIO | COMPLETE | docs/EXTENSIONS_WP10-wireless_bluetooth.md; record below |
| WP-10-camera_misc | UVC cameras + IEEE 1284 parallel + game port + infrared + SCSI (LSI 53C895A) | COMPLETE | docs/EXTENSIONS_WP10-camera_misc.md; record below |

The L1 interface numbering 1-138 closes at WP-10-wp08fix1
(docs/INTERFACES.md is the single-page index).


## WP-10-camera_misc: cameras + parallel + game port + infrared + SCSI (2026-10-14)

**Status: COMPLETE.** Five hardware domains as bare-metal drivers:
UVC cameras (USB Video Class 1.1 descriptor parser over the WP-10d USB
stack, Probe/Commit control, alternate-setting switching, a baseline
JPEG decoder with YCbCr->RGB and a camera core with a 20-interface
registry), the IEEE 1284 parallel port (SPP forward/reverse, EPP 1.7/1.9
address/data cycles, ECP mode+FIFO, LPT1-LPT3 at 0x378/0x278/0x3BC),
the legacy game port (0x200/0x201 RC-timing axis measurement with
calibration, 4 buttons), infrared (CIR over ACPI + SIR over the
secondary 16550 with NEC/RC-5/RC-6/SIRC encode+decode) and SCSI (a
SAM-style command core with CDB builders, response parsers, per-device
queues and UNIT-ATTENTION retry, plus an LSI 53C895A HBA backend that
programs the chip through its PCI I/O BAR and mini-SCRIPTS programs,
including the asynchronous disconnect/reselect protocol for real media
I/O).  Direct-access targets land in the block layer as sda.. and go
through MBR/GPT parsing like every other backend.  10 new shell commands
(uvc, uvctest, parallel, paralleltest, gameport, gameporttest, ir,
irtest, scsi, scsitest; Total 198 -> 208, additions only).

| Criterion | Status |
|---|---|
| Compiles 0 errors / 0 warnings (-Werror) | OK |
| ISO builds; BIOS + UEFI boot | OK |
| Batch E2E (tools/wp10_camera_misc_e2e.py) | OK (12/12 BIOS + 12/12 UEFI; LSI + scsi-hd + parallel chardev verified live) |
| Full regression (tools/run_fix4_regression.py) | OK (28/28; command-count baseline 198 -> 208 documented) |
| dhtest | OK (5/5, TCG; via regression suite) |
| cryptotest NIST | OK (via regression suite) |
| fstest | OK (p2 ALL PASS; via regression suite) |
| Host unit tests | OK (host_camera_test 30/30 at -Os, host_ir_test 21/21, host_scsi_test 49/49) |
| SSH E2E | OK (5/5) |
| HTTPS E2E | OK (7/7) |
| Interfaces frozen (commands added, none removed) | OK (help -a re-verified live; Total: 208 commands) |
| Extension contract (header + comments + default impl + docs) | OK (docs/EXTENSIONS_WP10-camera_misc.md; 51 interfaces) |
| L1 numbering stays closed at 138 | OK (driver APIs + commands, no numbered L1 additions) |

Hardware honesty: UVC binds enumerated USB video devices (QEMU ships
none - the uvc command reports zero cameras honestly, the parser runs
against byte-exact fixtures); CIR/game port have no QEMU model and are
reported absent; SIR is loopback-verified on the emulated 16550; SCSI
runs end-to-end against the QEMU lsi53c895a model with a real
scsi-hd disk (INQUIRY, READ CAPACITY, a WRITE/READ round trip on the
last sector with content save/restore, and LBA 0 through both the blk
layer and raw SCSI).

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
- **WP-10-AUDIT_P2-fix4** - COMPLETE: the final 55 P2 items
  (BUG-0295..0349) fixed in eight groups (G1 d01bf5a, G2 8a08d40, G3
  a4ba09d, G4 c106755, G5 dd0882f, G6 239f298, G7 147adc7, G8 c11c2ee)
  + the regression harness restored into the repo (95195ed). Highlights:
  crypto fail-closed paths (ECDSA on-curve check, X25519 all-zero shared
  secret per RFC 8446, BN bounds before write, zeroed key material on
  allocation failure); HDA BDL encoding resolved against QEMU v10.0.2
  source with a runtime spec switch (config hda.bdl=spec) for real ICH
  silicon; AC97 stop resets the engine via CR.RR (no more ring-tail
  replay/wedge); SB16 ISA-DMA 128 KiB-boundary + 16 MiB validation;
  ES1370 volume rewritten for the AK4531 codec (FreeBSD es137x.c
  reference); IRQ-registration failure no longer brings a device up;
  nano redraw rebuilt (one repaint + explicit cursor move - the old
  print-head trick painted every line twice and cut it at the cursor);
  echo is a pure text command (quoted '>' args no longer treated as
  redirection); sleep means delay (+2.1/+4.1 s measured); checkupdate
  false alarm fixed (numeric build id + git-describe descendant
  fallback, four-scenario QEMU E2E); help -a 176 == 176 static sites,
  two-way diff empty; dhtest completes 5/5 in the sandbox. The fix3
  release ISO was rebuilt AFTER its tag (the shipped one predated the
  tag and showed fix2b-24-g6ee810b in uname); the rebuilt ISO shows
  WP-10-AUDIT_P2-fix3 and replaced the Release asset, the site
  /downloads/ copy and the private archive. Gate: regression 28/28
  (tools/run_fix4_regression.py), SSH E2E 5/5, HTTPS E2E 7/7
  (tools/run_fix4_https_e2e.py), dhtest 5/5, 0 errors 0 warnings.
  `docs/audit/bugs_final.json` remains the authoritative per-finding
  tracker.

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

---

## WP-10-input_chipset: input devices + chipset (2026-10-12)

**Status: COMPLETE.** Five domains wired into the L0 kernel and
exercised live in QEMU on both BIOS and UEFI boots: I2C-HID touchpad
(gesture engine), PCI/PCIe (MMCONFIG + BAR + MSI/MSI-X over the xAPIC
path), ACPI (RSDT/XSDT/FADT/MADT/MCFG/HPET + minimal AML interpreter +
S3/S4/S5 + SCI/GPE), SMBus (full protocol layer incl. block), I2C
(PIIX4 + i801 + DesignWare backends). 16 new shell commands
(Total 176 -> 192, additions only).

| Criterion | Status |
|---|---|
| Compiles 0 errors / 0 warnings (-Werror) | OK (clean rebuild) |
| ISO builds; BIOS + UEFI boot | OK |
| Five-domain E2E (tools/wp10_chipset_e2e.py) | OK (28/28 BIOS + 28/28 UEFI) |
| Full regression (tools/run_fix4_regression.py) | OK (28/28; command-count baseline 176 -> 192 documented) |
| dhtest | OK (5/5, TCG) |
| cryptotest NIST | OK (4/4) |
| fstest | OK (p2 ALL PASS + p1fix4 ALL PASS) |
| SSH E2E | OK (5/5) |
| HTTPS E2E | OK (7/7) |
| Interfaces frozen (commands added, none removed) | OK (help -a re-verified live) |
| Extension contract (header + comments + default impl + docs) | OK (docs/EXTENSIONS_WP10-input_chipset.md) |
| License restored to the official Apache-2.0 text | OK (byte-diff vs apache.org: appendix copyright line only) |
| SPDX coverage | OK (0 files without the header) |

Honest notes: QEMU ships no I2C-HID or DesignWare models, so the
touchpad pipeline is proven by the synthetic touchtest fixtures + the
host regression (tests/host_i2chid_touch_test.c) and DesignWare is
skipped honestly when absent; process-call is unsupported by design
(non-atomic decomposition would corrupt EEPROMs). Eleven live-boot
defects found by the gate are documented with root causes in
docs/EXTENSIONS_WP10-input_chipset.md §4.

## WP-10-timer_rtc: time sources (2026-10-13)

**Status: COMPLETE.** Four time-source domains wired into the L0
kernel and exercised live in QEMU on both BIOS and UEFI boots: CMOS
RTC (status registers + IRQ8 update-ended/periodic/alarm interrupts),
HPET (ACPI table + MMIO driver + one-shot/periodic comparators on
timer 1 via the LEG_RT legacy route), ACPI PM timer (FADT
PM_TMR_BLK + 24-bit-masked wrap arithmetic + 64-bit extension + PIT
calibration), TSC (serialized reads + invariant probe + three-reference
calibration + TSC-driven delay). 4 new shell commands (Total
192 -> 196, additions only).

| Criterion | Status |
|---|---|
| Compiles 0 errors / 0 warnings (-Werror) | OK |
| ISO builds; BIOS + UEFI boot | OK |
| Time-source E2E (tools/wp10_timer_rtc_e2e.py) | OK (29/29 BIOS + 29/29 UEFI) |
| Full regression (tools/run_fix4_regression.py) | OK (28/28; command-count baseline 192 -> 196 documented) |
| dhtest | OK (5/5, TCG) |
| cryptotest NIST | OK (via regression suite) |
| fstest | OK (p2 ALL PASS) |
| SSH E2E | OK (5/5) |
| HTTPS E2E | OK (7/7) |
| Interfaces frozen (commands added, none removed) | OK (help -a re-verified live) |
| Extension contract (header + comments + default impl + docs) | OK (docs/EXTENSIONS_WP10-timer_rtc.md) |
| L1 numbering stays closed at 138 | OK (driver APIs + commands, no numbered L1 additions) |

Honest notes: the HPET legacy route pauses the system tick for the
duration of each test (PIC-only kernel, no IO-APIC yet); the PM timer
is 24-bit effective on this platform (QEMU `d & 0xffffff`, the
PIIX4/ACPI 1.0 convention) with a `pmtimer.width=32` config override;
the CMOS century register is surfaced by `rtc` but not wired into the
WP-09 reader; the TSC is a virtual clock under TCG (invariant flag
honestly 0).  Four live-boot defects found by the gate are documented
with root causes in docs/EXTENSIONS_WP10-timer_rtc.md §4.

## WP-10-wireless_bluetooth: wireless + bluetooth (2026-10-13)

**Status: COMPLETE.** 802.11 core (frame control/IE/beacon parsing,
open-system auth+assoc state machine, LLC/SNAP data bridge) + seven
wireless family drivers (Intel iwlwifi, Realtek rtl8188/8192/8821,
Atheros ath9k/ath10k/ath11k, Broadcom b43/brcmfmac) + a real
SDHCI/SDIO bus + a full Bluetooth stack (HCI core, H4 UART transport
on COM2/IRQ3, HCI-USB btusb class driver, L2CAP signaling, RFCOMM
TS 07.10 mux with real CRC-8 FCS, SDP client, SPP and HID profiles).
2 new shell commands (wifi, bt; Total 196 -> 198, additions only).
Wireless families verified at datasheet level (QEMU has no 802.11
model - byte-exact fixtures + live enumeration paths); Bluetooth
verified END-TO-END in QEMU on real emulated 16550 hardware against
a host-side H4 peer.  The batch's own E2E loop found and fixed 8+
real defects (HCI event offsets, double-framed ACL/L2CAP sends, the
16550 FIFO race, L2CAP buffer overflow, RFCOMM body offset, SDP
walker layout, fixture bugs, shell wp[24] one byte short).

| Criterion | Status |
|---|---|
| Compiles 0 errors / 0 warnings (-Werror) | OK |
| ISO builds; BIOS + UEFI boot | OK |
| Wireless/bluetooth E2E (tools/wp10_wireless_bluetooth_e2e.py) | OK (32/32 BIOS + 32/32 UEFI; real H4 UART peer) |
| Full regression (tools/run_fix4_regression.py) | OK (28/28; command-count baseline 196 -> 198 documented) |
| dhtest | OK (5/5, TCG) |
| cryptotest NIST | OK (via regression suite) |
| fstest | OK (p2 ALL PASS) |
| SSH E2E | OK (5/5) |
| HTTPS E2E | OK (7/7) |
| Interfaces frozen (commands added, none removed) | OK (help -a re-verified live) |
| Extension contract (header + comments + default impl + docs) | OK (docs/EXTENSIONS_WP10-wireless_bluetooth.md) |
| L1 numbering stays closed at 138 | OK (driver APIs + commands, no numbered L1 additions) |

## WP-10-tui (2026-10-15) — COMPLETE

The 20th work package.  TUI framework (terminal abstraction with two
real backends, window system, text/attributes, input with a real ANSI
CSI decoder and SGR mouse, panels, menus, forms) plus three
applications (hardware configuration over 11 domains, wireless
network with WiFi + Bluetooth managers, system information with
overview/processes/logs/monitor/power/update/time/help).  The batch
also closed the 12 handover suspicions (7 repaired in code, 3
documented as design notes, 1 stale, 1 doc-only) and recorded the
258 unremediated P3 audit findings in KNOWN_ISSUES 15.1.

| Gate | Result |
|---|---|
| Build 0 errors 0 warnings | OK (-Werror) |
| ISO boots (BIOS + UEFI) | see the E2E logs under docs/verification/ |
| tui-test self-checks | see tools/wp10_tui_e2e.py run output |
| host test suite (make test) | OK (17 PASS / 2 SKIP-FIXTURE / 0 failed) |
| Regression 28/28 + dhtest + cryptotest + fstest | OK |
| SSH E2E / HTTPS E2E | OK (5/5, 7/7) |
| Commands added only | OK (208 -> 213) |
| L1 numbering stays closed at 138 | OK (59 driver-API interfaces, no numbered additions) |
| Extension contract (header + comments + default impl + docs) | OK (docs/EXTENSIONS_WP10-tui.md) |
| Docs consistency (README/INTERFACES/CONFIG/TRY-IT/KNOWN_ISSUES/STATUS/WORK_LOG/website) | OK |

## WP-10-chinese_support (2026-10-16) — COMPLETE

The 21st work package (WP-10 finale).  Chinese support enters L0:
embedded GB2312 16x16 bitmap font (6763 hanzi + 682 symbols, exact
Unifont rasterization), GB2312 + UTF-8 codec with conversion and
detection, pluggable font registry + mixed-width renderer (cell stays
8x16, CJK spans two cells), console cn write path (stock putc
untouched), UTF-8 aware line editing and cat, TUI Chinese layer with
three Chinese applications and a pinyin IME (3755 level-1 chars /
396 syllables).  9 new commands (213 -> 222).  The numbered L1 set
stays closed at 1-138 (34 driver-API interfaces single-listed in
docs/EXTENSIONS_WP10-chinese_support.md).

| Gate | Result |
|---|---|
| Build 0 errors 0 warnings | OK (-Werror) |
| ISO boots (BIOS + UEFI) | OK (chinese E2E 14/14 both modes) |
| chinesetest in-kernel suite | OK (91/91) |
| Chinese E2E | OK (14/14 BIOS + 14/14 UEFI, tools/wp10_chinese_e2e.py) |
| Pixel-verified CJK rendering | OK (4/4 glyph blocks + 572 title pixels) |
| host test suite (make test) | OK (17 PASS / 0 FAIL / 2 SKIP-FIXTURE) |
| Regression 28/28 + dhtest + cryptotest + fstest | OK (baseline 222) |
| TUI E2E (no regressions) | OK (32/32 BIOS + 32/32 UEFI) |
| SSH E2E / HTTPS E2E | OK (5/5, 7/7) |
| Commands added only | OK (213 -> 222) |
| L1 numbering stays closed at 138 | OK (driver-API interfaces, no numbered additions) |
| Extension contract (header + comments + default impl + docs) | OK (docs/EXTENSIONS_WP10-chinese_support.md) |
| Docs consistency (README/INTERFACES/TRY-IT/STATUS/WORK_LOG/website) | OK |

## WP-10-chinese_support-fix1 (2026-10-16) — COMPLETE

User-reported fix batch on the chinese_support release (8 findings,
each root-caused, fixed and regression-tested):

- 1(a) Chinese input: the Chinese WiFi SSID field misused the pinyin
  editor (every lowercase letter entered the composition buffer) -
  ASCII fields now use a direct ASCII editor
  (tui_chinese_input_ascii); the pinyin IME gained a reachable,
  user-facing entry (tui-cn launcher item 4 中文输入 - a full-screen
  composition pad) and `ime -i` opens a real interactive console
  composition line (the old `ime` command printed usage for an
  interactive flow it did not implement); the Chinese bluetooth page
  gained S/P/U/V (real HCI inquiry / connect / disconnect / remote
  name) matching the English page.
- 1(b) English TUI gaps: the WiFi connect flow asks for the SSID on
  a bottom-line editor (Enter = first scan result, Esc = cancel)
  instead of silently connecting to scan result 0; the sysinfo
  update page runs the real check -> download -> SHA256 verify ->
  install -> set-boot flow through the same OTA APIs as the update
  command instead of telling the user to type it.
- 1(c) the serious one: tui_window_free never released its slot, so
  the 16-slot window pool drained after about eight page visits and
  every later tui_window_new returned NULL - apps dropped back to
  the shell and the TUI could not be started again (the released
  ISO measures 8/12 sessions rendering, then the session dies).
  Slots are now reused, and tui_init/tui_shutdown reset the pool,
  the text-layer caches and the CSI decoder
  (tui_windows_reset/tui_text_reset/tui_input_reset);
  tui_getch_timeout tests c > 0 (the keyboard queue returns -1 when
  empty; the old c != 0 test fed -1 into the CSI decoder, so pages
  exited on the first poll and menus spun); the physical Esc key
  maps through OC_KEY_ESC_KEY (raw 0x1B was swallowed as a CSI
  opener), the serial CSI decoder only treats "ESC [" as a control
  sequence (any other byte after ESC is a plain keypress, so
  "ESC ESC q" can no longer eat the q), and tui-cn gained the
  symmetric tui_shutdown() the five English commands already had.
- 2(a) the update chain: the chinese_support release never generated
  an OTA package, so update.json still pointed at camera_misc
  (build 193 > local 187) and every chinese_support checkupdate
  offered an OLDER build as an update.  The fix1 release ships the
  OTA package and refreshes update.json/update-v2.json; the baked
  build counter is made monotonic again (RELEASE_PACKAGING.md §4).
- 2(b) the website: the about-page timeline was missing the batches
  from WP-10-AUDIT_P2-fix2/fix2b through WP-10-chinese_support and
  the badge/brand/download strings still said WP-10-timer_rtc.
  Both updated; site.ts + update manifests + downloads refresh with
  the fix1 release.

| Gate | Result |
|---|---|
| Build 0 errors 0 warnings | OK (-Werror) |
| fix1 integration checks (fix1_verify.py, live QEMU boot) | OK 7/7; the released chinese_support ISO before the fix measures 1c-B FAIL (8/12 sessions, then session death) |
| chinesetest in-kernel suite (incl. fix1 blocks [13]/[14]) | OK (91/91) |
| Chinese E2E | OK (17/17 BIOS + 17/17 UEFI) |
| TUI E2E (incl. 4 fix1 tui-test checks + the enter/exit/re-enter regression) | OK (BIOS 18/18+20/20, UEFI 18/18+20/20) |
| Regression 28/28 | OK (baseline 222) |
| OTA package + manifest refresh | done at the fix1 release (RELEASE_PACKAGING.md §4) |
| Website rebuild + gh-pages deploy + live check | done at the fix1 release |
| Commands added only | OK (still 222 - no new commands) |
| L1 numbering stays closed at 138 | OK |
| Docs consistency | OK (EXTENSIONS contract rows, TRY-IT, STATUS, WORK_LOG) |
