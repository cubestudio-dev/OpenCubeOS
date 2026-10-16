<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 cubestudio-dev <cubestudio@qq.com> -->

# Open Cube OS — Public Work Log (WP-09 closeout)

The full engineering log (WP-01 → WP-09, every batch, every bug, every
decision) lives in the private archive repository (`worklog.md`,
cubestudio-dev — the private archive repository, name withheld from
public docs on purpose). This file is the
public summary of the WP-09 closeout batches.

## Batch 14 (2026-09-30) — SSH root-cause fixes + first full WP-09 regression

- SSH client K mismatch root causes fixed (DH group14 prime, sequence
  advance, CBC rolling IV, CHANNEL_DATA parsing).
- sshd hardened: `is_exec`/`password`/`session` NUL-dependence replaced with
  `oc_memcmp`; exit-status reply buffer overflow fixed (16→32 B);
  `shell_execute_captured()` added to shell.c (serial hook chain-restore);
  capture-hierarchy destroy bug fixed.
- Full clean rebuild: 0 errors 0 warnings (-Wall -Wextra -Werror).
- Regression: 18/18 PASS, dhtest 5/5, SSH client E2E + sshd E2E vs paramiko.
- Release: tag `WP-09-batch-14` (ISO + SRC zip). Commit author corrected to
  cubestudio-dev (see Batch B below).

## Batch A (2026-09-30) — SSH closeout assessment

- Both SSH directions re-verified with byte-level K agreement.
- Conclusion: no remaining SSH development items; optional quality items
  (host-key persistence, publickey auth, keepalive, algorithm whitelist)
  deferred to WP-10+ — see docs/KNOWN_ISSUES.md §2A.4.
- No code changes → no Release (no empty releases policy).

## Batch B (2026-09-30) — Regression evidence archived (this repo)

- 18/18 regression re-run in one QEMU session — real outputs archived.
- dhtest 5/5 re-run — real outputs archived.
- HTTPS E2E (first run with full dual-side evidence): kernel TLS 1.2 client
  vs tools/https_test_server.py — handshake, encrypted GET, MAC-verified
  response, `Saved 24 bytes`, body verified.
- SSH both directions re-run: 4/4 + byte-level K match
  (2f4130816e935c6a, len=256).
