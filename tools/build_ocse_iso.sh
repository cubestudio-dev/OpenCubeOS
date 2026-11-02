#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - tools/build_ocse_iso.sh
# Purpose: build the OCSE install-medium ISO (3.0.0-WP06a). The DUAL-
#          FIRMWARE OCBL boots this ISO under SeaBIOS AND OVMF; the
#          OCBL scanner classifies the volume via the /ocse.mrk marker
#          (the OCSE class outranks a co-resident kernel) and hands
#          /boot/ocse.elf to the OCOS-style mb2 ELF handoff. The G13
#          determinism seals travel with the OCBL packer.
#
# Output: build/ocse.iso
set -e

OC_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$OC_ROOT/build"
STAGE="$BUILD/ocse-iso-stage"
MARKER="$STAGE/ocse.mrk"

if [ ! -f "$BUILD/ocse/ocse.elf" ]; then
    echo "[ocse_iso] FATAL: $BUILD/ocse/ocse.elf missing (make ocse-elf)" >&2
    exit 1
fi

# WP06b b4 lesson: no .bss variable may reach the boot-structure zone
# (page tables + boot stack live at the top of .bss; ~140 B headroom).
python3 "$OC_ROOT/tools/check_bss.py" "$BUILD/ocse/ocse.elf" || exit 1
if [ ! -f "$BUILD/ocbl/ocbl_bios.bin" ] || \
   [ ! -f "$BUILD/ocbl/ocbl_x64.efi" ]; then
    echo "[ocse_iso] FATAL: OCBL images missing (make ocbl-bios ocbl-efi)" >&2
    exit 1
fi
# WP-06b: the MBR-flavor stage1 + core are what ocbl-install writes
# to disks (the El Torito pair above is only for booting the ISO).
if [ ! -f "$BUILD/ocbl/stage1_mbr.bin" ] || \
   [ ! -f "$BUILD/ocbl/core.bin" ]; then
    echo "[ocse_iso] FATAL: OCBL MBR flavor missing (make ocbl-bios)" >&2
    exit 1
fi
if [ ! -f "$BUILD/opencube.elf" ]; then
    echo "[ocse_iso] FATAL: $BUILD/opencube.elf missing (make kernel)" >&2
    exit 1
fi

echo "[ocse_iso] staging in $STAGE"
rm -rf "$STAGE"
mkdir -p "$STAGE/boot"

# --- 1. the install-environment kernel payload ---
cp "$BUILD/ocse/ocse.elf" "$STAGE/boot/ocse.elf"

# --- 2. the OCSE class marker (8.3-safe name; judge_volume reads it
#        FIRST, so an install medium carrying this marker never boots
#        as OCOS even if it also ships /boot/opencube.elf) ---
cat > "$MARKER" <<EOF
OpenCubeOS Setup Environment (OCSE) install medium
class: OCSE
release: $(git -C "$OC_ROOT" describe --tags --always 2>/dev/null || echo dev)
work-package: 3.0.0-WP06a
EOF

# --- 3. the OCBL dual-firmware pair ---
cp "$BUILD/ocbl/ocbl_bios.bin" "$STAGE/OCBL.BIN"
cp "$BUILD/ocbl/ocbl_x64.efi" "$STAGE/BOOTX64.EFI"

# --- 3b. (WP-06b) the disk-flavor OCBL image ocbl-install writes:
#         [ stage1_mbr 512B ][ core ] - ocbl-install patches the
#         +0x1A0 boot block (core LBA/sectors) and preserves the
#         target partition table (bytes 446..509) ---
python3 "$OC_ROOT/ocbl/build_image.py" "$STAGE/OCBLMBR.BIN" \
        "$BUILD/ocbl/stage1_mbr.bin" "$BUILD/ocbl/core.bin"

# --- 3c. (WP-06b) the OCOS system image `tar -xf` unpacks onto the
#         target partition (boot/opencube.elf + etc/opencube.conf) ---
python3 "$OC_ROOT/tools/make_ocos_tar.py" "$STAGE/OCOS.TAR" \
        "$BUILD/opencube.elf"

# --- 4. pack: BIOS El Torito + UEFI El Torito (1 MiB ESP), G13 seals ---
python3 "$OC_ROOT/ocbl/build_test_iso.py" "$BUILD/ocse.iso" \
        "$STAGE" OCBL.BIN BOOTX64.EFI

echo "[ocse_iso] ISO: $BUILD/ocse.iso"
ls -la "$BUILD/ocse.iso"
