# 3.0.0-WP06a-ocse — Delivery Report

OpenCubeOS Setup Environment (OCSE): the installer pre-boot
environment, launched by OCBL as a multiboot2 ELF, providing hardware
checks, disk identification, an en/zh bilingual GUI + CLI front-end.

**Public commit**: this delivery lands as two commits on main
(`wp06a: OCSE install environment...` build 364, plus this evidence
batch). **Rebuild note (honest)**: the original WP-06a working session
completed locally but was lost un-pushed to the 8th sandbox reset;
this delivery was rebuilt from the session records. Every number below
was re-achieved live in this tree (145 vendored files, edit groups,
2.2 MB dual-firmware ISO) - nothing is transcribed from the lost tree.

## 1. What was built

### 1.1 Vendoring (single source of truth)

* `tools/vendor_ocse.sh` — the 145-file list. Sources: **tag v1.0.0
  (bdc47047)**, the pre-restructure monolithic kernel. Rationale in
  `ocse/VENDORING.md`: OCSE runs before any OCOS service exists; v1.0.0
  already has every driver an installer needs (fb+font+console, USB
  HID, i2c/SMBus touchpad, ATA block layer + partition scan, PCI,
  ACPI incl. the AML interpreter, HPET/TSC/CMOS time) without the
  WP-03+ service tree. Re-vendor only rebuilds the vendored subtrees;
  the OCSE-specific files at `ocse/` top level survive.
* `tools/ocse_edits.py` — 12 edit groups (E1-E12), idempotent,
  anchor-mismatch aborts loudly. Groups:
  E1 l1_ext OTA API strip · E2 GRUB wrapper strip (disk_setup out of
  the build) · E3 net cut (health counters stay 0) · E4/E5 ring-3 cut
  (crash plumbing stubbed / kill path parked) · E6 int 0x80 dropped
  (ring-0 gate + count-and-drop) · E7 USB MSC/serial cut · E8 shell_lang
  cut · E9 ACPI NIC resume cut · E10/E11 **two base bugs found and
  fixed** (aml_store_value uninitialized `acpi_value_t copy`;
  aml_invoke_method early-returns leaving `*ret_out` unwritten - both
  latent since v1.0.0) · E12 kernel/main.c rewritten into the
  **24-stage OCSE boot ladder** (template `tools/ocse_kmain_c`).

### 1.2 OCSE environment (new units, ocse/ top level)

`ocse_main.c` (mode loop + hotkeys + console suppression) ·
`ocse_i18n.c` (en/zh UTF-8 catalog, GB2312 conversion for the console
renderer) · `ocse_detect.c` (four checks: CPU/long-mode via EFER.LMA,
PMM memory, framebuffer, storage) · `ocse_disk.c` (MBR/GPT tables,
NTFS/exFAT/FAT32/ext4 signature classification, partition-child
filtering, low-space notice) · `ocse_gui.c` (framebuffer panel,
pointer via USB mouse + I2C touchpad, three buttons, UTF-8/CJK glyph
renderer) · `ocse_cl.c` (OCSECL: help/lang/hwcheck/disks/gui/clear/
halt/click) · `linker_ocse.ld` · `ocse.mk` (0 errors 0 warnings at
-Werror, strip --strip-debug).

### 1.3 OCBL integration (three sites)

`ocbl_scan.c`: OCSE marker entries get `path=/boot/ocse.elf` and the
"OpenCubeOS Setup (OCSE)" label. `ocbl_main.c` (BIOS) and
`ocbl/efi/efi_entry.c` (UEFI): OCBL_ENT_OCSE rides the same mb2 ELF
handoff as OCOS (excluded from the chainload branch in both flavors).

### 1.4 Install-medium packaging

`tools/build_ocse_iso.sh`: `/ocse.mrk` marker (the 8.3-safe class
marker, judged FIRST by scan_volume) + `/boot/ocse.elf` + the OCBL
dual-firmware pair, packed through the G13-sealed builder.

