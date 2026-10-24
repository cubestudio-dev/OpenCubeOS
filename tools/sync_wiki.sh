#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - WP-14 localization
# File: tools/sync_wiki.sh
# Purpose: sync the documentation tree to the GitHub Wiki (bilingual).
#
#   docs/        (English, single source of truth)
#   docs/zh/     (Simplified Chinese translation)
#     -> wiki pages: Chinese is canonical under the mapped page name,
#        English ships as <Page>-EN.md. Every page carries a language
#        banner linking to its counterpart. Links are rewritten to
#        [[Wiki-Links]] per language (zh -> canonical names,
#        en -> -EN suffixed names) and relative assets are stripped.
#     -> pushed to cubestudio-dev/OpenCubeOS.wiki.git
#
# The mapping table below is the contract: every docs/ page listed
# becomes a Wiki page pair with the given name; every docs/ page NOT
# listed is reachable from the mapped pages' links only through the
# docs center (the Wiki is the curated view, the docs center is the
# full tree - same sources, both live, both languages).
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
        Developer-Handbook)      echo "docs/dev/index.md" ;;
        Architecture-Guide)      echo "docs/arch/index.md" ;;
        Drivers-Index)           echo "docs/drivers/index.md" ;;
        Troubleshooting-Index)   echo "docs/troubleshooting/index.md" ;;
        Internals-Index)         echo "docs/internals/index.md" ;;
        Release-Index)           echo "docs/release/index.md" ;;
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
    Developer-Handbook Architecture-Guide Drivers-Index
    Troubleshooting-Index Internals-Index Release-Index
)

# ---- link rewriting -------------------------------------------------
# Rewrite markdown links to [[Wiki-Page]] when the target maps to a wiki
# page; drop links to non-mapped pages' files (keep the text); rewrite
# DIRECTORY links (manual/, dev/, ... - they exist in the docs tree but
# not on the wiki) to the section's index page. Anchors are dropped
# (wiki headings are localized on the zh pages, so en anchors would
# dead-end). OC_WIKI_SUFFIX appends "-EN" for the English variants.
rewrite_links() {
    python3 - "$1" <<'PYEOF'
import os, re, sys
path = sys.argv[1]
text = open(path, encoding="utf-8", errors="replace").read()

SUFFIX = os.environ.get("OC_WIKI_SUFFIX", "")

WIKI_MAP = {}
# env-passed mapping list: "WikiName docs/rel.md" lines
for line in os.environ.get("OC_WIKI_MAP", "").splitlines():
    parts = line.split(None, 1)
    if len(parts) == 2:
        key = parts[1].strip()
        if key.endswith(".md"):
            key = key[:-3]
        if key.startswith("docs/"):
            key = key[len("docs/"):]
        WIKI_MAP[key] = parts[0] + SUFFIX

# directory the source page lives in (docs-relative, e.g. "manual");
# relative links in the page resolve against it before the map lookup.
SRCDIR = os.environ.get("OC_WIKI_SRCDIR", "").strip().strip("/")

# directory links (docs/<dir>/) -> the section index's wiki page
DIR_MAP = {
    "manual": "User-Manual",            # docs/manual/index.md
    "dev": "Developer-Handbook",        # docs/dev/index.md
    "arch": "Architecture-Guide",       # docs/arch/index.md
    "l1": "L1-Overview",                # docs/l1/overview.md
    "drivers": "Drivers-Index",         # docs/drivers/index.md
    "troubleshooting": "Troubleshooting-Index",
    "internals": "Internals-Index",
    "release": "Release-Index",
}
DIR_MAP = {k: v + SUFFIX for k, v in DIR_MAP.items()}

def resolve(target):
    """Resolve a page-relative link target to a docs-root-relative path
    (no .md suffix): same-dir links stay relative to SRCDIR, ../ climbs,
    and plain 'x.md' in a root-level page resolves to 'x'."""
    t = target.split("#")[0].strip()
    if not t:
        return ""
    joined = os.path.normpath(os.path.join(SRCDIR, t)) if SRCDIR else os.path.normpath(t)
    joined = joined.replace("\\", "/")
    if joined.endswith(".md"):
        joined = joined[:-3]
    return joined.lstrip("./")

def repl(m):
    t = resolve(m.group(2))
    if t and t in WIKI_MAP:
        # anchors dropped: wiki heading ids differ per language
        return "[[%s|%s]]" % (WIKI_MAP[t], m.group(1))
    return m.group(1)   # keep text, drop dead wiki link

def repl_dir(m):
    d = m.group(2).rstrip("/").lstrip("./")
    if d in DIR_MAP:
        return "[[%s|%s]]" % (DIR_MAP[d], m.group(1))
    # a relative directory link (e.g. 'commands/' inside manual/) maps to
    # that directory's index page when one exists in the wiki
    rd = resolve(m.group(2))
    if rd and (rd + "/index") in WIKI_MAP:
        return "[[%s|%s]]" % (WIKI_MAP[rd + "/index"], m.group(1))
    return m.group(1)   # keep text, drop dead wiki link

text = re.sub(r"\[([^\]]+)\]\(([^)]+\.md(?:#[^)]*)?)\)", repl, text)
text = re.sub(r"\[([^\]]+)\]\((\.{0,2}/?[a-z][a-z0-9]*/)\)", repl_dir, text)
open(path, "w", encoding="utf-8").write(text)
PYEOF
}

