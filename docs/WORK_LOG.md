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
