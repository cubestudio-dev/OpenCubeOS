#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - WP-01..WP-04 build_iso.sh (GRUB release chain)
# Open Cube OS - 3.0.0-WP05 B5-7: the GRUB chain is REMOVED.  This is
# now the OCBL release ISO: the dual-firmware OCBL boot loader (BIOS
# stage1+core image + UEFI PE application) replaces grub-mkimage and
# the grub.cfg / grub-ab.cfg menus.  OCOS and OCIRE are booted by the
# OCBL six-class scanner - same ISO boots under SeaBIOS AND OVMF.
#
# The G13 determinism stack is unchanged (delegated to the OCBL
# packer): epoch-pinned timestamps everywhere, fixed GPT disk GUID,
# fixed ESP volume serial - two clean builds are byte-identical.
#
# Output: build/opencube.iso
set -e

OC_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$OC_ROOT/build"
ISO_STAGE="$BUILD/iso-stage"

# prerequisites: the kernel ELF and BOTH OCBL flavors
if [ ! -f "$BUILD/opencube.elf" ]; then
    echo "[build_iso] FATAL: $BUILD/opencube.elf missing (make kernel)" >&2
    exit 1
fi
if [ ! -f "$BUILD/ocbl/ocbl_bios.bin" ] || \
   [ ! -f "$BUILD/ocbl/ocbl_x64.efi" ]; then
    echo "[build_iso] FATAL: OCBL images missing (make ocbl-bios ocbl-efi)" >&2
    exit 1
fi

echo "[build_iso] staging in $ISO_STAGE"
rm -rf "$ISO_STAGE"
mkdir -p "$ISO_STAGE/boot"

# --- 1. the kernel ELF (OCOS: the ISO registers as an OCOS entry
#        through the OCBL scanner's ISO9660 path) ---
cp "$BUILD/opencube.elf" "$ISO_STAGE/boot/opencube.elf"

# --- 2. the OCBL dual-firmware pair ---
cp "$BUILD/ocbl/ocbl_bios.bin" "$ISO_STAGE/OCBL.BIN"
cp "$BUILD/ocbl/ocbl_x64.efi" "$ISO_STAGE/BOOTX64.EFI"

# --- 3. assemble: BIOS El Torito (stage1+core) + UEFI El Torito
#        (1 MiB ESP FAT with /EFI/BOOT/BOOTX64.EFI), G13 seals and all,
#        through the same OCBL packer the B-batches drove live ---
python3 "$OC_ROOT/ocbl/build_test_iso.py" "$BUILD/opencube.iso" \
        "$ISO_STAGE" OCBL.BIN BOOTX64.EFI

echo "[build_iso] ISO: $BUILD/opencube.iso"
ls -la "$BUILD/opencube.iso"
