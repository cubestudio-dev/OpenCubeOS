#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - WP-14-docs_release
# File: tools/sync_wiki.sh
# Purpose: sync the documentation tree to the GitHub Wiki.
#
#   docs/ (single source of truth)
#     -> wiki pages (one mapped file per Wiki page, links rewritten
#        to [[Wiki-Links]], relative assets stripped)
#     -> pushed to cubestudio-dev/OpenCubeOS.wiki.git
#
# The mapping table below is the contract: every docs/ page listed
# becomes a Wiki page with the given name; every docs/ page NOT listed
# is reachable from the mapped pages' links only through the docs
# center (the Wiki is the curated view, the docs center is the full
# tree - same sources, both live).
#
# Usage: tools/sync_wiki.sh [--dry-run]
# Env:   GH_TOKEN with repo access (push rights to the wiki).

set -euo pipefail

OC_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WIKI_REPO="${WIKI_REPO:-https://x-access-token:${GH_TOKEN}@github.com/cubestudio-dev/OpenCubeOS.wiki.git}"
WORK="$(mktemp -d /tmp/oc-wiki.XXXXXX)"
DRY=0
[ "${1:-}" = "--dry-run" ] && DRY=1

# ---- mapping: Wiki page name -> docs/ source ------------------------
map_page() {
    case "$1" in
        Home)                   echo "docs/index.md" ;;
        Getting-Started)        echo "docs/manual/getting-started.md" ;;
        Boot-Modes)             echo "docs/manual/boot-modes.md" ;;
        Shell-Basics)           echo "docs/manual/shell-basics.md" ;;
        Filesystems)            echo "docs/manual/filesystems.md" ;;
        Networking)             echo "docs/manual/network.md" ;;
        Update-and-OTA)         echo "docs/manual/update-ota.md" ;;
        TUI-Guide)              echo "docs/manual/tui-guide.md" ;;
        Chinese-Console)        echo "docs/manual/chinese-console.md" ;;
        Recovery)               echo "docs/manual/recovery.md" ;;
        User-Manual)            echo "docs/manual/index.md" ;;
        Command-Reference)      echo "docs/manual/commands/index.md" ;;
        Building-from-Source)   echo "docs/root/BUILDING.md" ;;
        Architecture-Overview)  echo "docs/root/ARCHITECTURE.md" ;;
        Interfaces-Reference)   echo "docs/root/INTERFACES.md" ;;
        FAQ)                    echo "docs/root/FAQ.md" ;;
        Contributing)           echo "docs/root/CONTRIBUTING.md" ;;
        Developer-Environment)  echo "docs/dev/environment.md" ;;
        Build-System)           echo "docs/dev/build.md" ;;
        QEMU-Testing)           echo "docs/dev/qemu-testing.md" ;;
        Debugging)              echo "docs/dev/debugging.md" ;;
        Host-Tests)             echo "docs/dev/host-tests.md" ;;
        E2E-Suites)             echo "docs/dev/e2e-suites.md" ;;
        PR-Workflow)            echo "docs/dev/pr-workflow.md" ;;
        Portability)            echo "docs/dev/portability.md" ;;
        Coding-Style)           echo "docs/dev/coding-style.md" ;;
        Architecture-Boot-Flow) echo "docs/arch/boot-flow.md" ;;
        Architecture-Memory)    echo "docs/arch/memory.md" ;;
        Architecture-Scheduler) echo "docs/arch/scheduler.md" ;;
        Architecture-Interrupts) echo "docs/arch/interrupts.md" ;;
        Architecture-Ring3)     echo "docs/arch/syscall-ring3.md" ;;
        Architecture-VFS)       echo "docs/arch/vfs.md" ;;
        Architecture-Network)   echo "docs/arch/network-stack.md" ;;
        Architecture-Driver-Model) echo "docs/arch/driver-model.md" ;;
        Architecture-TUI)       echo "docs/arch/tui-engine.md" ;;
        Architecture-OTA)       echo "docs/arch/ota-ab.md" ;;
        L1-Overview)            echo "docs/l1/overview.md" ;;
        L1-Surface-Map)         echo "docs/l1/surface-map.md" ;;
        L1-Block-Drivers)       echo "docs/l1/driver-block.md" ;;
        L1-NIC-Drivers)         echo "docs/l1/driver-nic.md" ;;
        L1-Sound-Drivers)       echo "docs/l1/driver-snd.md" ;;
        L1-OTA)                 echo "docs/l1/ota.md" ;;
        L1-Watchdog-Health)     echo "docs/l1/watchdog-health.md" ;;
        L1-Shell-Bridge)        echo "docs/l1/shell-bridge.md" ;;
        L1-TUI-Extensions)      echo "docs/l1/tui-ext.md" ;;
        L1-Writing-Modules)     echo "docs/l1/writing-modules.md" ;;
        L1-Solib-Loader)        echo "docs/l1/solib-loader.md" ;;
        Drivers-Storage)        echo "docs/drivers/storage.md" ;;
        Drivers-Network)        echo "docs/drivers/network.md" ;;
        Drivers-USB)            echo "docs/drivers/usb.md" ;;
        Drivers-Sound)          echo "docs/drivers/sound.md" ;;
        Drivers-Input)          echo "docs/drivers/input.md" ;;
        Drivers-Display)        echo "docs/drivers/display.md" ;;
        Drivers-PCI)            echo "docs/drivers/pci-pcie.md" ;;
        Drivers-ACPI)           echo "docs/drivers/acpi.md" ;;
        Drivers-I2C)            echo "docs/drivers/i2c-smbus.md" ;;
        Drivers-Time)           echo "docs/drivers/time.md" ;;
        Drivers-Wireless)       echo "docs/drivers/wireless.md" ;;
        Drivers-Bluetooth)      echo "docs/drivers/bluetooth.md" ;;
        Drivers-SD-Camera)      echo "docs/drivers/sd-camera.md" ;;
        Drivers-Misc-SCSI)      echo "docs/drivers/misc-scsi.md" ;;
        Internals-Watchdog)     echo "docs/internals/watchdog.md" ;;
        Internals-Klog)         echo "docs/internals/klog.md" ;;
        Internals-Health)       echo "docs/internals/health.md" ;;
        Internals-Crash-Dump)   echo "docs/internals/crash-dump.md" ;;
        Internals-Crypto)       echo "docs/internals/drbg-crypto.md" ;;
        Internals-OTA-AB)       echo "docs/internals/ota-ab.md" ;;
        Troubleshooting-Common) echo "docs/troubleshooting/common-errors.md" ;;
        Troubleshooting-QEMU)   echo "docs/troubleshooting/qemu-debugging.md" ;;
        Troubleshooting-Boot)   echo "docs/troubleshooting/boot-failures.md" ;;
        Troubleshooting-Network) echo "docs/troubleshooting/network-issues.md" ;;
        Troubleshooting-Storage) echo "docs/troubleshooting/storage-issues.md" ;;
        Versioning)             echo "docs/release/versioning.md" ;;
        Release-Process)        echo "docs/release/process.md" ;;
        Release-Notes)          echo "GENERATED" ;;
        *)                      echo "" ;;
    esac
}

