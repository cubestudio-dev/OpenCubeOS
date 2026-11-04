#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - WP-06a OCSE vendoring (single source of truth).
#
# Vendors a pre-1.0.0 kernel subset from tag v1.0.0 (bdc47047) into
# ocse/. OCSE = OpenCubeOS Setup Environment, the installer pre-boot
# environment (WP-06a). Re-running this script rebuilds ocse/ from the
# pristine v1.0.0 sources; ALL modifications on top are applied by
# tools/ocse_edits.py afterwards (never edit vendored files by hand).
#
# Why v1.0.0 as the base: OCSE needs a small, self-contained kernel that
# predates the L0/L1 restructure (WP-03) - the installer runs before any
# OS services exist, and the v1.0.0 monolith provides exactly the
# drivers an installer needs (framebuffer + font, USB HID, i2c/touchpad,
# ATA block layer, PCI, ACPI incl. the AML interpreter for power ops,
# HPET/TSC time) without dragging in the OCOS service tree. See
# ocse/VENDORING.md.
set -e
cd "$(dirname "$0")/.."

SRC_TAG=v1.0.0
DST=ocse

FILES=(
  # ---- kernel: arch/x86_64 (ring-3 + S3 entries excluded on purpose) ----
  kernel/arch/x86_64/arch_abi_probe.S
  kernel/arch/x86_64/arch_context_switch.S
  kernel/arch/x86_64/arch_exc_exit.S
  kernel/arch/x86_64/arch_exceptions.c
  kernel/arch/x86_64/arch_exceptions.h
  kernel/arch/x86_64/arch_idt.c
  kernel/arch/x86_64/arch_idt.h
  kernel/arch/x86_64/arch_idt_load.S
  kernel/arch/x86_64/arch_idt_stub.S
  kernel/arch/x86_64/arch_irq.c
  kernel/arch/x86_64/arch_irq.h
  kernel/arch/x86_64/arch_lapic.c
  kernel/arch/x86_64/arch_lapic.h
  kernel/arch/x86_64/arch_multiboot2.c
  kernel/arch/x86_64/arch_multiboot2.h
  kernel/arch/x86_64/arch_pic.h
  kernel/arch/x86_64/arch_acpi_s3.S
  kernel/arch/x86_64/arch_acpi_s3_resume.S
  # ---- kernel: core (syscall/usermode/userprogs/g7/g8 excluded) ----
  kernel/core/core_crash.c
  kernel/core/core_crash.h
  kernel/core/core_health.c
  kernel/core/core_health.h
  kernel/core/core_power.c
  kernel/core/core_power.h
  kernel/core/core_rtc.c
  kernel/core/core_rtc.h
  kernel/core/core_sched.c
  kernel/core/core_sched.h
  kernel/core/core_sync.c
  kernel/core/core_sync.h
  kernel/core/core_timer.c
  kernel/core/core_timer.h
  kernel/core/core_watchdog.c
  kernel/core/core_watchdog.h
  # ---- kernel: lib ----
  kernel/lib/lib_config.c
  kernel/lib/lib_config.h
  kernel/lib/lib_gzip.c
  kernel/lib/lib_gzip.h
  kernel/lib/lib_klog.c
  kernel/lib/lib_klog.h
  kernel/lib/lib_log.c
  kernel/lib/lib_log.h
  kernel/lib/lib_string.c
  kernel/lib/lib_string.h
  kernel/lib/lib_tar.c
  kernel/lib/lib_tar.h
  # ---- kernel: mem ----
  kernel/mem/mem_heap.c
  kernel/mem/mem_heap.h
  kernel/mem/mem_pmm.c
  kernel/mem/mem_pmm.h
  kernel/mem/mem_vmm.c
  kernel/mem/mem_vmm.h
  # ---- kernel: entry ----
  kernel/main.c
  kernel/types.h
  # ---- boot: multiboot2 entry (GRUB-era helpers excluded) ----
  boot/boot.S
  boot/long_mode_init.S
  boot/multiboot2_header.S
  # ---- fs: signature/ro support for disk identification ----
  fs/fs_exfat.c
  fs/fs_exfat.h
  fs/fs_ext4.c
  fs/fs_ext4.h
  fs/fs_fat32.c
  fs/fs_fat32.h
  fs/fs_ramfs.c
  fs/fs_ramfs.h
  fs/fs_vfs.c
  fs/fs_vfs.h
  # ---- l1: extension hooks + self-test (reworked into ocse_hooks by edits) ----
  l1/l1_ext.c
  l1/l1_ext.h
  l1/l1_selftest.c
  # ---- drivers: acpi (incl. AML interpreter) ----
  drivers/acpi/driver_acpi.c
  drivers/acpi/driver_acpi.h
  drivers/acpi/driver_acpi_aml.c
  drivers/acpi/driver_acpi_aml.h
  drivers/acpi/driver_acpi_power.c
  drivers/acpi/driver_acpi_power.h
  # ---- drivers: block (ATA + partitions; NVMe/AHCI/virtio kept for real HW) ----
  drivers/block/driver_block_ahci.c
  drivers/block/driver_block_ahci.h
  drivers/block/driver_block_ata.c
  drivers/block/driver_block_ata.h
  drivers/block/driver_block_ata_dma.c
  drivers/block/driver_block_ata_dma.h
  drivers/block/driver_block_blk.c
  drivers/block/driver_block_blk.h
  drivers/block/driver_block_cache.c
  drivers/block/driver_block_cache.h
  drivers/block/driver_block_disk_setup.c
  drivers/block/driver_block_disk_setup.h
  drivers/block/driver_block_nvme.c
  drivers/block/driver_block_nvme.h
  drivers/block/driver_block_part.c
  drivers/block/driver_block_part.h
  drivers/block/driver_block_virtio_blk.c
  drivers/block/driver_block_virtio_blk.h
  # ---- drivers: display (fb + font + console) ----
  drivers/display/screen_console.c
  drivers/display/screen_console.h
  drivers/display/screen_console_in.c
  drivers/display/screen_console_in.h
  drivers/display/screen_fb.c
  drivers/display/screen_fb.h
  drivers/display/screen_font.c
  drivers/display/screen_font.h
  drivers/display/screen_font_data.c
  drivers/display/screen_serial_in.c
  drivers/display/screen_serial_in.h
  # ---- drivers: i2c (touchpad path) ----
  drivers/i2c/driver_i2c.c
  drivers/i2c/driver_i2c.h
  drivers/i2c/driver_i2c_dw.c
  drivers/i2c/driver_i2c_dw.h
  drivers/i2c/driver_i2c_i801.c
  drivers/i2c/driver_i2c_i801.h
  drivers/i2c/driver_smbus.c
  drivers/i2c/driver_smbus.h
  # ---- drivers: input (kbd + i2c hid) ----
  drivers/input/driver_input_i2chid.c
  drivers/input/driver_input_i2chid.h
  drivers/input/driver_input_keyboard.c
  drivers/input/driver_input_keyboard.h
  # ---- drivers: pci ----
  drivers/pci/driver_pci.c
  drivers/pci/driver_pci.h
  drivers/pci/driver_pcie.c
  drivers/pci/driver_pcie.h
  # ---- drivers: time ----
  drivers/time/driver_cmos_rtc.c
  drivers/time/driver_cmos_rtc.h
  drivers/time/driver_hpet.c
  drivers/time/driver_hpet.h
  drivers/time/driver_pmtimer.c
  drivers/time/driver_pmtimer.h
  drivers/time/driver_tsc.c
  drivers/time/driver_tsc.h
  # ---- drivers: usb (host controllers + HID; serial/audio/msc excluded) ----
  drivers/usb/driver_usb.c
  drivers/usb/driver_usb.h
  drivers/usb/driver_usb_ehci.c
  drivers/usb/driver_usb_ohci.c
  drivers/usb/driver_usb_xhci.c
  drivers/usb/driver_usb_hid.c
  drivers/usb/driver_usb_hid.h
  # ---- chinese: font + render (OCSE zh locale) ----
  chinese/chinese_font.c
  chinese/chinese_font.h
  chinese/chinese_font_data.c
  chinese/console_cn.c
  chinese/console_cn.h
  chinese/encoding.c
  chinese/encoding.h
  chinese/render_cn.c
  chinese/render_cn.h
)

