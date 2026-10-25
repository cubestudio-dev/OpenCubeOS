#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - v1.0.0 release tooling
# File: tools/build_release.sh
# Purpose: the one-command release builder - the SOP's steps 1-5
#          (docs/release/process.md) executed and gated:
#
#            1. clean build (make all)             - 0 warnings gate
#            2. reproducible-check                 - byte-identical gate
#            3. boot gates (BIOS + UEFI banners)   - via QEMU
#            4. deep gates (regression 28, E2E 14) - optional --deep
#            5. version the website data + docs data
#
#          It does NOT push anything: review the outputs, then run the
#          release chain (tag, Release upload, website, wiki, mirror)
#          manually or with your own tooling - deliberate asymmetry.
#
# Usage: tools/build_release.sh <version> [--deep]
#        (version example: 1.0.0)

set -euo pipefail

OC_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$OC_ROOT"

VER="${1:?usage: build_release.sh <version> [--deep]}"
DEEP=0
[ "${2:-}" = "--deep" ] && DEEP=1

echo "=============================================="
echo " build_release: $VER  (deep=$DEEP)"
echo "=============================================="

# ---- 0. pre-flight ---------------------------------------------------
git status --porcelain | head -5
if [ -n "$(git status --porcelain)" ]; then
    echo "build_release: WARNING - uncommitted changes listed above"
fi
if git rev-parse -q --verify "refs/tags/$VER" >/dev/null 2>&1; then
    echo "build_release: tag $VER already exists - refusing" >&2
    exit 1
fi
git config user.name | grep -q cubestudio-dev \
    || { echo "build_release: git identity is not cubestudio-dev" >&2; exit 1; }

# ---- 1. build --------------------------------------------------------
echo "== [1/5] clean build (make all) =="
make clean >/dev/null
make -j"$(nproc)" all
ls -l build/opencube.iso dist/*.zip dist/*-update.tar.gz 2>/dev/null || true

# ---- 2. reproducibility ----------------------------------------------
echo "== [2/5] reproducible-check =="
make reproducible-check

# ---- 3. boot gates ---------------------------------------------------
echo "== [3/5] boot gates =="
source "${OC_TOOLS:-/usr}/usr/share/qemu/../dev/null" 2>/dev/null || true
[ -n "${OC_TOOLS:-}" ] || OC_TOOLS=/usr
QEMU="$OC_TOOLS/usr/bin/qemu-system-x86_64"
[ -x "$QEMU" ] || QEMU="$(command -v qemu-system-x86_64)"
DATADIR="$OC_TOOLS/usr/share/qemu"
[ -d "$DATADIR" ] || DATADIR=/usr/share/qemu
export LD_LIBRARY_PATH="$OC_TOOLS/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

boot_banner() {
    local mode="$1"; shift
    timeout 90 "$QEMU" -m 512 -cdrom build/opencube.iso \
        -L "$DATADIR" -display none -serial file:/tmp/rel_boot.log "$@" \
        >/dev/null 2>&1 || true
    grep -aq "ready. Type 'help' for commands" /tmp/rel_boot.log \
        && grep -aq "shell command system (233 commands)" /tmp/rel_boot.log
}
boot_banner "bios" && echo "    BIOS boot gate: PASS" \
    || { echo "    BIOS boot gate: FAIL" >&2; exit 1; }
# UEFI via pflash (CODE + writable VARS copy): the plain -bios form
# silently fails to run the payload on current OVMF builds.
OVMF_CODE="${OVMF_FD:-$OC_TOOLS/usr/share/OVMF/OVMF_CODE_4M.fd}"
OVMF_VARS="${OVMF_CODE/OVMF_CODE_/OVMF_VARS_}"
[ -f "$OVMF_VARS" ] || OVMF_VARS=/usr/share/OVMF/OVMF_VARS_4M.fd
cp -f "$OVMF_VARS" /tmp/rel_ovmf_vars.fd
boot_banner "uefi" -machine q35 \
    -drive if=pflash,format=raw,readonly=on,file="$OVMF_CODE" \
    -drive if=pflash,format=raw,file=/tmp/rel_ovmf_vars.fd \
    && echo "    UEFI boot gate: PASS" \
    || { echo "    UEFI boot gate: FAIL" >&2; exit 1; }

# ---- 4. deep gates (opt-in; release runs MUST use --deep) ------------
if [ "$DEEP" = "1" ]; then
    echo "== [4/5] deep gates =="
    python3 tools/run_fix4_regression.py build/opencube.iso \
        evidence/release_regression.log | tail -2
    python3 tools/wp12_watchdog_logging_e2e.py build/opencube.iso | tail -2
else
    echo "== [4/5] deep gates SKIPPED (run with --deep for a release) =="
fi

# ---- 5. website + docs data -------------------------------------------
echo "== [5/5] release data =="
BUILD_NO=$(git rev-list --count HEAD)
DATE=$(date -u +%F)
python3 tools/gen_api_docs.py --help-capture evidence/help_a_runtime.txt --pages-zh \
    --pages --md
tools/update_website.sh "$VER" "$BUILD_NO" "$DATE"
website/build.sh

echo "=============================================="
echo " build_release: ALL GATES PASSED"
echo "   version : $VER"
echo "   build   : $BUILD_NO"
echo "   date    : $DATE"
echo "   artifacts:"
sha256sum build/opencube.iso dist/OpenCubeOS-*.zip \
    dist/OpenCubeOS-*-update.tar.gz
echo " Next: tag $VER, push, create the Release, deploy out/, sync wiki."
echo "=============================================="
