#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
#
# Open Cube OS NAK - tools/install-toolchain.sh
#
# Installs the NAK build/test toolchain into a user prefix WITHOUT root:
#   nasm, xorriso, mtools, grub-pc-bin + grub-common (grub-mkrescue),
#   ovmf and qemu-system-x86 (qemu-system-x86_64).
# gcc / ld / make are expected from the base image (NAK targets gcc 14).
#
# Method: apt-get download + dpkg -x into a user-local prefix - no root,
# no system mutation, no Python. The apt metadata lives inside the prefix
# so the script is self-contained and re-runnable.
#
# Mirror policy: plain deb.debian.org is unreachable or crawling from CN
# networks, so mirrors are tried in order TUNA -> USTC -> official CDN.
#
# Usage:
#   tools/install-toolchain.sh [PREFIX]     # default: $HOME/opt/extract
#
# Afterwards source $PREFIX/env.sh (printed at the end too) to extend
# PATH / LD_LIBRARY_PATH with the user-local toolchain.

set -u

PREFIX="${1:-$HOME/opt/extract}"

# --- sanity: Debian-based system with a known suite ----------------------
if [ ! -r /etc/os-release ]; then
    echo "install-toolchain: /etc/os-release missing - not a Debian base?" >&2
    exit 1
fi
. /etc/os-release
SUITE="${VERSION_CODENAME:-}"
if [ -z "$SUITE" ]; then
    echo "install-toolchain: cannot determine Debian suite" >&2
    exit 1
fi

# --- figure out what is actually missing ----------------------------------
NEED=()
command -v nasm               >/dev/null 2>&1 || NEED+=(nasm)
command -v xorriso            >/dev/null 2>&1 || NEED+=(xorriso)
command -v mformat            >/dev/null 2>&1 || NEED+=(mtools)
command -v grub-mkrescue      >/dev/null 2>&1 || NEED+=(grub-pc-bin grub-common)
command -v qemu-system-x86_64 >/dev/null 2>&1 || NEED+=(qemu-system-x86 ovmf)
# WP-02-ocbl: the UEFI side is built as a real PE32+ image, which needs
# the mingw-w64 cross toolchain (x86_64-w64-mingw32-gcc produces native
# EFI executables with -Wl,--subsystem,10).
command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1 || NEED+=(gcc-mingw-w64-x86-64)

if [ "${#NEED[@]}" -eq 0 ]; then
    echo "install-toolchain: nothing to do, all tools already on PATH"
    exit 0
fi
echo "install-toolchain: packages to fetch: ${NEED[*]}"

# --- prefix layout ---------------------------------------------------------
mkdir -p "$PREFIX/debs" \
         "$PREFIX/apt-state/lists/partial" \
         "$PREFIX/apt-cache/archives/partial"

write_sources() {
    cat > "$PREFIX/mirror.sources" <<EOF
Types: deb
URIs: $1/debian
Suites: $SUITE $SUITE-updates
Components: main
Signed-By: /usr/share/keyrings/debian-archive-keyring.pgp

Types: deb
URIs: $1/debian-security
Suites: $SUITE-security
Components: main
Signed-By: /usr/share/keyrings/debian-archive-keyring.pgp
EOF
}

APTOPTS=(
    -o Dir::Etc::sourcelist="$PREFIX/mirror.sources"
    -o Dir::Etc::sourceparts=-
    -o Dir::State::lists="$PREFIX/apt-state/lists"
    -o Dir::State::status=/var/lib/dpkg/status
    -o Dir::Cache="$PREFIX/apt-cache"
    -o Acquire::Languages=none
    -o Acquire::Retries=3
)

# --- pick the first mirror that answers ------------------------------------
MIRROR=""
for m in https://mirrors.tuna.tsinghua.edu.cn \
         https://mirrors.ustc.edu.cn \
         http://deb.debian.org; do
    echo "install-toolchain: trying mirror $m"
    write_sources "$m"
    if apt-get "${APTOPTS[@]}" update >"$PREFIX/apt-update.log" 2>&1; then
        MIRROR="$m"
        break
    fi
    echo "install-toolchain: mirror $m failed (see $PREFIX/apt-update.log)"
done
if [ -z "$MIRROR" ]; then
    echo "install-toolchain: no usable mirror, giving up" >&2
    exit 1
fi
echo "install-toolchain: using mirror $MIRROR"

# --- resolve the dependency closure, minus what is installed ---------------
CLOSURE="$(apt-cache "${APTOPTS[@]}" depends --recurse \
                    --no-recommends --no-suggests \
                    --no-conflicts --no-breaks --no-replaces --no-enhances \
                    "${NEED[@]}" 2>/dev/null \
           | grep -E '^[a-zA-Z0-9]' | sort -u)"

MISSING=()
for p in $CLOSURE; do
    if dpkg-query -W -f='${Status}' "$p" 2>/dev/null \
         | grep -q 'ok installed'; then
        continue
    fi
    MISSING+=("$p")
done
echo "install-toolchain: closure = $(echo "$CLOSURE" | wc -l) packages," \
     "not installed = ${#MISSING[@]}"

# --- download (each package gets its own retries; the link can be flaky) ---
cd "$PREFIX/debs" || exit 1
FAILED=()
for p in "${MISSING[@]}"; do
    ok=0
    for try in 1 2 3; do
        if apt-get "${APTOPTS[@]}" download -q "$p" >/dev/null 2>&1; then
            ok=1
            break
        fi
    done
    if [ "$ok" -ne 1 ]; then
        FAILED+=("$p")
    fi
done
if [ "${#FAILED[@]}" -gt 0 ]; then
    echo "install-toolchain: failed to download: ${FAILED[*]}" >&2
    echo "(virtual package names land here; check whether their providers" \
         "are already installed)" >&2
fi

# --- extract everything into the prefix -------------------------------------
extracted=0
for f in "$PREFIX"/debs/*.deb; do
    [ -e "$f" ] || continue
    if dpkg -x "$f" "$PREFIX"; then
        extracted=$((extracted + 1))
    else
        echo "install-toolchain: dpkg -x failed for $f" >&2
    fi
done
echo "install-toolchain: extracted $extracted deb archives into $PREFIX"

# --- environment file ---------------------------------------------------------
LIBDIR64="$PREFIX/usr/lib/x86_64-linux-gnu"
cat > "$PREFIX/env.sh" <<EOF
# Source this file to use the user-local NAK toolchain.
export PATH="$PREFIX/usr/bin:\$PATH"
export LD_LIBRARY_PATH="$LIBDIR64:$PREFIX/usr/lib\${LD_LIBRARY_PATH:+:\$LD_LIBRARY_PATH}"
# user-local prefixes consumed by ocos/Makefile
export GRUB_PREFIX="$PREFIX"
export QEMU_PREFIX="$PREFIX"
EOF

echo
echo "install-toolchain: done. Run:"
echo "  source $PREFIX/env.sh"
