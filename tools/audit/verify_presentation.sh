#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS WP-presentation-consistency-audit - ledger verifier.
#
# Machine reconciliation of docs/reference/presentation-consistency-
# ledger.md: every FIXED/VERIFIED row is re-derived from the tree and
# re-checked on its fixed surfaces.  EXEMPT rows are listed with their
# register entry (human-audited).
#
# Usage: tools/audit/verify_presentation.sh [--root DIR] [--facts FILE]
# Exit:  0 all checks pass; 2 any check fails.
set -u

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
FACTS="$ROOT/evidence/presentation-audit/fact-source.json"

while [ $# -gt 0 ]; do
  case "$1" in
    --root)  ROOT="$(cd "$2" && pwd)"; FACTS="$ROOT/evidence/presentation-audit/fact-source.json"; shift 2 ;;
    --facts) FACTS="$2"; shift 2 ;;
    *) echo "usage: $0 [--root DIR] [--facts FILE]" >&2; exit 1 ;;
  esac
done

FAIL=0
pass() { echo "  PASS  $1"; }
fail() { echo "  FAIL  $1"; FAIL=$((FAIL+1)); }

echo "=== 1. rebuild the fact source ==="
if bash "$ROOT/tools/audit/build_fact_source.sh" --out "$FACTS" \
     --help-a "$ROOT/evidence/presentation-audit/help-a-output.txt" > /tmp/vp_facts.log 2>&1; then
  pass "fact source rebuild (all internal cross-checks)"
else
  fail "fact source rebuild"; cat /tmp/vp_facts.log | tail -8
fi

echo "=== 2. ledger A-row facts (re-derived) ==="
python3 - "$FACTS" "$ROOT" <<'PYEOF'
import json, re, sys, subprocess
F = json.load(open(sys.argv[1])); root = sys.argv[2]
fails = []

def check(name, got, want):
    if got == want:
        print(f"  PASS  {name} = {got}")
    else:
        print(f"  FAIL  {name}: got {got}, want {want}")
        fails.append(name)

c = F["commands"]
check("A1 commands_total", c["total"], 311)
check("A2 ring3", c["ring3_total"], 300)
check("A3 builtins (registry caliber)", c["builtins"], 11)
check("A4 services", F["services"]["oc_svc_decl"], 20)
check("A5 l1svc_ops", F["abi"]["l1svc_ops"], 221)
check("A6 syscalls", F["abi"]["syscalls"], 54)
check("A7 drivers legacy+framework", (F["drivers"]["legacy_oc_drv_decl"],
      F["drivers"]["framework_oc_drvi_decl"]), (38, 7))
check("A10 dictionary EN/zh", (F["dictionary"]["en_pages"],
      F["dictionary"]["zh_pages"]), (26, 26))
check("A13 mega apps/ops", (c["mega_apps"], c["mega_ops_total"]), (8, 154))

sys.exit(1 if fails else 0)
PYEOF
[ $? -ne 0 ] && FAIL=$((FAIL+1))

echo "=== 3. fixed surfaces carry the facts (and not the stale values) ==="
python3 - "$ROOT" <<'PYEOF'
import re, sys
root = sys.argv[1]
fails = []
SURFACE_MUST = {
  "README.md": [("311", 1), ("300 ring-3", 1), ("163,935", 1)],
  "website/index.html": [("311", 1), ("221", 1), ("163,935", 1),
                          ("3.0.0-WP08-driver-framework", 1)],
  "website/architecture.html": [("221", 1), ("300 programs", 1),
                                 ("framework/ (.oc_drvi)", 1),
                                 ("OC_DRIVER_INFO_DECL", 1)],
  "docs/reference/presentation-consistency-ledger.md": [("311", 1), ("221", 1)],
}
SURFACE_MUST_NOT = {
  "README.md": ["284 ring-3", "296 commands", "158,474"],
  "website/index.html": ["296", "275 programs", "src/L1/", "158,474", "16 driver families"],
  "website/architecture.html": ["190 numbered", "src/L0/", "128,890"],
  "website/download.html": ["WP-l1apps-merge - development preview"],
  "docs/index.md": ["208 ops", "296-command"],
}
for path, wants in SURFACE_MUST.items():
    try:
        t = open(f"{root}/{path}", encoding="utf-8").read()
    except OSError:
        print(f"  FAIL  {path}: missing"); fails.append(path); continue
    for needle, _ in wants:
        if needle not in t:
            print(f"  FAIL  {path}: missing {needle!r}"); fails.append(path)
        else:
            print(f"  PASS  {path} contains {needle!r}")