- Evidence: docs/VERIFICATION_BATCH_B.md + docs/verification/*.log (7 files).
- New tool: tools/https_test_server.py.
- Commit: 90903b6 (author cubestudio-dev). No Release (kernel artifacts
  byte-identical to batch-14; docs+tools only).

## Batch C (2026-09-30) — Documentation set updated to WP-09

- README.md → WP-09 (stats, WP-09 section, tests, downloads).
- MANIFEST.md → WP-09 (structure, how-to-test, line counts).
- VERIFICATION_REPORT.md → WP-09 (§10 added, real evidence).
- docs/KNOWN_ISSUES.md → WP-09 (§2A: 7 WP-09 known items/accepted behaviors).
- docs/STATUS.md → WP-09 status + acceptance table.
- docs/EXTENSIONS_WP09.md created (WP-09 interfaces + evidence).
- docs/INTERFACES.md created (full interface index, real signatures).
- docs/WORK_LOG.md created (this file).

## Batch D (2026-09-30) — Official WP-09 release

- Tag `WP-09` + GitHub Release with ISO + SRC zip assets.
- Pre-release batch tags kept as-is (history preserved).

## Batch E (2026-09-30) — Website + GitHub Pages

- website/ subtree (Next.js static export) + .github/workflows/deploy.yml.
- Verified live at https://cubestudio-dev.github.io/OpenCubeOS/.

## Batch F (2026-09-30) — Privacy / license / SHA256 cross-check

- Privacy scan, Apache-2.0 license header check, 3-way SHA256 comparison
  (local build / GitHub Release assets / website download links).

## Batch G (2026-10-02 ~ 2026-10-04) — WP-10 series documentation

- WP-10a (storage) / WP-10b (NIC) / WP-10u (A/B update) / WP-10c (sound)
  / WP-10d (USB host): each got a README section, an
  docs/EXTENSIONS_WP10*.md contract doc and TRY-IT sections.
- wp10-wp08-shell-and-tools-completion (WP-10-wp08fix1) documented:
  shell line editor + nano/vi editors + VFS links + 7 new syscalls.
- wp10-project-restructure-fix1 documented: one-module-one-directory
  layout, [category]_[specific] naming, libs/.
- wp10d-fix1 / wp10d-fix2 documented (core_power_* shutdown/suspend/
  halt/reboot + shell listing helpers).

## Batch H (2026-10-05 ~ 2026-10-07) — WP-AUDIT-01 documentation

- README Stats / Tests / Download updated for the 18-route audit
  (607 findings) and the P0 (41) / P1 (94) fix batches
  (p0fix1 / p0fix2 / p1fix1..p1fix4).
- docs/KNOWN_ISSUES.md sections 4..7 record those fix rounds.

## Batch I (2026-10-07 ~ 2026-10-11) — P2 fix rounds documentation

- WP-10-AUDIT_P2-fix1 / fix2 / fix2b / fix3 / fix4 (plus the untagged
  fix5 round) documented in README, STATUS and KNOWN_ISSUES
  (sections 8..10); regression harnesses committed under tools/
  (run_fix4_regression.py, run_fix4_https_e2e.py).

## Batch J (2026-10-12) — License restoration

- LICENSE restored to the verbatim official Apache-2.0 text (the only
  difference is the appendix copyright line: cubestudio-dev); NOTICE
  lists GRUB's unicode.pf2 font (GPL-3.0 + font exception); SPDX
  headers verified complete (0 files missing the header).

## Batch K (2026-10-13) — WP-10-input_chipset release + doc consistency

- Five chipset domains (touchpad I2C HID / PCI/PCIe / ACPI / SMBus /
  I2C): README Download section + Stats, STATUS record,
  docs/EXTENSIONS_WP10-input_chipset.md, website constants + FEATURES
  entry (zh+en), GitHub Release assets (ISO + SRC zip), 3-way SHA
  verified (site /downloads/ = Release = local build).
- Doc consistency pass on top of the release: README Stats header +
  command counts (176 -> 192) + work-package count (15 -> 16), website
  i18n wpSectionDesc (15 -> 16), MANIFEST Version (WP-10b ->
  WP-10-input_chipset), KNOWN_ISSUES chipset section (11), TRY-IT
  chipset try-it section (6), and this file brought up to date.

## Batch L (2026-10-13) — WP-10-timer_rtc

- Four time-source domains in `drivers/time/` (+588 G1 / +510 G2 /
  +442 G3 / +427 G4 / +122 G5 lines, commits e2dbd37 / cbe09eb /
  90847c1 / a2bedeb / 0462167):
  - CMOS RTC: status registers (A/B/C/D + FADT century) + IRQ8
    update-ended / periodic / alarm interrupt tests (`rtc`),
  - HPET: ACPI table + MMIO driver, 64-bit counter, one-shot and
    periodic comparators on timer 1 via LEG_RT -> IRQ8 (`hpet`),
  - ACPI PM timer: FADT PM_TMR_BLK, 24-bit-masked wrap arithmetic +
    64-bit extension, PIT calibration (`pmtimer`), `pmtimer.width`
    config key,
  - TSC: serialized reads, invariant probe, PIT/HPET/PMTimer
    calibration + TSC-driven delay (`tsc`).
- Live-boot defects found and fixed: acpi_hpet_t lacked the SDT header
  AND split the block ID into 6 bytes (first consumer ever); PM timer
  misread the QEMU/PIIX4 24-bit wrap as a backward step; TSC-vs-HPET
  calibration spun on a frozen (boot-disabled) HPET counter; HPET
  periodic deadline did not scale with the fire count.
- Gates: build 0/0 (-Werror), regression 28/28 (command-count baseline
  192 -> 196), dhtest 5/5, SSH E2E 5/5, HTTPS E2E 7/7, batch E2E
  29/29 BIOS + 29/29 UEFI (tools/wp10_timer_rtc_e2e.py).
- Docs: README Stats (114,593 lines / 17 work packages / 196
  commands), MANIFEST Version, INTERFACES section 4, CONFIG key
  pmtimer.width, docs/EXTENSIONS_WP10-timer_rtc.md (new), KNOWN_ISSUES
  section 12, TRY-IT section 7, STATUS record, website constants +
  home stats + i18n (zh+en), docs-zh README stats refresh.

## Batch M (2026-10-13) — WP-10-wireless_bluetooth

- G1: 802.11 core (drivers/wireless/driver_wireless.{c,h}) - frame
  control/IE/beacon parsing, open-system auth+assoc, LLC/SNAP bridge,
  family/device registry; tests/host_wifi_test.c 38/38 (found 5 real
  defects during development).
- G2: Intel iwlwifi + Realtek rtl8188/8192/8821 (PCIe + USB
  vendor-request transport); wl_device_probe_all wired into main.c.
- G3: Atheros ath9k/ath10k/ath11k (MAC map + descriptors; CE/HTC/BMI/
  WMI-TLV; MHI/QMI encoders).
- G4: SDHCI/SDIO bus (drivers/sd, real emulated hardware via QEMU
  sdhci-pci) + Broadcom b43/brcmfmac; the 520 KiB static g_tcp_conns
  table moved to the kernel heap (linker identity-window ASSERT;
  image 3.49 MiB, 520 KiB headroom); regression 28/28 after the move.
- G5+G6: Bluetooth stack (HCI core, H4 UART COM2/IRQ3, HCI-USB btusb,
  L2CAP, RFCOMM with real CRC-8 FCS, SDP, SPP, HID).
- G7: wifi/bt commands (Total 196 -> 198), bt_test_peer.py (host-side
  H4 controller + remote device), wp10_wireless_bluetooth_e2e.py,
  host_bt_test.c 22/22, regression baseline 196 -> 198.  The E2E loop
  found and fixed: HCI async-event completion + parameter offsets,
  double-framed ACL/L2CAP sends, the 16550 FIFO race (single-consumer
  ring + trigger 1), L2CAP CFG_REQ overflow, RFCOMM body offset + C/R
  gating, SDP walker layout/stride, non-zeroed iwlwifi fixtures,
  WL_MAX_DEVICES 4 -> 8, shell wp[24] -> wp[32].
- Gates: E2E 32/32 BIOS + 32/32 UEFI, regression 28/28, build 0/0.
- Docs: README Stats (121,697 lines / 18 work packages / 198
  commands), EXTENSIONS_WP10-wireless_bluetooth.md, INTERFACES,
  MANIFEST, KNOWN_ISSUES section 13, TRY-IT section 8, STATUS record,
  website numbers.

## WP-10-camera_misc (2026-10-14)

Five hardware domains as bare-metal drivers + 10 new shell commands
(198 -> 208):
- UVC cameras: drivers/camera/driver_camera.c|h (20-interface core:
  register/unregister/count/get, stream start/stop/format/frame/
  interval, capture/release, brightness/contrast/saturation/exposure/
  focus/zoom, get_caps/get_formats/get_frames), driver_camera_uvc.c|h
  (UVC 1.1 VC/VS descriptor parser, Probe/Commit control, alternate
  setting switching), lib_mjpeg.c|h (baseline JPEG decoder,
  YCbCr->RGB), shell_cmds_camera.c|h + vectors header, tests/
  host_camera_test.c (30/30 at -Os).
- Parallel: drivers/misc/driver_parallel.c|h (9 interfaces: register/
  unregister/count/get, set_mode/get_mode, write/read/get_status +
  EPP address/data + ECP FIFO helpers; SPP forward/reverse, EPP 1.7/
  1.9, ECP mode+FIFO; LPT1 0x378 / LPT2 0x278 / LPT3 0x3BC),
  shell_cmds_pargame.c|h, live SPP verified through `-parallel file:`.
- Game port: drivers/misc/driver_gameport.c|h (7 interfaces: register/
  unregister/count/get, read_axis/read_buttons/calibrate; 0x200/0x201
  RC timing + calibration).
- Infrared: drivers/misc/driver_ir.c|h (8 interfaces: register/
  unregister/count/get, recv/decode/encode/send + NEC/RC-5/RC-6/SIRC
  codecs; CIR over ACPI, SIR over the secondary 16550 loopback-
  verified), shell_cmds_ir.c|h, tests/host_ir_test.c (21/21).
- SCSI: drivers/scsi/driver_scsi.c|h (7-interface SAM core: register/
  unregister/count/get, submit/complete/get_device; CDB builders for
  TUR/INQUIRY/REQUEST SENSE/RC10/RC16/READ10/READ16/WRITE10/WRITE16/
  MODE SENSE6/SYNC CACHE10; INQUIRY/capacity/sense parsers; per-device
  queues; transparent UNIT ATTENTION retry), driver_scsi_lsi.c|h (LSI
  53C895A HBA: PCI I/O BAR programming, mini-SCRIPTS programs —
  SELECT with ATN -> IDENTIFY -> CDB -> a phase-tested data route ->
  STATUS -> a MSG-IN loop draining SAVE POINTER/RESTORE/DISCONNECT/
  COMMAND COMPLETE — with the asynchronous disconnect/reselect
  protocol driven by SIEN0.RSL + SCID.RRE soft enables and a second
  script for the reselected data phase), BLK_TYPE_SCSI in the block
  layer, sda.. naming, MBR/GPT through the existing partition scan,
  shell_cmds_scsi.c|h, tests/host_scsi_test.c (49/49).
- Debug journey (honest): the SCRIPTS bring-up iterated through real
  defects found by the QEMU trace of the lsi model — SELECT with ATN
  encoding (0x47000000 has bit25 set = table-indirect id; the correct
  direct-id form is 0x44000008), the fixed-format sense discriminant
  (0x70 & 0x7C also matches descriptor codes 0x72/0x73), probe-time
  commands needing the submit path armed before registration, the
  UNIT ATTENTION (ASC 0x29) every cold reset leaves queued, the
  sense-fetch allocation length (fixed sense = 18 bytes), the chip's
  CON bit (0x10, not 0x08) surviving an aborted transaction, the
  MSG-IN phase carrying multi-byte messages (SDTR / SAVE / RESTORE /
  DISCONNECT) that a single-byte move leaves queued, and QEMU's
  asynchronous media-I/O model (SAVE DATA POINTER + DISCONNECT ->
  reselect) that a single monolithic script cannot serve.  Final
  architecture: a static script buffer (heap-independent), two
  scripts per asynchronous transaction (command phase + reselected
  data phase), a MSG-IN loop that drains the whole message phase,
  and SIEN0.RSL/SCID.RRE soft enables so the model reselects without
  a hardware IRQ line (polled, like AHCI/NVMe).
- Gates: E2E 12/12 BIOS + 12/12 UEFI (lsi + scsi-hd + parallel
  chardev verified live; scsitest 17/17 incl. the WRITE/READ round
  trip with content restore), regression 28/28 (command baseline
  198 -> 208 documented in the script), build 0/0, host suites 30/30
  + 21/21 + 49/49.
- Docs: README Stats (128,275 lines / 19 work packages / 208
  commands), EXTENSIONS_WP10-camera_misc.md (51 interfaces), INTERFACES
  (per-file counts 56+16+18+7+4+8+18+17+10+9+6+7+16+4+1+1+2+4+2+2 =
  208), STATUS (19/19 COMPLETE + 12-gate table), KNOWN_ISSUES 14
  (hardware-model honesty), TRY-IT 9, CONFIG 8 (no new keys),
  MANIFEST version line, this log.

## WP-10-camera_misc release-chain completion (2026-10-14, session 2)

The five domains and their tests were already on both mains (G1..G8,
public 626f1ae mirroring private b1befff..e908cdd) with the tag and the
public Release 412937591 live; this session closed the remaining release
chain honestly:

- OTA: `opencube-WP-10-camera_misc-update.tar.gz` built by
  tools/make_update_pkg.sh from the kernel ELF extracted out of the
  release ISO itself (xorriso -osirrox), so the package carries exactly
  the released bits. The release kernel's baked build id was pinned
  empirically with checkupdate boundary probes against a local server:
  serving build 192/193 both report "up to date", 194 reports "new
  version available" -> the release kernel carries OC_KERNEL_BUILD=193
  (the private-repo count at a229bc5; the release build happened while
  the G5-G8 changes were still uncommitted in the working tree — the
  public mirror at 626f1ae has public count 178, which is why a
  public-tree rebuild bakes 178; harmless: the OTA rule is
  "remote build strictly greater than local"). update.json and
  update-v2.json refreshed to version WP-10-camera_misc / build 193 /
  package sha256 2fd0a69e...e8ab32ef / size 711844; short "changes"
  stays under the 127-byte legacy limit. Manifest payload digest
  14cbd152... re-verified independently by re-hashing the payload
  bodies in archive order (docs/CONFIG.md, boot/grub.cfg,
  kernel/opencube.elf, etc/opencube.conf).
- Assets committed to the public tree (they had only lived on the
  Release and gh-pages): ISO a07183ac..., SRC 4a32b71a..., update
  package 2fd0a69e... — all four copies (tree, public Release server
  digest, private Release server digest, live gh-pages) verified
  identical.
- Docs: MANIFEST counts recomputed (drivers 16 subdirs incl.
  camera(6)/misc(6)/scsi(4), tools 50, docs 37+audit+verification);
  website i18n wpSectionDesc updated 18 -> 19 work packages + the
  camera_misc name in zh and en (home.ts/site.ts STATS+FEATURES were
  already correct); no other stale current-release pointers found
  (EXTENSIONS_WP10-wireless_bluetooth.md, TRY-IT section 8, INTERFACES
  history lines and KNOWN_ISSUES section 13 legitimately keep the
  wireless name as history).
- Regression baseline: tools/run_fix4_regression.py's "uname current
  tree" check enumerated the release-tag forms but lacked camera_misc —
  the previous session missed the baseline move. Added the camera_misc
  form; full regression now 28/28 on a clean rebuild (before the fix it
  was 27/28 for exactly that reason, not a kernel defect).
- Gates re-run on the clean rebuild at 7e82df5 (kernel sources
  unchanged from the release): 0 errors 0 warnings (-Werror, 18 s);
  SeaBIOS + OVMF boots show "Open Cube OS WP-10-camera_misc x86_64"
  (uname -a) and 208 commands; batch E2E 12/12 BIOS + 12/12 UEFI;
  dhtest 5/5; SSH E2E 5/5; HTTPS E2E 7/7.
- Release flow: private repo synced per SYNC-RULES (the ISO+SRC pair
  e908cdd had put under the top-level source-only website/ mirror moved
  to oc-os/website/public/downloads where binaries live; root website/
  source files re-mirrored; wp10camera_misc/ archive created) and
  pushed e908cdd..1a0ef53; private Release 413007997 created for the
  existing tag with ISO + SRC + update package (server digests match
  the four-way table above); public repo pushed 626f1ae..7e82df5 (the
  remote URL had embedded a stale third token ending rl2B, replaced
  with the current tokenA); gh-pages redeployed c580e00..2c0528d via
  build-local.sh (hash gate BUILD-OK) + rw2_ghpages.sh (note: the
  script's default PUBLIC_DIR layout assumes <repo>/oc-os/website, this
  checkout is the repo root itself, so the documented OC_PUBLIC_DIR
  override was used); live site verified: update.json camera_misc/193,
  /downloads/ ISO+SRC+update.tar.gz all 200 with matching SHA256, zh+en
  home pages show 19 work packages / 128,275 lines / 208 commands /
  camera_misc.

## WP-10-tui (2026-10-15)

Session 1.  The 20th work package: TUI framework + hardware
configuration + wireless network + system information, plus the
handover-driven pre-fix batch.

Pre-fix batch (all 12 handover suspicions dispositioned):

- #1 WiFi RX ring bridged into the TCP/IP stack: the wireless STA
  registers with the driver_nic framework ("wl0"), net_poll drains
  every present framework NIC (was first-only).  Honest DOWN until
  the STA associates.
- #2 the resident USB poll thread actually starts now (the thread
  body lived only inside the USB test command file; kmain called
  driver_usb_poll_start() after the HC probe).
- #3 OTA signature verification: integrity-only today (SHA-256 over
  the package, manifest travels inside it).  Record: RSA-PSS verify
  primitives exist (crypto_rsa.h); the signing scheme needs a key
  custody decision (public key embedded, private key out of every
  git tree) — tracked in KNOWN_ISSUES, deliberately not faked in
  this batch.
- #4 fs/_raw cache bypass: intentional (raw writers + no invalidation
  path); documented in KNOWN_ISSUES 15.2, no code churn.
- #5 ring3 socket API: SYS 103-109 + l1 wrappers (socket ids are
  net-table ids, not process fds — documented).
- #6 access_ok_str bounded to 16 pages; dead sanity line removed.
- #7 fork full-copy + MAX_TASKS=32: documented (KNOWN_ISSUES 15.3);
  COW needs PMM refcounts + a #PF write branch, its own work package.
- #8 EXTENSIONS_WP08a.md now states 8 numbered items bundle 21
  wrapper functions.
- #9 stale suspicion: the Makefile already carries the embed chain
  (userprogs/userprogs-check targets) since the fix1 round.
- #10 real volume: SB16 mixer writes with read-back, UAC1 Feature
  Unit SET_CUR (FU parsed at probe), virtio-snd honest -1.
- #11 `make test`: tools/run_host_tests.py remaps the restructure
  moves; 17 PASS / 2 SKIP-FIXTURE (openssl fixtures) / 0 failed.
- #12 174 stale "File:" header paths rewritten (comment-only commit
  folded into the pre-fix commit).

Number audit (handover request): source lines 128,275 could not be
reproduced at the camera_misc release commit by any caliber; the
README-stated caliber measures 121,286 and README/website/docs-zh
were corrected to it.  208 commands and 46 syscalls were confirmed
exact (the 47 was a double-registration read; SYS_WRITE's legacy
hard-coded entry is dead code by design).

P3 ledger: the 258 unremediated P3 audit findings (BUG-0350..0607)
are now tracked in KNOWN_ISSUES 15.1 (they were previously in
bugs_final.json only).

Token hygiene: public remote unified on tokenA (last4=JGAL); the
historical token (last4=rl2B) appears in WORK_LOG only in last4
form, which is the agreed disclosure level; no full token exists in
any tracked file (git grep verified).

TUI batch: tui/ directory (12 files): core with two real backends
(framebuffer console + ANSI COM1 terminal, 16-color palette with
256-color nearest-entry map), window system, text/attributes, input
(OC_KEY mapping + a real ANSI CSI decoder + SGR mouse + touchpad
path), panels, menus, forms; hardware configuration (11 domains
through the live driver query APIs, lib_config persistence); wireless
(wifi scan/list/connect through the real STA state machine +
Bluetooth manager through the real HCI stack); system information
(overview/processes through the live task table/log ring viewer/live
monitor/power/update/time/help).  Five shell commands (tui, tui-test,
tui-hw, tui-net, tui-sysinfo).  lib_log gained a 64-line boot-log
ring.  tui-test runs 40+ non-interactive checks with
input/expected/actual/verdict output.

Gates at session 1: build 0 errors 0 warnings (-Werror); host suite
17/0/2; ISO built; QEMU BIOS/UEFI E2E + full regression pending the
sandbox toolchain completion (tracked; not claimed).

Session 2 (same day).  The sandbox toolchain finished, so every gate
that session 1 left open was actually run - nothing below is claimed
without a captured output in this sandbox.

- TUI E2E (tools/wp10_tui_e2e.py): 32/32 BIOS AND 32/32 UEFI.  Getting
  there required three driver fixes, all in tools/wp10_tui_e2e.py:
  the verdict log is now line-buffered (a killed run keeps its
  verdicts), full-screen page commands are followed by a `q` keypress
  so the page's event loop returns to the shell, and assertions run
  against ANSI-stripped whitespace-normalized text (the serial backend
  positions each cell, so raw text is not matchable).  The driver also
  seeds `tui.backend=serial` through the real config path (echo >>
  /etc/opencube.conf) and the UEFI boot moved to the standard OVMF 4M
  pflash pair (CODE read-only + a fresh VARS copy per run).  The
  previous "wifi nic bridged: nic" assertion was wrong for QEMU - the
  legacy e1000 never joins the nic framework and wl0 cannot exist
  without 802.11 hardware, so it now asserts the honest `wifi` output
  instead; the bridge itself is covered by the host suite
  (host_wifi_test + host_wifi_nic_stub.c).
- Two real kernel-side gaps surfaced by the E2E and fixed in-tree:
  `g_wp_order[]` (help -w) lacked both WP-10-camera_misc and
  WP-10-tui, so those commands fell into the "other" bucket; and the
  E2E's tui-hw/tui-net/tui-sysinfo page commands hung forever because
  the pages stay in their event loop by design - the E2E now presses
  `q`, the kernel behavior is unchanged (Tab pages, Esc/q quits).
- Full regression (tools/run_fix4_regression.py): 28/28 (the
  command-count baseline moved 208 -> 213 in the script, with the
  usual additions-only note).  dhtest 5/5 (g^0, 1^x, group14 truth
  e/K2, determinism IDENTICAL, scale sweep 8..256 - output captured).
  Host suite `make test`: 17 PASS / 0 FAIL / 2 SKIP-FIXTURE.
- SSH E2E (tools/ssh_mainstream_test.py): 5/5.  The per-installation
  RSA-2048 host key cannot be generated inside the sandbox in one
  sitting (the sshd-hkgen thread needs far more TCG minutes than a
  single background process is allowed to live; two attempts were
  killed by the sandbox with the key incomplete).  The build/etc.img
  volume was therefore provisioned with tools/host_keygen_etc.py
  (openssl on the host, same 512-byte n||d format, load path taken -
  sshd logs "host key: per-installation (1 authorized key)"); these
  are local E2E-only keys, not shipped anywhere.
- HTTPS E2E (tools/run_fix4_https_e2e.py): 7/7.  One intermittent
  handshake failure was observed during the loop (server answered the
  ClientHello through ServerKeyExchange, then the kernel reported
  TLS failed (code 1) before the first record was processed); with
  TLS_DBG=1 the same boot completes the whole handshake, and five
  further TLS_DBG=0 runs all pass end to end.  Nothing in net_tls.c
  changed since the camera_misc release (comment-only), so this is
  recorded as an unreproduced timing-sensitive observation rather
  than a regression: see KNOWN_ISSUES 15.5.
- Stats caliber correction: the 121,286 figure recorded in session 1
  was taken with a non-recursive git pathspec that silently missed
  kernel/**/*.S; the find-based verify command also swept in the
  generated grub_boot_data.c (10,335 lines).  The corrected caliber -
  git-tracked .c/.h/.S under kernel/boot/userprogs/fs/net/shell/l1/
  drivers/libs/tui - measures 121,890 lines (304 files) and README,
  website docs-zh and the site stats block were moved to it.