# insert a language banner after the leading comment block
insert_banner() {
    python3 - "$1" "$2" <<'PYEOF'
import sys
path, page = sys.argv[1], sys.argv[2]
zh = page.endswith("-EN.md") is False
lines = open(path, encoding="utf-8").read().split("\n")
i = 0
while i < len(lines) and (lines[i].startswith("<!--") or lines[i].strip() == ""):
    # multi-line comments: skip to the closing -->
    if lines[i].startswith("<!--") and "-->" not in lines[i]:
        while i < len(lines) and "-->" not in lines[i]:
            i += 1
    i += 1
if zh:
    other = page[:-3] + "-EN" if page.endswith(".md") else page + "-EN"
    banner = "> 语言：简体中文 ｜ [[%s|English]]" % other
else:
    other = page[:-6] if page.endswith("-EN.md") else page
    banner = "> Language: English ｜ [[%s|简体中文]]" % other
lines.insert(i, banner)
lines.insert(i + 1, "")
open(path, "w", encoding="utf-8").write("\n".join(lines))
PYEOF
}

# ---- build the wiki tree --------------------------------------------
echo "sync_wiki: ${#PAGES[@]} mapped pages (zh canonical + -EN variants)"

# every mapped page must exist in BOTH trees before we push anything
MISSING=0
for p in "${PAGES[@]}"; do
    src="$(map_page "$p")"
    [ -z "$src" ] && { echo "sync_wiki: no source for $p" >&2; exit 1; }
    [ "$src" = "GENERATED" ] && continue
    if [ ! -f "$OC_ROOT/$src" ]; then
        echo "sync_wiki: missing EN source $src for $p" >&2
        MISSING=1
    fi
    if [ ! -f "$OC_ROOT/docs/zh/${src#docs/}" ]; then
        echo "sync_wiki: missing ZH source docs/zh/${src#docs/} for $p" >&2
        MISSING=1
    fi
done
[ "$MISSING" = "1" ] && exit 1

