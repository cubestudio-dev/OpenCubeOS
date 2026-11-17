#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - QEMU-coupling audit scanner (WP-docs-cleanup).
#
# Collects EVERY line in the engineering tree that couples the system
# to the QEMU acceptance platform.  Read-only: this script never
# modifies, moves or deletes anything it scans.
#
# Category model (the work-package contract):
#   A1  CPUID hypervisor-bit probe that changes behavior
#   A2  DMI/SMBIOS string match (QEMU/Bochs/SeaBIOS/OVMF)
#   A3  fw_cfg channel use (ports 0x510..0x514 / DMA interface)
#   A4  ACPI OEM-id / table-signature match on QEMU firmware marks
#   A5  PCI-ID whitelist aimed at QEMU-emulated devices
#   B1  hardcoded QEMU-platform address/port/layout/IRQ constants
#   B2  driver written to QEMU virtual-device timing, no real-machine
#       fallback
#   B3  init path simplified for QEMU; real-machine steps skipped
#   B4  firmware-behavior assumption limited to OVMF/SeaBIOS
#   B5  QEMU-specific enumeration order / BAR layout / ACPI shape
#   B6  acceptance test executable only under QEMU
#   M   plain mention (curated later into A/B or discarded with a
#       stated false-positive reason; nothing is dropped silently)
#
# Boundary rule honored by the CURATION (not by this scanner): a
# driver written to a published virtual-hardware SPEC (virtio, e1000
# register spec, AHCI spec...) is NOT coupling; the scanner still
# records its QEMU mentions under M and the inventory states the
# exclusion reason per entry.
#
# Usage:
#   tools/audit/scan_qemu_coupling.sh [--root DIR] [--out FILE]
#          [--words FILE] [--cover FILE]
#     --root   tree to scan (default: repo root)
#     --out    hits JSONL (default: stdout)
#     --words  extra patterns file (one 'category<TAB>regex' per
#              line; '#' comments) - ADDS to the built-in set
#     --cover  per-file coverage record (scanned=yes hits=N)
#
# Output hit record (JSON per line):
#   {"file": "...", "line": 42, "cat": "A1", "snippet": "..."}
set -u

ROOT="" OUT="" WORDS="" COVER=""
while [ $# -gt 0 ]; do
    case "$1" in
        --root)  ROOT="$2"; shift 2 ;;
        --out)   OUT="$2"; shift 2 ;;
        --words) WORDS="$2"; shift 2 ;;
        --cover) COVER="$2"; shift 2 ;;
        -h|--help) grep '^#' "$0" | sed '1d'; exit 0 ;;
        *) echo "scan_qemu_coupling.sh: unknown arg: $1" >&2; exit 1 ;;
    esac
done
if [ -n "$ROOT" ]; then ROOT=$(cd "$ROOT" && pwd); else ROOT=$(cd "$(dirname "$0")/../.." && pwd); fi

exec python3 - "$ROOT" "$OUT" "$WORDS" "$COVER" <<'PYEOF'
import json, os, re, sys

ROOT = sys.argv[1]
OUT = sys.argv[2] if len(sys.argv) > 2 else ""
WORDS = sys.argv[3] if len(sys.argv) > 3 else ""
COVER = sys.argv[4] if len(sys.argv) > 4 else ""

SKIP_DIRS = {".git", "node_modules", "build", "dist", "__pycache__",
             ".venv"}
SKIP_EXT = {".iso", ".img", ".zip", ".tar", ".gz", ".bin", ".elf", ".o",
            ".a", ".zip", ".deb", ".fd", ".pdf", ".png", ".jpg",
            ".jpeg", ".woff", ".woff2", ".ttf", ".ico", ".mp4", ".woff"}