PAGES=(
    Home Getting-Started Boot-Modes Shell-Basics Filesystems Networking
    Update-and-OTA TUI-Guide Chinese-Console Recovery User-Manual
    Command-Reference Building-from-Source Architecture-Overview
    Interfaces-Reference FAQ Contributing Developer-Environment
    Build-System QEMU-Testing Debugging Host-Tests E2E-Suites
    PR-Workflow Portability Coding-Style Architecture-Boot-Flow
    Architecture-Memory Architecture-Scheduler Architecture-Interrupts
    Architecture-Ring3 Architecture-VFS Architecture-Network
    Architecture-Driver-Model Architecture-TUI Architecture-OTA
    L1-Overview L1-Surface-Map L1-Block-Drivers L1-NIC-Drivers
    L1-Sound-Drivers L1-OTA L1-Watchdog-Health L1-Shell-Bridge
    L1-TUI-Extensions L1-Writing-Modules L1-Solib-Loader Drivers-Storage
    Drivers-Network Drivers-USB Drivers-Sound Drivers-Input Drivers-Display
    Drivers-PCI Drivers-ACPI Drivers-I2C Drivers-Time Drivers-Wireless
    Drivers-Bluetooth Drivers-SD-Camera Drivers-Misc-SCSI
    Internals-Watchdog Internals-Klog Internals-Health
    Internals-Crash-Dump Internals-Crypto Internals-OTA-AB
    Troubleshooting-Common Troubleshooting-QEMU Troubleshooting-Boot
    Troubleshooting-Network Troubleshooting-Storage Versioning
    Release-Process Release-Notes
)

# ---- reverse map for link rewriting ---------------------------------
# docs path (relative, no docs/ prefix, .md stripped) -> wiki page name
wiki_target() {
    local rel="$1"
    for p in "${PAGES[@]}"; do
        [ "$p" = "Release-Notes" ] && continue
        local src
        src="$(map_page "$p")"
        [ -z "$src" ] && continue
        if [ "${src#docs/}" = "${rel%.md}" ] || [ "${src#docs/}" = "$rel" ]; then
            echo "$p"
            return
        fi
    done
    echo ""
}

