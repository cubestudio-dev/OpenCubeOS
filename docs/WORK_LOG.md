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