# built-in pattern set: (category, compiled regex).  Patterns are
# deliberately NARROW: they must not drown the curation in noise, but
# the M catch-all (any QEMU-family mention) guarantees nothing is
# missed.
BUILTIN = [
    ("A1", re.compile(r"under_hypervisor|cpuid[^/\n]{0,60}(bit\s*31|hypervisor)|hypervisor\s+bit|ECX[^/\n]{0,20}31")),
    ("A2", re.compile(r"(?i)\b(dmi|smbios)\b[^/\n]{0,80}(qemu|bochs|seabios|ovmf)|(qemu|bochs|seabios|ovmf)[^/\n]{0,80}\b(dmi|smbios)\b")),
    ("A3", re.compile(r"\bfw_cfg\b|\bfwcfg\b|FW_CFG|selector\s*0x51[0-4]|\(0x51[0-4]")),
    ("A4", re.compile(r"BOCHS\b|OemId[^/\n]{0,40}(QEMU|BOCHS)|(QEMU|BOCHS)[^/\n]{0,20}OEM")),
    ("A5", re.compile(r"(?i)(qemu|virtio|i440fx|piix)[^/\n]{0,40}(8086|1b36|1af4)\s*[:=]?\s*:?[\da-fA-F]{4}|0x1237\b|1af4:10|1b36:")),
    ("B1", re.compile(r"(?i)qemu[^/\n]{0,80}?(0x[0-9A-Fa-f]{4,8})|(0x[0-9A-Fa-f]{4,8})[^/\n]{0,40}qemu")),
    ("B2", re.compile(r"(?i)(qemu|qemu's)[^/\n]{0,140}(timing|quirk|model|cache|ring|delay|order|workaround|requires|expects)")),
    ("B3", re.compile(r"(?i)qemu[^/\n]{0,100}(skip|simplif|only|absent|not present|no real|real hw|real machine)")),
    ("B4", re.compile(r"(?i)(ovmf|seabios)[^/\n]{0,100}(firmware|behavior|expect|assum)|firmware[^/\n]{0,60}(ovmf|seabios)")),
    ("B5", re.compile(r"(?i)qemu[^/\n]{0,100}(enumerat|BAR|assigns?|order|slot)")),
    ("B6", re.compile(r"qemu-system-x86_64|qemu-system-aarch64")),
    ("M",  re.compile(r"(?i)\b(qemu|bochs|seabios|ovmf|fw_cfg|fwcfg|i6300esb|i440fx|piix4?|virtio[-_ ]?(net|blk|net-pci))\b")),
]

extra = []
if WORDS and os.path.exists(WORDS):
    for ln in open(WORDS):
        ln = ln.strip()
        if not ln or ln.startswith("#"):
            continue
        cat, _, pat = ln.partition("\t")
        if pat:
            try:
                extra.append((cat, re.compile(pat)))
            except re.error as e:
                sys.stderr.write("bad pattern %r: %s\n" % (pat, e))

PATTERNS = BUILTIN + extra

def is_text(path):
    try:
        with open(path, "rb") as f:
            head = f.read(8192)
        return b"\x00" not in head
    except OSError:
        return False

hits_out = open(OUT, "w") if OUT else sys.stdout
cover_out = open(COVER, "w") if COVER else None
n_files = n_hits = 0
per_dir = {}

for dirpath, dirnames, filenames in os.walk(ROOT):
    dirnames[:] = sorted(d for d in dirnames if d not in SKIP_DIRS)
    for fn in sorted(filenames):
        ext = os.path.splitext(fn)[1].lower()
        if ext in SKIP_EXT:
            continue
        p = os.path.join(dirpath, fn)
        rel = os.path.relpath(p, ROOT)
        if not is_text(p):
            continue
        n_files += 1
        fhits = 0
        try:
            with open(p, "r", encoding="utf-8", errors="replace") as f:
                for i, line in enumerate(f, 1):
                    cats = [c for c, rx in PATTERNS if rx.search(line)]
                    if not cats:
                        continue
                    # keep the strongest category (A/B over M)
                    best = sorted(cats, key=lambda c: (c == "M", c))[0]
                    rec = {"file": rel, "line": i, "cat": best,
                           "cats": cats,
                           "snippet": line.rstrip("\n")[:240]}
                    hits_out.write(json.dumps(rec, ensure_ascii=False)
                                   + "\n")
                    fhits += 1
                    n_hits += 1
        except OSError:
            continue
        if cover_out:
            cover_out.write(json.dumps(
                {"file": rel, "scanned": 1, "hits": fhits},
                ensure_ascii=False) + "\n")
        d = os.path.dirname(rel) or "."
        s = per_dir.setdefault(d, {"files": 0, "hits": 0, "entries": 0})
        s["files"] += 1
        s["hits"] += fhits

if cover_out:
    cover_out.close()
if OUT:
    hits_out.close()

summary = {"tool": "tools/audit/scan_qemu_coupling.sh", "root": ROOT,
           "files_scanned": n_files, "hit_lines": n_hits,
           "per_dir": per_dir}
sys.stderr.write(json.dumps(summary, ensure_ascii=False, indent=1)
                 + "\n")
PYEOF