if ! git rev-parse -q --verify "$SRC_TAG" >/dev/null; then
    if git cat-file -t bdc47047 >/dev/null 2>&1; then
        git tag "$SRC_TAG" bdc47047
    else
        echo "vendor_ocse: tag $SRC_TAG (or bdc47047) not found locally." >&2
        echo "  fetch it: git fetch origin refs/tags/v1.0.0:refs/tags/v1.0.0 --depth=1" >&2
        exit 1
    fi
fi

# Only the vendored subtrees are rebuilt; OCSE-specific files at the
# ocse/ top level (ocse_*.c, ocse.mk, linker_ocse.ld) SURVIVE a re-vendor.
#
# WP06f: OCSE-native additions that live INSIDE the vendored subtrees
# are stashed aside before the rm -rf and restored after, so a re-vendor
# no longer deletes them:
#   drivers/input/driver_input_ps2mouse.{c,h}  (WP-06c GUI pointer)
#   chinese/chinese_font_data.c                 (WP06f regenerated font:
#     the fixed gen_chinese_font.py rasterizer output; the v1.0.0 copy
#     carries the pre-WP06f baseline bug - NEVER let a re-vendor restore
#     it. tools/ocse_edits.py E20 guards this as well.)
PRESERVE=(
  drivers/input/driver_input_ps2mouse.c
  drivers/input/driver_input_ps2mouse.h
  chinese/chinese_font_data.c
)
STASH=$(mktemp -d)
for f in "${PRESERVE[@]}"; do
  if [ -f "$DST/$f" ]; then
    mkdir -p "$STASH/$(dirname "$f")"
    cp "$DST/$f" "$STASH/$f"
  fi