git clone -q "$WIKI_REPO" "$WORK/wiki" 2>/dev/null || {
    # the clone fails both for an empty wiki AND for a wiki that was
    # never initialized (GitHub creates the .wiki.git repository only
    # after the first page is saved from the web UI - no API or git
    # push can create it). Distinguish the two:
    if git ls-remote "$WIKI_REPO" HEAD >/dev/null 2>&1; then
        : # repository exists (empty): create the checkout manually
    else
        echo "sync_wiki: the wiki repository does not exist yet." >&2
        echo "  GitHub lazily creates OpenCubeOS.wiki.git only when the" >&2
        echo "  first page is saved from the web UI; no token/API path" >&2
        echo "  can initialize it." >&2
        echo "  ONE-TIME manual step: open" >&2
        echo "    https://github.com/cubestudio-dev/OpenCubeOS/wiki" >&2
        echo "  create the Home page with any content (one line is fine)" >&2
        echo "  and save it, then re-run this script - it will push all" >&2
        echo "  mapped pages in both languages." >&2
        exit 1
    fi
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
        # (both languages, same data)
        {
            echo "<!-- SPDX-License-Identifier: Apache-2.0 -->"
            echo "# 发布说明"
            echo
            echo "> 语言：简体中文 ｜ [[Release-Notes-EN|English]]"
            echo
            echo "每个版本都出自同一条可复现流水线；完整说明见"
            echo "[发布页](https://github.com/cubestudio-dev/OpenCubeOS/releases)。"
            echo
            python3 - "$OC_ROOT" zh <<'PYEOF'
import json, os, re, sys
root, lang = sys.argv[1], sys.argv[2]
sd = os.path.join(root, "website", "assets", "site-data.js")
txt = open(sd).read()
ver = re.search(r'version: "([^"]+)"', txt)
build = re.search(r'build: (\d+)', txt)
date = re.search(r'date: "([^"]+)"', txt)
iso = re.search(r'name: "(opencube-[^"]+\.iso)"', txt)
sha = re.search(r'sha256: "([0-9a-f]{64})"', txt)
print("| 版本 | 日期 | 构建 |")
print("|------|------|------|")
if ver and date and build:
    print("| %s | %s | %s |" % (ver.group(1), date.group(1), build.group(1)))
hist = re.findall(r'\{ version: "([^"]+)", date: "([^"]+)"', txt)
for v, d in hist:
    print("| %s | %s | - |" % (v, d))
print()
print("当前 ISO：`%s`" % (iso.group(1) if iso else "-"))
print("ISO SHA256：`%s`" % (sha.group(1) if sha else "-"))
PYEOF
        } > "$WORK/wiki/Release-Notes.md"
        {
            echo "<!-- SPDX-License-Identifier: Apache-2.0 -->"
            echo "# Release notes"
            echo
            echo "> Language: English ｜ [[Release-Notes|简体中文]]"
            echo
            echo "Every release ships from the same reproducible pipeline;"
            echo "full notes live on the"
            echo "[releases page](https://github.com/cubestudio-dev/OpenCubeOS/releases)."
            echo
            python3 - "$OC_ROOT" en <<'PYEOF'
import json, os, re, sys
root, lang = sys.argv[1], sys.argv[2]
sd = os.path.join(root, "website", "assets", "site-data.js")
txt = open(sd).read()
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
        } > "$WORK/wiki/Release-Notes-EN.md"
        continue
    fi

    # Chinese canonical page
    cp "$OC_ROOT/docs/zh/${src#docs/}" "$WORK/wiki/$p.md"
    # strip the generated-mirror header comment (3 lines) where present
    sed -i '1{/GENERATED mirror/{N;N;d}}' "$WORK/wiki/$p.md"
    OC_WIKI_SRCDIR="$(dirname "${src#docs/}")" OC_WIKI_SUFFIX="" rewrite_links "$WORK/wiki/$p.md"
    insert_banner "$WORK/wiki/$p.md" "$p.md"

    # English variant page
    cp "$OC_ROOT/$src" "$WORK/wiki/$p-EN.md"
    sed -i '1{/GENERATED mirror/{N;N;d}}' "$WORK/wiki/$p-EN.md"
    OC_WIKI_SRCDIR="$(dirname "${src#docs/}")" OC_WIKI_SUFFIX="-EN" rewrite_links "$WORK/wiki/$p-EN.md"
    insert_banner "$WORK/wiki/$p-EN.md" "$p-EN.md"
done

# ---- local dead-link check ------------------------------------------
echo "sync_wiki: checking wiki-internal links ..."
python3 - "$WORK/wiki" <<'PYEOF'
import os, re, sys
wiki = sys.argv[1]
files = [f for f in os.listdir(wiki) if f.endswith(".md")]
names = {f[:-3] for f in files}
dead = []
checked = 0
for f in files:
    text = open(os.path.join(wiki, f), encoding="utf-8").read()
    for m in re.finditer(r"\[\[([^\]|]+)(?:\|[^\]]*)?\]\]", text):
        target = m.group(1).strip()
        checked += 1
        if target not in names:
            dead.append((f, target))
    for m in re.finditer(r"\]\(([^)#]+\.md|[^)#]+/)\)", text):
        t = m.group(1).strip()
        if t.startswith("http") or t.startswith("mailto"):
            continue
        checked += 1
        if t.rstrip("/") not in names:
            dead.append((f, t))
if dead:
    print("DEAD LINKS: %d" % len(dead))
    for f, t in dead[:20]:
        print("  %s -> %s" % (f, t))
    sys.exit(1)
print("link check: %d links, 0 dead" % checked)
PYEOF

cd "$WORK/wiki"
if [ "$DRY" = "1" ]; then
    echo "sync_wiki: DRY RUN - pages in $WORK/wiki:"
    ls -1 . | sed 's/^/    /' | head -170
    if [ "${OC_WIKI_KEEP:-0}" = "1" ]; then
        echo "sync_wiki: KEEP=1 - tree preserved at $WORK/wiki"
    else
        rm -rf "$WORK"
    fi
    exit 0
fi
git add -A
if git diff --cached --quiet; then
    echo "sync_wiki: wiki already up to date"
else
    git commit -q -m "docs sync: bilingual documentation tree -> wiki (tools/sync_wiki.sh)"
    git -c http.version=HTTP/1.1 push -q origin master 2>/dev/null \
        || git -c http.version=HTTP/1.1 push -q origin main
    echo "sync_wiki: pushed $(( ${#PAGES[@]} * 2 )) pages (zh canonical + en)"
fi
echo "sync_wiki: page list:"
ls -1 . | sed 's/^/    /' | head -170
rm -rf "$WORK"