- Website: STATS moved to 121,890 / 53 syscalls / 213 commands /
  20 work packages (zh+en); FEATURES gained the missing
  WP-10-camera_misc entry plus the WP-10-tui entry (zh+en).
- Regression driver baseline note and the E2E driver changes above
  are part of this session's commits.

## WP-10-chinese_support (2026-10-16)

Batch: GB2312 font + encoding + CJK rendering + Chinese TUI + pinyin
IME (the 21st work package; WP-10 finale).

- chinese/ module: chinese_font (slot registry + zone/Unicode lookup
  both directions + status), encoding (GB2312 + strict RFC 3629
  UTF-8 + conversion through the Unicode pivot + heuristic
  detection), render_cn (font registry + mixed-width renderer: cell
  stays 8x16, CJK spans two cells, ASCII output byte-identical),
  console_cn (cn write path mirroring the putc state machine; stock
  putc untouched; new screen_console_scroll_up), tui_chinese (UTF-8
  windows/printing, CJK-aware menu/form drivers, pinyin IME 3755
  chars / 396 syllables), three Chinese applications
  (tui_cn_hw/tui_cn_net/tui_cn_sysinfo) on the live driver data
  sources, chinese_cmd (9 commands) + chinese_test (91 checks).
- Generators: tools/gen_chinese_font.py (Unifont rectangle-outline
  exact rasterization -> chinese_font_data.c: 6763+682 glyphs,
  zone/unicode index tables), tools/gen_pinyin_table.py (pypinyin ->
  pinyin_data.c).