for path, needles in SURFACE_MUST_NOT.items():
    try:
        t = open(f"{root}/{path}", encoding="utf-8").read()
    except OSError:
        print(f"  FAIL  {path}: missing"); fails.append(path); continue
    for needle in needles:
        if needle in t:
            print(f"  FAIL  {path}: still contains {needle!r}"); fails.append(path)
        else:
            print(f"  PASS  {path} free of {needle!r}")
sys.exit(1 if fails else 0)
PYEOF
[ $? -ne 0 ] && FAIL=$((FAIL+1))

echo "=== 4. dictionary gate (A10) ==="
if bash "$ROOT/tools/audit/verify_docs_counts.sh" --root "$ROOT" > /tmp/vp_gate.log 2>&1; then
  pass "dictionary == registry == enumeration, EN+zh, examples aligned"
else
  fail "dictionary gate"; tail -5 /tmp/vp_gate.log
fi

echo "=== 5. registry zero-drift (commands.json == enumeration) ==="
if bash "$ROOT/tools/docs/enum_apps.sh" --root "$ROOT" > /tmp/vp_enum.log 2>&1; then
  pass "enum_apps registry reconciliation"
else
  fail "enum_apps drift"; tail -4 /tmp/vp_enum.log
fi

echo "=== 6. D1 version four-way ==="
V=$(python3 -c "import json;print(json.load(open('$ROOT/website/update.json'))['version'])" 2>/dev/null)
T=$(cd "$ROOT" && git describe --tags --abbrev=0 2>/dev/null)
U=$(python3 -c "import json,urllib.request;print('skip')" 2>/dev/null)
TAG="v$V"
if [ "$V" = "3.0.0-WP08-driver-framework" ] && [ "$T" = "$TAG" ] && grep -q "3.0.0-WP08-driver-framework" "$ROOT/website/index.html"; then
  pass "update.json = tag = website = $V (release body checked in the release step)"
else
  fail "version four-way: update.json=$V tag=$T"
fi

echo "=== 7. B1/B2 screenshots regenerated (differ from the WP06g2 blobs) ==="
for shot in shot-bios shot-uefi; do
  P="$ROOT/website/assets/$shot.png"
  if [ ! -f "$P" ]; then fail "$shot.png missing"; continue; fi
  OLD=$(cd "$ROOT" && git rev-parse "HEAD:website/assets/$shot.png" 2>/dev/null)
  NEW=$(cd "$ROOT" && git hash-object "$P")
  if [ "$OLD" != "$NEW" ]; then pass "$shot.png regenerated (blob changed)"; else fail "$shot.png unchanged from HEAD"; fi
done

echo "=== 8. E1 bug-list.md absent; E2 mirror items absent ==="
if [ -f "$ROOT/bug-list.md" ]; then fail "bug-list.md still present"; else pass "bug-list.md removed"; fi
if grep -rq "Private mirror complete\|私有镜像仓完整" "$ROOT/docs/release/" 2>/dev/null; then
  fail "ci-gates mirror item still present"
else
  pass "ci-gates mirror items removed (EN+zh)"
fi

echo "=== 9. E3 privacy scan zero (non-tool) hits ==="
if bash "$ROOT/tools/audit/scan_privacy.sh" --root "$ROOT" > /tmp/vp_priv.log 2>&1; then
  pass "privacy word-list scan: zero real hits"
else
  fail "privacy scan has hits:"; grep '^{' /tmp/vp_priv.log | grep -v internal-env-words | head -6
fi

echo "=== 10. exemption register present ==="
if grep -q "## Exemption register" "$ROOT/docs/reference/presentation-consistency-ledger.md"; then
  pass "ledger exemption register present"
else
  fail "ledger exemption register missing"
fi

echo
if [ "$FAIL" = "0" ]; then
  echo "verify_presentation: ALL CHECKS PASS"
  exit 0
else
  echo "verify_presentation: $FAIL check(s) FAILED"
  exit 2
fi
