#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS WP-presentation-consistency-audit - privacy scanner.
#
# Full-tree + full-site privacy scan against the internal-environment
# word list (tools/audit/internal-env-words.txt - additions allowed,
# removals forbidden).  The word list is the single caliber; this
# wrapper runs the battle-tested engine (scan_internal_env.sh) over
# BOTH surfaces and reports raw hits verbatim:
#
#   surface 1: the repository working tree (--root)
#   surface 2: a deployed site tree (--site, e.g. a gh-pages checkout
#              or website/public) - scanned with the same word list
#
# Raw output goes to stdout as JSONL (one record per hit) plus a
# coverage footer per surface.  PASS requires zero non-tool hits on
# both surfaces (tool self-references in the word list itself are the
# pattern definitions, listed separately).
#
# Usage: tools/audit/scan_privacy.sh [--root DIR] [--site DIR] [--words FILE]
# Exit:  0 zero hits; 2 hits found.
set -u

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SITE=""
WORDS="$ROOT/tools/audit/internal-env-words.txt"

while [ $# -gt 0 ]; do
  case "$1" in
    --root)  ROOT="$(cd "$2" && pwd)"; WORDS="$ROOT/tools/audit/internal-env-words.txt"; shift 2 ;;
    --site)  SITE="$2"; shift 2 ;;
    --words) WORDS="$2"; shift 2 ;;
    -h|--help) grep '^#' "$0" | sed '1d'; exit 0 ;;
    *) echo "usage: $0 [--root DIR] [--site DIR] [--words FILE]" >&2; exit 1 ;;
  esac
done

rc=0
total_non_tool=0

scan_one() {
  local name="$1" dir="$2"
  echo "=== surface: $name ($dir) ==="
  local out
  out=$(bash "$(cd "$(dirname "$0")" && pwd)/scan_internal_env.sh" \
        --root "$dir" --words "$WORDS" 2>/dev/null)
  local n_total n_tool n_real
  n_total=$(echo "$out" | grep -c '^{' || true)
  n_tool=$(echo "$out" | grep '{"file": "tools/audit/internal-env-words.txt"' -c || true)
  n_real=$((n_total - n_tool))
  echo "$out" | grep '^{'
  echo "--- $name: hits=$n_total (tool self-refs=$n_tool, real=$n_real)"
  total_non_tool=$((total_non_tool + n_real))
}

scan_one "repo-tree" "$ROOT"
if [ -n "$SITE" ]; then
  scan_one "site-tree" "$SITE"
fi

echo "=== privacy scan: real hits total = $total_non_tool ==="
[ "$total_non_tool" = "0" ] || rc=2
exit $rc
