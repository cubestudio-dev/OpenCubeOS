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
if [ ! -f "$BUILD/ocbl/ocbl_bios.bin" ] || \
   [ ! -f "$BUILD/ocbl/ocbl_x64.efi" ]; then
    echo "[ocse_iso] FATAL: OCBL images missing (make ocbl-bios ocbl-efi)" >&2
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

# --- 4. pack: BIOS El Torito + UEFI El Torito (1 MiB ESP), G13 seals ---
python3 "$OC_ROOT/ocbl/build_test_iso.py" "$BUILD/ocse.iso" \
        "$STAGE" OCBL.BIN BOOTX64.EFI

echo "[ocse_iso] ISO: $BUILD/ocse.iso"
ls -la "$BUILD/ocse.iso"