- Integration: UTF-8 line editing (character boundaries +
  display-width redraw), serial bridge +0x100 bias for 8-bit bytes,
  cat via console_cn_puts_enc, tui_print UTF-8 decode +
  TUI_WIDE_CONT wide cells, fb/serial backend wide handling,
  g_wp_order tag, Makefile KERNEL_DIRS + chinese, boot-time
  render_chinese_init + console_cn_init.
- Gates on the final tree: 0E0W (-Werror); chinesetest 91/91;
  chinese E2E 14/14 BIOS + 14/14 UEFI; TUI E2E 32/32 BIOS + 32/32
  UEFI; regression 28/28 (baseline 222); dhtest 5/5; SSH E2E 5/5;
  HTTPS E2E 7/7; host 17/0/2; pixel-verified rendering (4/4 glyph
  blocks + 572 title pixels).  125,776 lines of source.  Public tip
  88e0ed4 mirrors private 3cc373c; commit-author hygiene re-verified
  (the 2026-10-15 filter-branch rewrote 3 public + 7 private commits
  to cubestudio-dev <cubestudio@qq.com>, force-pushed both repos).

## WP-10-chinese_support-fix1 (2026-10-16)

User-reported fix batch on the chinese_support release: 8 findings
(1a/1b/1c on the TUI, 2a on the update chain, 2b on the website),
each root-caused, fixed, and covered by a regression check.

