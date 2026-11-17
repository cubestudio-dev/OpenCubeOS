<!-- SPDX-License-Identifier: Apache-2.0 -->
# QEMU coupling inventory

Every registration of an accommodation to the QEMU
acceptance platform in the engineering tree, collected by
`tools/audit/scan_qemu_coupling.sh` (read-only scan) and
classified by `tools/audit/curate_qemu_inventory.py`.
Collection only - nothing was modified, moved or deleted
by the audit.

## Method and coverage

- Scanner: `tools/audit/scan_qemu_coupling.sh` (A/B
  pattern set + a catch-all mention class; per-file
  coverage record emitted alongside).
- Classification: rule-based by zone and shape, plus a
  hand-curated override table - every override entry was
  verified by reading the code at that line.
- Boundary rule honored: a driver written to a published
  virtual-hardware SPEC (virtio, xHCI class code, PIIX4,
  i6300ESB - all real parts/specs) is NOT counted as
  coupling; such entries are recorded as observations or
  excluded with a stated reason.

## Reconciliation

| Measure | Count |
|---|---|
| Raw scanner hits | 2338 |
| Classified entries | 2338 |
| A/B-class entries (significant) | 212 |

Class distribution: `{"DOC": 1426, "CMT": 664, "B6": 97, "B4": 12, "B2": 60, "FIX": 10, "B3": 4, "A5": 11, "B1": 24, "A1": 4, "TOOL": 26}`

## Live verification (A/B entries actually firing)

Four audited entries were triggered in a live boot of the
release ISO with the coupling-relevant devices attached
(transcripts: `evidence/qemu-coupling-audit/`):

| Class | Entry | Live proof (verbatim console line) |
|---|---|---|
| A1 | `real_hw_test` CPUID hypervisor branch | `[real_hw_test] actual: hypervisor bit set (QEMU/sandbox)` |
| A5 | virtio-blk 1AF4:1004 PCI-ID match | `[virtio_blk_test] capacity nonzero (legacy cfg @0x14) => PASS` |
| B2 | e1000 legacy register-init path | `e1000: HWaddr 52:54:00:12:34:56 ... link: UP` |
| B2 | e1000 S3 resume replay (existing rings, same MAC) | `SLEEP_ARMED set ... Resumed from S3 ... e1000: (same MAC) link: UP` |

## Family analyses (per category)

### A5

PCI-ID whitelists: the virtio (1AF4:1059/1004), qemu-xhci (1b36:000D) and EHCI entries ride CLASS-driven matches that cover the QEMU model AND real silicon (xhci: class 0x0C0330 'covers qemu-xhci, nec-usb-xhci, real HW'; i801 PIIX4 8086:7113 is a real Intel part). Per the boundary rule these are standard-spec drivers, recorded as observations.

### B2

The e1000/e1000e family comments (bus-master-last ordering, RDT ring-full rule) document register orderings QEMU's models require; the same orderings are within the Intel datasheet sequences, so real NICs execute them unchanged.

### B4

Firmware-behavior notes (ACPI 1.0 RSDT pointer, AML subset, SeaBIOS resume-vector convention, OVMF load-address relocation) describe what the QEMU firmwares do; the code paths are spec-driven (RSDP/XSDT parse, relocation-safe PE) and the notes record where the behavior was observed.

### B6

Acceptance batteries drive QEMU from the host; the OS image itself carries no emulator dependency.

## Significant entries (verified by code reading)