## 2. Artifacts (build 364)

| artifact | size |
|---|---|
| build/ocse/ocse.elf  (stripped) | 670,408 B |
| build/ocse.iso (BIOS+UEFI)     | 2,234,368 B |

## 3. Acceptance matrix (T1–T8, all green)

| # | test | result |
|---|---|---|
| T1 | SeaBIOS negative: no disk -> 24-stage ladder -> check4 FAIL -> honest halt | PASS (`ocse_t1_seabios_negative.txt`) |
| T2 | SeaBIOS positive: 64 MiB IDE disk -> 4x PASS -> GUI up | PASS (`ocse_t2_seabios_positive.txt`) |
| T3 | OVMF UEFI positive: GOP handoff -> GUI up | PASS (`ocse_t3_ovmf_uefi.txt`) |
| T4 | GUI/CLI battery x8: hotkeys both ways, hwcheck, disks, lang en/zh, click matrix, gui, screendump | PASS (`ocse_t4_gui_battery.txt` + PPM) |
| T5 | fs signatures FAT32/exFAT/NTFS/ext4 + 32 MiB low-space notice + zh path | PASS (`ocse_t5_fs_signatures.txt`) |
| T6 | OCBL dynamic scan: OCSE + post-install OCOS entries co-exist; OCOS boots to ush | PASS (`ocse_t6_ocbl_dynamic_scan.txt`) |
| T7 | regressions: OCIRE BIOS + OCIRE OVMF + OCOS OVMF | PASS (`ocse_t7_regressions.txt`) |
| T8 | two clean rebuilds byte-identical | PASS (`ocse_t8_reproducibility.txt`) |

## 4. Live-caught during this build (recorded in code comments)

1. `ocse_stage` overlapping strcpy self-replication (the stage-9
   report passed `g_stage_line` as `what`) walked 320 KiB of .bss into
   the boot page tables -> #PF -> #DF -> triple fault. Fixed with a
   stack snapshot.
2. The stock QEMU CPU does not advertise CPUID leaf 0x80000001; the
   CPU check read EFER.LMA instead (the kernel IS running in long
   mode).
3. ext2/3/4 magic lives at byte 1080 - a 2-sector read buffer was an
   out-of-bounds access that -O2 array-bounds caught; now a 3-sector
   read.
4. The touchpad ring contract is `0 = event / -1 = empty` (the USB
   mouse ring is the opposite) - the inverted `while` froze the GUI
   loop on the first empty poll.
5. Partition children register as `<disk>pN` block devices; the
   installer now filters them out of the physical-disk list.

## 5. Known limitations (declared honestly)

* **Pointer-event injection in QEMU is impossible** for the vendored
  USB stack: HMP `mouse_move`/`mouse_button` (active-device routing
  verified), HMP hot-plug + poll enumeration, and QMP
  `input-send-event` all produce **zero guest-side events** - and the
  v1.0.0 tree itself contains no QEMU mouse-input evidence either (the
  runner comment advertises the capability, no test ever exercised
  it). The GUI pointer path (accumulation, clamping, hit-test, action
  dispatch) is therefore accepted through the OCSECL `click x y`
  command, which drives the **same hit_test()** the pointer path uses;
  the USB/touchpad data links are delivered per the vendored v1.0.0
  protocols, verified to compile and be wired, but not
  event-verified under QEMU.
* The i2c-hid touchpad has no QEMU device model at all - same
  situation as above.
* The installer's actual disk-writing stage is WP-06b/06c scope;
  T6 simulates the post-install state with a hand-made disk carrying
  `/boot/opencube.elf` (the exact state the WP-06b installer will
  produce).

## 6. Boundary statement

WP-06a delivers the environment, its boot integration and its
acceptance battery. The interactive installer itself (partitioning,
copying, boot-slot writing) is WP-06b (ocsecl install commands) and
WP-06c (GUI installer flows) - NOT started here.