rewrite_links() {
    # rewrite markdown links to [[Wiki-Page]] when the target maps to a
    # wiki page; drop links to non-mapped pages' files (keep the text).
    python3 - "$1" <<'PYEOF'
import os, re, sys
path = sys.argv[1]
text = open(path, encoding="utf-8", errors="replace").read()

WIKI_MAP = {}
import subprocess
# env-passed mapping list: "WikiName docs/rel.md" lines
for line in os.environ.get("OC_WIKI_MAP", "").splitlines():
    parts = line.split(None, 1)
    if len(parts) == 2:
        WIKI_MAP[parts[1].rstrip(".md").lstrip("docs/")] = parts[0]

def repl(m):
    target = m.group(2)
    t = target.split("#")[0]
    t = t.lstrip("./")
    if t in WIKI_MAP:
        anchor = target.split("#", 1)[1] if "#" in target else ""
        name = WIKI_MAP[t]
        if anchor:
            return "[%s](%s-%s)" % (m.group(1), name.replace("-", "-"),
                                    anchor)
        return "[[%s|%s]]" % (name, m.group(1))
    return m.group(1)   # keep text, drop dead wiki link

text = re.sub(r"\[([^\]]+)\]\(([^)]+\.md(?:#[^)]*)?)\)", repl, text)
open(path, "w").write(text)
PYEOF
}

# ---- build the wiki tree --------------------------------------------
echo "sync_wiki: ${#PAGES[@]} mapped pages"
for p in "${PAGES[@]}"; do
    src="$(map_page "$p")"
    [ -z "$src" ] && { echo "sync_wiki: no source for $p" >&2; exit 1; }
    if [ "$src" = "GENERATED" ]; then continue; fi
    if [ ! -f "$OC_ROOT/$src" ]; then
        echo "sync_wiki: missing source $src for $p" >&2
        exit 1
    fi
done

git clone -q "$WIKI_REPO" "$WORK/wiki" 2>/dev/null || {
    # empty wiki: create the checkout manually
    mkdir -p "$WORK/wiki"
    cd "$WORK/wiki"
    git init -q
    git remote add origin "$WIKI_REPO"
    git config user.name "cubestudio-dev"
    git config user.email "cubestudio@qq.com"
}

# export the reverse map for the python rewriter
MAPENV=""
for p in "${PAGES[@]}"; do
    src="$(map_page "$p")"
    [ -z "$src" ] && continue
    [ "$src" = "GENERATED" ] && continue
    MAPENV+="$p $src"$'\n'
done
export OC_WIKI_MAP="$MAPENV"

for p in "${PAGES[@]}"; do
    src="$(map_page "$p")"
    if [ "$src" = "GENERATED" ]; then
        # Release-Notes: generated from the update manifest history
        {
            echo "<!-- SPDX-License-Identifier: Apache-2.0 -->"
            echo "# Release notes"
            echo
            echo "Every release ships from the same reproducible pipeline;"
            echo "full notes live on the"
            echo "[releases page](https://github.com/cubestudio-dev/OpenCubeOS/releases)."
            echo
            python3 - "$OC_ROOT" <<'PYEOF'
import json, os, sys
root = sys.argv[1]
sd = os.path.join(root, "website", "assets", "site-data.js")
txt = open(sd).read()
# crude but honest parse of the small generated file
import re
ver = re.search(r'version: "([^"]+)"', txt)
build = re.search(r'build: (\d+)', txt)
date = re.search(r'date: "([^"]+)"', txt)
iso = re.search(r'name: "(opencube-[^"]+\.iso)"', txt)
sha = re.search(r'sha256: "([0-9a-f]{64})"', txt)
print("| Version | Date | Build |")
print("|---------|------|-------|")
if ver and date and build:
    print("| %s | %s | %s |" % (ver.group(1), date.group(1), build.group(1)))
hist = re.findall(r'\{ version: "([^"]+)", date: "([^"]+)"', txt)
for v, d in hist:
    print("| %s | %s | - |" % (v, d))
print()
print("Current ISO: `%s`" % (iso.group(1) if iso else "-"))
print("ISO SHA256: `%s`" % (sha.group(1) if sha else "-"))
PYEOF
        } > "$WORK/wiki/Release-Notes.md"
        continue
    fi
    cp "$OC_ROOT/$src" "$WORK/wiki/$p.md"
    # strip the generated-mirror header comment (3 lines) where present
    sed -i '1{/GENERATED mirror/{N;N;d}}' "$WORK/wiki/$p.md"
    rewrite_links "$WORK/wiki/$p.md"
done

cd "$WORK/wiki"
if [ "$DRY" = "1" ]; then
    echo "sync_wiki: DRY RUN - pages in $WORK/wiki:"
    ls -1 . | head -90
    exit 0
fi
git add -A
if git diff --cached --quiet; then
    echo "sync_wiki: wiki already up to date"
else
    git commit -q -m "docs sync: documentation tree -> wiki (tools/sync_wiki.sh)"
    git -c http.version=HTTP/1.1 push -q origin master 2>/dev/null \
        || git -c http.version=HTTP/1.1 push -q origin main
    echo "sync_wiki: pushed ${#PAGES[@]} pages"
fi
echo "sync_wiki: page list:"
ls -1 . | sed 's/^/    /' | head -90
rm -rf "$WORK"