| File | Line | Class | Real-machine impact | Blocking? |
|---|---|---|---|---|
| `ocbl/efi/efi_entry.c` | 353 | B4 | the UEFI entry calls SetWatchdogTimer(0) before ExitBootServices - OVMF arms a 5-minute firmware watchdog; real UEFI firmware also arms it and the same call disarms it (a UEFI-spec service) | no (spec service, required on real firmware too) |
| `ocire/ocire_wdt.c` | 62 | B2 | the comment describes QEMU's MMIO BAR0; the code accepts BOTH the MMIO and the I/O BAR flavors ('real 6300ESB boards may use an I/O BAR - both are accepted here') | no (explicit real-machine handling) |
| `ocos/drivers/audio/drv_audio_hda.c` | 498 | B3 | RIRBCTL.IRQ_EN stays on because QEMU's HDA model only clears its response counter when the guest write-clears RIRBSTS.IRQ (which requires the enable); the controller IRQ line stays quiet because INTCTL.CIE is off - real HDA controllers accept the same setting | no |
| `ocos/drivers/bus/drv_bus_acpi_power.c` | 225 | B1 | the resume trampoline page 0x8000 is placed below the EBDA whose top is cited as 0x9FC00 'on QEMU'; firmware with a higher EBDA could overlap the page | possible (S3 resume on non-QEMU firmware must re-check the EBDA top) |
| `ocos/drivers/bus/drv_bus_pcie.c` | 273 | B1 | unassigned-BAR allocation starts at 0xC0000000 with a comment placing it below the QEMU ECAM window; a real machine with dense low-MMIO firmware reservations could collide with the pool | possible (real-machine bring-up must re-derive the pool from the firmware MMIO map) |
| `ocos/drivers/usb/drv_usb_ehci.c` | 626 | B3 | the port-speed decode treats an EHCI-enabled port as high-speed (QEMU sets PED at reset completion only for HS devices; standard EHCI silicon is HS-only per spec - the Linux driver does the same) | no |
| `ocos/kernel/arch/arm64/boot.S` | 10 | B1 | arm64 bring-up parks secondary harts as QEMU -smp presents them at 0x40000000; the port targets the QEMU virt machine only (declared bring-up scope) | yes for real arm64 hardware (by design, bring-up) |
| `ocos/kernel/arch/arm64/kernel_main.c` | 11 | B1 | arm64 console is the QEMU virt PL011 at 0x09000000 (primecell per the ARM spec, but the address is the virt-machine layout) | yes for real arm64 hardware (by design, bring-up) |
| `ocos/kernel/arch/arm64/kernel_main.c` | 21 | B1 | same PL011 base constant | yes for real arm64 hardware (by design) |
| `ocos/kernel/arch/riscv/boot.S` | 6 | B1 | riscv bring-up assumes the QEMU virt machine loads the image at 0x80000000 in M-mode | yes for real riscv hardware (by design, bring-up) |
| `ocos/kernel/arch/riscv/kernel_main.c` | 11 | B1 | riscv console is the QEMU virt NS16550A at 0x10000000 | yes for real riscv hardware (by design) |
| `ocos/kernel/arch/riscv/kernel_main.c` | 23 | B1 | same NS16550A base constant | yes for real riscv hardware (by design) |
| `ocos/services/block/src/svc_block_cmds_test.c` | 573 | A1 | real_hw_test branches on the CPUID hypervisor bit; the bare-metal path exists and is complete (prints the AHCI/ATA/NVMe/NIC/sound/USB driver states) | no |
| `ocos/services/block/src/svc_block_cmds_test.c` | 582 | A1 | same branch: under a hypervisor the command reports NOT RUN (needs bare metal) instead of the driver report | no |
| `ocos/services/block/src/svc_block_cmds_test.c` | 583 | A1 | report text of the hypervisor branch | no |
| `ocos/services/crash/src/svc_crash_watchdog.c` | 154 | B1 | comment cites the QEMU ACPI table placement (0x400000-0x600000) for the RSDT read window; the read goes through the kernel identity map of the cached table phys, which the MCFG/RSDT parse discovered | no (the phys address is parsed, not hardcoded) |
| `ocos/services/crash/src/svc_crash_watchdog.c` | 206 | B1 | the TCO probe reads the LPC bridge at the fixed slot 00:1f.0 (Intel convention, q35 ICH9 cited); non-Intel chipsets place the LPC elsewhere | possible (TCO watchdog absent on non-Intel boards; the i6300esb path is the fallback) |
| `ocos/services/net/src/svc_net_core.c` | 6394 | B2 | the S3 resume replay re-programs the legacy e1000 (8086:100E, QEMU's default NIC) against the surviving rings; the replay mirrors the init sequence exactly and keeps the same MAC - a real NIC that survives suspend differently would still be re-programmed through the same path | no |
| `ocos/services/power/src/svc_power_core.c` | 133 | B1 | the hardcoded 0x604/0xB004/0xF000 writes run only AFTER the primary FADT+\_S5 ACPI path fails; real machines take the ACPI path | no (fallback only) |

## Boundary-rule exclusions (false positives, with
reasons)

| File | Line | Reason |
|---|---|---|
| `ocire/generated/ocire_cn_font_data.c` | 7774 | font glyph data: the bytes are CJK glyph indexes that coincide with the i440FX device-id constant |
| `ocire/generated/ocire_cn_font_data.c` | 8541 | same font-data coincidence |
| `ocos/drivers/input/drv_input_i2chid.h` | 15 | the comment notes QEMU has no i2c-hid model; the probe path is identical on real hardware (finds zero devices under QEMU and reports honestly) |
| `ocos/services/camera/src/svc_camera_cmds_scsi.c` | 193 | parser golden-check fixture: synthetic QEMU-style INQUIRY bytes fed to the generic parser; no runtime device-identity matching |
| `ocos/services/camera/src/svc_camera_cmds_scsi.c` | 199 | same fixture (vendor field bytes) |
| `ocos/services/camera/src/svc_camera_cmds_scsi.c` | 200 | same fixture (product field bytes) |
| `ocos/services/camera/src/svc_camera_cmds_scsi.c` | 205 | same fixture (expected parse result) |
| `ocse/chinese/chinese_font_data.c` | 7774 | same font-data coincidence |
| `ocse/chinese/chinese_font_data.c` | 8541 | same font-data coincidence |
| `ocse/drivers/input/driver_input_i2chid.h` | 15 | same as the OCOS-side note |

The complete per-hit register (every raw hit with its
classification) is the machine-readable output of the
curator; the counts above reconcile hit-for-hit.