TUI session lifecycle (1c - the serious one):
- tui_window_new now reuses freed slots (tui_window_free only marked
  id = -1 and the 16-slot pool never shrank - after ~8 page visits
  every tui_window_new returned NULL, apps fell back to the shell and
  the TUI could not be started again; the released ISO measures 8/12
  enter/exit rounds before the session dies).  tui_windows_reset()
  empties the pool at tui_init/tui_shutdown; tui_text_reset() drops
  the cached focus/status-window pointers; tui_input_reset() resets
  the CSI decoder and the mouse pending flag (tui_cn also gained the
  symmetric tui_shutdown() the five English commands already had).
- tui_getch_timeout tests c > 0: the keyboard queue returns -1 when
  empty, and the old c != 0 test fed -1 into the CSI decoder (map_key
  maps it to CTRL_A-1 == -1), which turned every getch into an
  instant -1 - pages exited on the first poll and menus spun.
- The physical Esc key maps through the new OC_KEY_ESC_KEY (0x9C):
  raw 0x1B from the PS/2 scancode table was swallowed by the CSI
  decoder as a sequence opener, so Esc never reached any application.
- The serial CSI decoder now follows ANSI properly: a control
  sequence is ESC [ <params> <final>; until the '[' arrives, every
  other byte is a plain keypress.  "ESC ESC q" (leave a page, then
  quit a launcher) used to swallow the q as an unknown-CSI final byte
  and the launcher hung forever.

Chinese input (1a):
- The Chinese WiFi SSID field uses the new tui_chinese_input_ascii()
  (the pinyin editor turned every lowercase letter into a composition
  buffer - even "home" was impossible to type).
- New full-screen pinyin composition pad tui_cn_input_app() as the
  launcher's 4th entry 中文输入 (letters, digits 1-9, Space = first,
  Enter commits to the pad, Esc leaves) - previously the IME had no
  user-reachable surface.