done

for d in kernel drivers fs boot l1 chinese; do
    rm -rf "$DST/$d"
done
rm -f "$DST/VENDORING.md"
mkdir -p "$DST"
for f in "${FILES[@]}"; do
    mkdir -p "$DST/$(dirname "$f")"
    git show "$SRC_TAG:$f" > "$DST/$f"
done

# restore the stashed OCSE-native additions (see PRESERVE above)
for f in "${PRESERVE[@]}"; do
  if [ -f "$STASH/$f" ]; then
    mkdir -p "$DST/$(dirname "$f")"
    cp "$STASH/$f" "$DST/$f"
  else
    echo "vendor_ocse: WARNING: $f missing (fresh bootstrap?) - see" >&2
    echo "  the WP06f regeneration instructions in tools/ocse_edits.py E20" >&2
  fi
done
rm -rf "$STASH"

cat > "$DST/VENDORING.md" <<'EOF'
# OCSE vendoring record (WP-06a)

OCSE (OpenCubeOS Setup Environment) is the installer pre-boot
environment: a small standalone kernel launched by OCBL as a multiboot2
ELF, providing hardware detection, a GUI + CLI installer front-end and
disk identification before OpenCubeOS itself is installed.

## Base

Vendored from tag **v1.0.0** (commit bdc47047) - the last pre-restructure
monolithic kernel. Rationale:

* OCSE runs before any OCOS service exists; the WP-03+ service tree
  (L0/L1 split, OCIRE embed, OTA, net stack) is installer dead weight.
* v1.0.0 already contains every driver an installer needs:
  framebuffer+font+console, USB HID (mouse/keyboard), i2c/SMBus
  (touchpad), ATA block layer + partition scan, PCI, ACPI incl. the
  AML interpreter (power ops) and HPET/TSC/CMOS time.
* A vendored copy (not a shared build) keeps OCSE frozen against
  future OCOS churn: the installer image must keep booting even as the
  main kernel evolves.

## Procedure

1. `tools/vendor_ocse.sh` - re-creates `ocse/` from the pristine
   v1.0.0 sources (143 files, this script is the file-list source of
   truth).  WP06f: OCSE-native additions inside the vendored subtrees
   (the PS/2 mouse driver, the regenerated Chinese font data) are
   stashed and restored automatically; on a fresh bootstrap a missing
   font data file must be regenerated with the FIXED
   `tools/gen_chinese_font.py` (see `tools/ocse_edits.py` E20).
2. `tools/ocse_edits.py` - applies every modification on top (E1-E20;
   never edit vendored files by hand: a re-vendor would
   erase them).
3. WP06f: `ocse/chinese/chinese_font_data.c` is GENERATED data from
   `tools/gen_chinese_font.py` (committed output, same precedent as
   `drivers/display/screen_font_data.c`).  The v1.0.0 tag carries the
   pre-WP06f glyph data (baseline-clipped strokes); never restore it.

## Excluded on purpose (v1.0.0 areas not vendored)

kernel/ota, kernel/crypto, net/*, shell/, tui/, userprogs/, libs/ld_so,
drivers/{bluetooth,camera,nic,scsi,sd,snd,wireless,misc},
drivers/usb/{serial,audio,msc} entries, kernel/core/{syscall,usermode,
g7test,g8test,userprogs_data}, boot/grub_*, l1/ (all but l1_ext and
l1_selftest), pinyin/IME data.

## Layout

`ocse/` mirrors the v1.0.0 tree layout (kernel/, drivers/, fs/, boot/,
l1/, chinese/). OCSE-specific new code lives at the ocse/ top level
(ocse_main.c, ocse_detect.c, ocse_i18n.c, ocse_disk.c, ocse_gui.c,
ocse_cl.c) plus linker_ocse.ld / ocse.mk.
EOF

echo "vendor_ocse: ${#FILES[@]} files -> $DST/ (plus VENDORING.md)"
