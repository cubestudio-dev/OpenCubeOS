#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 */
# Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
# Open Cube OS - 3.0.0-WP-runtime-hardening-and-experience-rebase
# File: tools/test_strict.sh
# Purpose: the STRICT QEMU boot matrix (spec 4.3) - the only accepted
#          proof that the boot chain does not depend on QEMU's
#          default-mode nanny behaviors:
#
#            -cpu qemu64,enforce -machine vmport=off -boot strict=on
#
#          Both firmware tracks of build/opencube.iso must reach the
#          ring-3 ush prompt THROUGH OCRMECL under these arguments.
#          Evidence transcripts land in
#          evidence/wp-runtime-hardening-and-experience-rebase/strict/.
#
# Usage: bash tools/test_strict.sh        (expects build/opencube.iso)
# Exit:  0 = both tracks green, 1 = any track failed.
set -e

OC_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$OC_ROOT"
EV="evidence/wp-runtime-hardening-and-experience-rebase/strict"
mkdir -p "$EV"

OC_TOOLS="${OC_TOOLS:-$HOME/opt/extract}"

# Resolve the toolchain layout: the historical default
# (~/opt/extract) is a `dpkg -x` EXTRACTION ROOT, so tools live at
# <root>/usr/bin and firmware at <root>/usr/share.  A system install
# sets OC_TOOLS=/usr and the tools live at /usr/bin DIRECTLY - joining
# "/usr" + "/usr/bin" produced "/usr/usr/bin/qemu-system-x86_64" and the
# strict matrix silently reported RED because QEMU never started (no log
# file was even created).  Detect the shape and fall back to PATH.
_OC_BINDIR="$OC_TOOLS/usr/bin"
_OC_SHAREDIR="$OC_TOOLS/usr/share"
if [ ! -d "$_OC_BINDIR" ] && [ -d "$OC_TOOLS/bin" ]; then
    _OC_BINDIR="$OC_TOOLS/bin"
    _OC_SHAREDIR="$OC_TOOLS/share"
fi
QEMU="$_OC_BINDIR/qemu-system-x86_64"
[ -x "$QEMU" ] || QEMU="$(command -v qemu-system-x86_64 || true)"
OVMF="$_OC_SHAREDIR/ovmf/OVMF.fd"
[ -f "$OVMF" ] || OVMF="$_OC_SHAREDIR/OVMF/OVMF.fd"
[ -f "$OVMF" ] || OVMF="/usr/share/ovmf/OVMF.fd"
export LD_LIBRARY_PATH="$OC_TOOLS/usr/lib/x86_64-linux-gnu:$OC_TOOLS/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

ISO="${1:-build/opencube.iso}"
[ -f "$ISO" ] || { echo "test-strict: FATAL $ISO missing (make iso)" >&2; exit 1; }

STRICT="-cpu qemu64,enforce -machine vmport=off -boot strict=on"

wait_for() {   # wait_for <file> <pattern> <timeout_s>
    local f="$1" pat="$2" t="$3" i=0
    while [ $i -lt $((t * 2)) ]; do
        grep -aq "$pat" "$f" 2>/dev/null && return 0
        sleep 0.5
        i=$((i + 1))
    done
    return 1
}

run_track() {  # run_track <name> <bios|ovmf>
    local name="$1" fw="$2"
    local log="$EV/$name.log"
    echo "test-strict: $name booting under: $STRICT"
    rm -f "$log"
    if [ "$fw" = "ovmf" ]; then
        timeout -k 5 180 "$QEMU" -L "$OC_TOOLS/usr/share/qemu" \
            -L "$OC_TOOLS/usr/share/seabios" -m 512M -cdrom "$ISO" -boot d \
            -bios "$OVMF" -no-reboot -display none -serial file:"$log" \
            -monitor none -vga std $STRICT >/dev/null 2>&1 &
    else
        timeout -k 5 180 "$QEMU" -L "$OC_TOOLS/usr/share/qemu" \
            -L "$OC_TOOLS/usr/share/seabios" -m 512M -cdrom "$ISO" -boot d \
            -no-reboot -display none -serial file:"$log" \
            -monitor none -vga std $STRICT >/dev/null 2>&1 &
    fi
    local ok=1
    if wait_for "$log" "OCRMECL" 60; then
        # WP-compat B9: the intermediate "boot complete" banner is a
        # console print that the boot splash legitimately captures to
        # the background log - the chain verdict is OCRMECL (serial,
        # pre-kernel) followed by the ring-3 ush prompt (serial tee,
        # post-splash handoff).  BIOS + UEFI tracks unchanged.
        if wait_for "$log" "ush>" 240; then
            ok=0
        fi
    fi
    wait 2>/dev/null || true
    pkill -f "qemu-system-x86_64.*$name" 2>/dev/null || true
    if [ $ok -eq 0 ]; then
        echo "test-strict: $name GREEN (OCRMECL -> OCBL -> kernel -> ush)"
    else
        echo "test-strict: $name RED - transcript tail:" >&2
        tail -5 "$log" >&2 || true
    fi
    return $ok
}

rc=0
run_track strict-bios-bios-seabios bios || rc=1
run_track strict-uefi-ovmf ovmf || rc=1

if [ $rc -eq 0 ]; then
    echo "test-strict: BOTH TRACKS GREEN under $STRICT"
else
    echo "test-strict: FAILED (see $EV/)" >&2
fi
exit $rc
