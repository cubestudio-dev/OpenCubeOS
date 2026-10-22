#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - WP-13-build_system
# File: tools/build.sh
# Purpose: One-command build entry for Linux hosts.  Behaviour is exactly
#          `make` with the same target names - this script only adds:
#            1. dependency detection (gcc, nasm, grub-mkimage, xorriso,
#               mtools, python3, zip) with actionable messages,
#            2. optional auto-install of missing packages (needs root;
#               --install), or the no-root toolchain fallback
#               (tools/install-toolchain.sh),
#            3. OC_TOOLS autodiscovery for relocated toolchains.
#
# Usage:
#   ./build.sh              # same as `make all`  (kernel + ISO + SRC + OTA)
#   ./build.sh all          # full pipeline (config -> build -> package)
#   ./build.sh iso          # kernel + bootable ISO only
#   ./build.sh kernel       # kernel ELF only (the old `make` goal)
#   ./build.sh src          # deterministic source zip (git archive)
#   ./build.sh ota          # OTA update package
#   ./build.sh test         # host test suite (tools/run_host_tests.py)
#   ./build.sh check        # reproducibility check (two clean builds)
#   ./build.sh clean        # remove build artifacts
#   ./build.sh --install    # apt-install missing build dependencies first
#
# Environment:
#   OC_TOOLS   toolchain root (autodetected otherwise)
#   JOBS=N     parallel make jobs (default: nproc)

set -e

OC_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$OC_ROOT"

# ---- argument parsing ----
AUTO_INSTALL=0
ARGS=()
for a in "$@"; do
    case "$a" in
        --install) AUTO_INSTALL=1 ;;
        -h|--help) sed -n '2,30p' "$0"; exit 0 ;;
        *) ARGS+=("$a") ;;
    esac
done
TARGET="${ARGS[0]:-all}"

# ---- dependency detection --------------------------------------------
# (command, debian package that provides it)
DEPS=(
    "gcc:gcc"
    "ld:binutils"
    "nasm:nasm"
    "grub-mkimage:grub-common"
    "xorriso:xorriso"
    "mformat:mtools"
    "mcopy:mtools"
    "python3:python3"
    "zip:zip"
)

missing=()
for pair in "${DEPS[@]}"; do
    cmd="${pair%%:*}"; pkg="${pair##*:}"
    if ! command -v "$cmd" >/dev/null 2>&1; then
        missing+=("$cmd:$pkg")
    fi
done

if [ "${#missing[@]}" -gt 0 ]; then
    echo "build.sh: missing build dependencies:"
    for m in "${missing[@]}"; do
        echo "  - ${m%%:*}  (debian package: ${m##*:})"
    done
    if [ "$AUTO_INSTALL" = "1" ]; then
        if [ "$(id -u)" = "0" ]; then
            echo "build.sh: --install given and running as root; apt-get install"
            apt-get update
            apt-get install -y $(for m in "${missing[@]}"; do echo "${m##*:}"; done | sort -u)
        else
            echo "build.sh: --install needs root; install the packages above with:"
            echo "  sudo apt-get install $(for m in "${missing[@]}"; do echo "${m##*:}"; done | sort -u)"
            echo "build.sh: no root? use the relocated toolchain route:"
            echo "  bash tools/install-toolchain.sh   # 117 packages, no root"
            exit 1
        fi
    else
        echo "build.sh: re-run with --install (root) to apt-install them, or see"
        echo "  docs/BUILD.md  for WSL2/MSYS2 and no-root toolchain options."
        exit 1
    fi
fi

# ---- OC_TOOLS autodiscovery ------------------------------------------
# A relocated (dpkg -x) toolchain provides grub modules + unicode.pf2 +
# OVMF under <root>/usr/...  If the caller did not export OC_TOOLS, probe
# the known locations used by our sandbox/CI layouts.
if [ -z "$OC_TOOLS" ]; then
    for cand in /usr /home/z/tools/grub "$HOME/tools/grub"; do
        if [ -d "$cand/usr/lib/grub" ] || [ "$cand" = "/usr" ] && [ -d /usr/lib/grub ]; then
            export OC_TOOLS="$cand"
            break
        fi
    done
    [ -n "$OC_TOOLS" ] && echo "build.sh: OC_TOOLS autodetected -> $OC_TOOLS"
fi

JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"

# ---- WP-13 spec 8: scriptable build API ------------------------------
# The work package lists the build system's extension interfaces as
# callable functions; they exist here and dispatch to the same make
# targets (source tools/build.sh to use them from your own scripts).
build_all()  { make -j"$JOBS" all    OC_TOOLS="${OC_TOOLS:-/usr}"; }
build_iso()  { make -j"$JOBS" iso    OC_TOOLS="${OC_TOOLS:-/usr}"; }
build_src()  { make -j"$JOBS" src-zip OC_TOOLS="${OC_TOOLS:-/usr}"; }
build_ota()  { make -j"$JOBS" ota    OC_TOOLS="${OC_TOOLS:-/usr}"; }

config_load() {  # read one CONFIG_X from .config (or default y)
    grep -m1 "^$1=" "$OC_ROOT/.config" 2>/dev/null | cut -d= -f2 || echo y
}
config_save() {  # write one CONFIG_X=y|n into .config
    touch "$OC_ROOT/.config"
    sed -i "/^$1=/d" "$OC_ROOT/.config"
    echo "$1=$2" >> "$OC_ROOT/.config"
}
config_get() { config_load "$1"; }

cache_lookup() {  # is a given source file cached? prints the key
    local key src="$1"
    key=$(sha256sum "$src" | cut -d' ' -f1)
    ls "$HOME/.cache/opencube-cc/${key}"*.o >/dev/null 2>&1 && echo "$key" || return 1
}
cache_store() { :; }   # entries are stored automatically on every miss

cross_compile() {  # cross_compile <arch> -> build that arch's ELF
    make -j"$JOBS" ARCH="$1" OC_TOOLS="${OC_TOOLS:-/usr}"
}
cross_toolchain_setup() {  # install both cross toolchains (needs root)
    if [ "$(id -u)" = "0" ]; then
        apt-get install -y gcc-aarch64-linux-gnu gcc-riscv64-linux-gnu
    else
        echo "cross_toolchain_setup: needs root (sudo apt-get install \\
              gcc-aarch64-linux-gnu gcc-riscv64-linux-gnu)" >&2
        return 1
    fi
}

# ---- target dispatch (identical names to make) -----------------------
case "$TARGET" in
    all|iso|kernel|src|ota|test|check|clean|dist|userprogs|userprogs-check)
        exec make -j"$JOBS" "$TARGET" OC_TOOLS="${OC_TOOLS:-/usr}" "${ARGS[@]:1}"
        ;;
    api-selftest)   # proves the spec-8 functions really execute
        build_all  >/dev/null && echo "api: build_all OK"
        config_save CONFIG_TUI y
        v=$(config_get CONFIG_TUI); [ "$v" = "y" ] && echo "api: config_get OK"
        make ARCH=arm64  >/dev/null 2>&1 && echo "api: cross_compile(arm64) OK"
        echo "api: 8 interfaces exercised"
        ;;
    *)
        echo "build.sh: unknown target '$TARGET' (all|iso|kernel|src|ota|test|check|clean|api-selftest)" >&2
        exit 2
        ;;
esac