- `ime -i` opens a real interactive console composition line
  (ime_console_compose): the old command printed usage text for an
  interactive flow that did not exist.  Same key contract as the TUI
  pad; redraws in place through \r on both fb and serial.
- The Chinese bluetooth page gained S/P/U/V (bt_hci_inquiry /
  create_connection / disconnect / remote_name) matching the English
  page; the Chinese hardware page gained S/L (config save/load).

English TUI gaps (1b):
- The WiFi connect flow asks for the SSID on a bottom-line editor
  (net_ask_line: Enter = first scan result, Esc = cancel) instead of
  connecting to scan result 0 unconditionally.
- The sysinfo update page runs the real check -> download ->
  SHA256-verify -> install -> set-boot flow through the same OTA APIs
  as the update command, with honest per-step return codes, instead
  of printing "use the update command".
- ota_update.h: the OC_RELEASE_VERSION fallback (source-zip builds
  without .git) no longer reports the eight-work-package-old
  "WP-10u" - it reports WP-10-chinese_support.

Update chain (2a): the chinese_support release never generated an OTA
package, so update.json still pointed at camera_misc (build 193) and
chinese_support ISOs (build 187) were offered an OLDER build as an
update.  The fix1 release ships the OTA package, refreshes
update.json/update-v2.json, and re-monotonizes the baked build
counter (RELEASE_PACKAGING.md §4).

Website (2b): about-page timeline gains the missing batches
(WP-10-AUDIT_P2-fix2/fix2b through WP-10-chinese_support, zh + en);
i18n badge/brand/download strings move to the current release.
site.ts constants + update manifests + downloads refresh at the
fix1 release.

New regression coverage: chinesetest blocks [13]/[14] (ASCII editor,
IME pad, console IME compose/verbatim-digits/cancel), tui-test fix1
block (injected key immediate, empty queue blocks the full budget,
40 new/free pool cycles + honest 16-pool fill/refuse/reuse, Esc via
OC_KEY_ESC_KEY, serial double-ESC), the E2E re-enter regression
(two full TUI sessions), and 3 new chinese E2E checks (ime -i real
input, the pad, tui-cn re-enter).

Gates on the fix1 tree: 0E0W (-Werror); fix1_verify 7/7 (released
ISO before-fix: 1c-B FAIL 8/12 + session death); chinesetest 91/91;
chinese E2E 17/17 BIOS + 17/17 UEFI; TUI E2E BIOS 18/18+20/20 +
UEFI 18/18+20/20; regression 28/28 (baseline 222).
