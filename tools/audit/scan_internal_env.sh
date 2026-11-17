#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - internal-environment trace scanner (WP-docs-cleanup).
#
# Scans the public tree for internal-environment traces (sandbox
# paths, token material, private-repo references, work-log and
# disaster-recovery vocabulary) against a word list.  Read-only.
#
# The word list is a file (default: tools/audit/internal-env-words.txt,
# the reviewed minimum baseline - additions allowed, removals
# forbidden) so the audit is repeatable and the exact caliber is
# in-repo.
#
# Usage:
#   tools/audit/scan_internal_env.sh [--root DIR] [--words FILE]
#          [--out FILE] [--cover FILE]
#     --root   tree to scan (default: repo root)
#     --words  word list (label<TAB>regex per line; default shipped)
#     --out    hits JSONL (default: stdout)
#     --cover  per-file coverage record (scanned=yes hits=N)
#
# Output hit record (JSON per line):
#   {"file": "...", "line": 7, "label": "home-z", "snippet": "..."}
set -u

ROOT="" OUT="" WORDS="" COVER=""
while [ $# -gt 0 ]; do
    case "$1" in
        --root)  ROOT="$2"; shift 2 ;;
        --out)   OUT="$2"; shift 2 ;;
        --words) WORDS="$2"; shift 2 ;;
        --cover) COVER="$2"; shift 2 ;;
        -h|--help) grep '^#' "$0" | sed '1d'; exit 0 ;;
        *) echo "scan_internal_env.sh: unknown arg: $1" >&2; exit 1 ;;
    esac
done
if [ -n "$ROOT" ]; then ROOT=$(cd "$ROOT" && pwd); else ROOT=$(cd "$(dirname "$0")/../.." && pwd); fi
if [ -z "$WORDS" ]; then WORDS="$(cd "$(dirname "$0")" && pwd)/internal-env-words.txt"; fi
if [ ! -f "$WORDS" ]; then
    echo "scan_internal_env.sh: word list not found: $WORDS" >&2; exit 1
fi

exec python3 - "$ROOT" "$OUT" "$WORDS" "$COVER" <<'PYEOF'
import json, os, re, sys

ROOT = sys.argv[1]
OUT = sys.argv[2] if len(sys.argv) > 2 else ""
WORDS = sys.argv[3]
COVER = sys.argv[4] if len(sys.argv) > 4 else ""

SKIP_DIRS = {".git", "node_modules", "build", "dist", "__pycache__",
             ".venv"}
SKIP_EXT = {".iso", ".img", ".zip", ".tar", ".gz", ".bin", ".elf", ".o",
            ".a", ".deb", ".fd", ".pdf", ".png", ".jpg", ".jpeg",
            ".woff", ".woff2", ".ttf", ".ico", ".mp4"}

patterns = []
for ln in open(WORDS, encoding="utf-8"):
    ln = ln.strip()
    if not ln or ln.startswith("#"):
        continue
    label, _, pat = ln.partition("\t")
    if not pat:
        continue
    try:
        patterns.append((label, re.compile(pat)))
    except re.error as e:
        sys.stderr.write("bad pattern %r: %s\n" % (pat, e))
        sys.exit(1)
if not patterns:
    sys.stderr.write("empty word list: %s\n" % WORDS)
    sys.exit(1)

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
                    for label, rx in patterns:
                        if rx.search(line):
                            hits_out.write(json.dumps(
                                {"file": rel, "line": i, "label": label,
                                 "snippet": line.rstrip("\n")[:240]},
                                ensure_ascii=False) + "\n")
                            fhits += 1
                            n_hits += 1
        except OSError:
            continue
        if cover_out:
            cover_out.write(json.dumps(
                {"file": rel, "scanned": 1, "hits": fhits},
                ensure_ascii=False) + "\n")

if cover_out:
    cover_out.close()
if OUT:
    hits_out.close()

sys.stderr.write(json.dumps(
    {"tool": "tools/audit/scan_internal_env.sh", "root": ROOT,
     "words": WORDS, "files_scanned": n_files, "hit_lines": n_hits},
    ensure_ascii=False, indent=1) + "\n")
PYEOF
