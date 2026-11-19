#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS WP-presentation-consistency-audit - presentation layer
# scanner.
#
# Scans every user-visible surface (root docs, docs/ tree, website,
# published data) for numeric claims, path claims, version strings and
# image references, then reconciles each against the fact source
# (tools/audit/build_fact_source.sh).
#
# Baseline coverage (A numbers, B images, C docs, D claims, E privacy
# is a separate scanner) plus extensions: stale paths (src/L0, src/L1),
# stale work-package version strings, code-size claims, category-page
# counts, dictionary counts, meta descriptions, download-page tables.
#
# Output: JSON diff list (default evidence/presentation-audit/scan-presentation.json)
#         + human summary on stdout.
#
# Usage: tools/audit/scan_presentation.sh [--root DIR] [--facts FILE] [--out FILE]
# Exit:  0 no findings; 2 findings (each is a fix candidate).
set -u

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
FACTS="$ROOT/evidence/presentation-audit/fact-source.json"
OUT="$ROOT/evidence/presentation-audit/scan-presentation.json"

while [ $# -gt 0 ]; do
  case "$1" in
    --root)  ROOT="$(cd "$2" && pwd)"; FACTS="$ROOT/evidence/presentation-audit/fact-source.json"; OUT="$ROOT/evidence/presentation-audit/scan-presentation.json"; shift 2 ;;
    --facts) FACTS="$2"; shift 2 ;;
    --out)   OUT="$2"; shift 2 ;;
    *) echo "usage: $0 [--root DIR] [--facts FILE] [--out FILE]" >&2; exit 1 ;;
  esac
done
mkdir -p "$(dirname "$OUT")"

python3 - "$ROOT" "$FACTS" "$OUT" <<'PYEOF'
import json, os, re, sys

root, facts_path, out_path = sys.argv[1:4]
F = json.load(open(facts_path))
findings = []

cmd = F["commands"]
FACT = {
    "commands_total": cmd["total"],
    "ring3": cmd["ring3_total"],
    "builtins_enum": cmd["builtins"],
    "services": F["services"]["oc_svc_decl"],
    "l1svc_ops": F.get("abi", {}).get("l1svc_ops", 221),
    "syscalls": F.get("abi", {}).get("syscalls", 54),
    "drivers_legacy": F["drivers"]["legacy_oc_drv_decl"],
    "drivers_framework": F["drivers"]["framework_oc_drvi_decl"],
    "driver_families": 7,
    "release_version": F["version"]["update_json_version"],
    "release_build": F["version"]["update_json_build"],
    "categories": F["categories"]["total"],
    "dict_pages": F["dictionary"]["en_pages"],
    "mega_apps": cmd["mega_apps"],
    "mega_ops": cmd["mega_ops_total"],
}

# ---- surfaces to scan -------------------------------------------------
SURFACES = []
for f in os.listdir(root):
    if f.endswith(".md"):
        SURFACES.append((f, "root-doc"))
for sub in ("docs", "website"):
    base = os.path.join(root, sub)
    for dirpath, _dirs, files in os.walk(base):
        if "node_modules" in dirpath or "/out/" in dirpath or os.sep + "out" == dirpath[-4:]:
            continue
        for fn in files:
            if fn.endswith((".md", ".html", ".json", ".js", ".css")):
                rel = os.path.relpath(os.path.join(dirpath, fn), root)
                if rel.startswith("docs/data/"):
                    continue  # registry data itself, not a claim
                SURFACES.append((rel, "site" if rel.startswith("website") else "docs"))

# ---- numeric claim patterns -------------------------------------------
# Each: (id, regex, fact_key, context) - a hit whose captured number
# differs from the fact is a finding.
NUM_PATTERNS = [
    ("commands-total", r"\b(\d{2,4})\s+(?:total\s+)?commands\b", "commands_total",
     "total command count"),
    ("ring3-programs", r"\b(\d{2,4})\s+ring-3\s+programs", "ring3",
     "ring-3 program count"),
    ("programs-l1apps", r"\b(\d{2,4})\s+programs\s+under\s+`?ocos/l1apps", "ring3",
     "programs under ocos/l1apps"),
    ("ush-commands", r"\b(\d{2,4})\s+commands\s*=\s*\d+", "commands_total",
     "commands = N breakdown"),
    ("builtins", r"\b(\d{1,2})\s+built-?ins?\b", "builtins_enum",
     "ush built-in count"),
    ("services", r"\b(\d{1,2})\s+(?:SYS_L1SVC\s+)?(?:service\s+)?categories?\s*/\s*\d+\s+ops", "services",
     "services/ops pair"),
    ("l1svc-services", r"\b(\d{1,2})\s+(?:SYS_L1SVC|L1SVC)\s+services", "services",
     "L1SVC services"),
    ("twenty-services", r"\b(\d{1,2})\s+services\b", "services", "services count"),
    ("ops-count", r"\b(\d{2,4})\s+(?:numbered\s+(?:entries|ops)|L1SVC\s+ops)", "l1svc_ops",
     "L1SVC ops count"),
    ("syscalls", r"\b(\d{1,3})\s+syscalls", "syscalls", "syscall count"),
    ("driver-families", r"\b(\d{1,2})\s+declared\s+driver\s+famil", "driver_families",
     "declared driver families"),
    ("driver-families2", r"\b(\d{1,2})\s+driver\s+famil", "driver_families",
     "driver families"),
    ("drivers-count", r"(?<!PHASE )\b(\d{1,3})\s+drivers\b", "drivers_legacy",
     "driver count"),
    ("mega-apps", r"\b(\d{1,2})\s+(?:merged|mega)\s+apps", "mega_apps",
     "mega apps"),
    ("dictionary-entries", r"\b(\d{2,4})\s+(?:command\s+)?(?:entries|条目)\s+(?:in\s+the\s+)?(?:the\s+)?(?:application\s+)?dictionary", "commands_total",
     "dictionary entries"),
]

# stale work-package version strings in user-visible surfaces
STALE_VERSIONS = [
    r"3\.0\.0-WP-l1apps-merge", r"3\.0\.0-WP06g2", r"3\.0\.0-WP-0\d\b",
    r"3\.0\.0-WP-07\b", r"3\.0\.0-WP-l1apps", r"v?3\.0\.0-WP-(?!08-driver-framework|fix-residual-defects)\w+",
]
# stale paths
STALE_PATHS = [
    (r"\bsrc/L0\b", "src/L0 was restructured to ocos/ (kernel/services/drivers)"),
    (r"\bsrc/L1\b", "src/L1 was restructured to ocos/l1apps"),
]

def fact_for(key):
    return FACT[key]

# historical-surface exemptions (auditable rules, not silence):
#  - docs/release/*.md are frozen release notes: they describe the
#    tree AT THAT RELEASE; rewriting their numbers would falsify
#    history. Links pointing INTO them are legitimate.
#  - site-data.js / download pages carry real asset names and
#    releases/download URLs of past releases (e.g. the WP-l1apps-merge
#    OCSE medium) - those strings ARE the assets.
#  - KNOWN_ISSUES.md table rows are the frozen per-problem record.
def is_exempt(rel, line):
    norm = rel.replace("\\", "/")
    if "/release/" in norm or norm.startswith("docs/release/"):
        return True
    if "/archive/" in norm or norm.startswith("docs/archive/") or norm.endswith("ARCHIVE-README.md"):
        return True   # frozen historical archive
    if "releases/download/" in line or "releases/tag/" in line:
        return True
    if norm.endswith("KNOWN_ISSUES.md"):
        if line.lstrip().startswith("|"):
            return True   # frozen per-problem table
        if line.startswith("## "):
            return False  # section headers still checked
    if norm.endswith("site-data.js") and (re.match(r"\s*\{\s*version:", line) or "iso_url:" in line):
        return True   # history array entries and past-asset URLs
    if "WP-l1apps-merge" in line and ("ocse-" in line or "release/3.0.0-WP-l1apps-merge.md" in line):
        return True
    return False

scanned = 0
for rel, kind in SURFACES:
    path = os.path.join(root, rel)
    try:
        text = open(path, encoding="utf-8", errors="replace").read()
    except OSError:
        continue
    scanned += 1
    for lineno, line in enumerate(text.splitlines(), 1):
        # per-service op tables ("### services/block (23 ops)") are
        # per-folder stats, not global claims - skip
        if re.match(r"^###\s+services/", line):
            continue
        if is_exempt(rel, line):
            continue
        for pid, rx, fkey, ctx in NUM_PATTERNS:
            for m in re.finditer(rx, line):
                try:
                    got = int(m.group(1))
                except ValueError:
                    continue
                want = fact_for(fkey)
                if got != want:
                    findings.append({
                        "file": rel, "line": lineno, "pattern": pid,
                        "context": ctx, "claimed": got, "fact": want,
                        "text": line.strip()[:200],
                    })
        for rx in STALE_VERSIONS:
            m = re.search(rx, line)
            if m:
                findings.append({
                    "file": rel, "line": lineno, "pattern": "stale-version",
                    "context": f"stale version string {m.group(0)}",
                    "claimed": m.group(0), "fact": FACT["release_version"],
                    "text": line.strip()[:200],
                })
        for rx, why in STALE_PATHS:
            if re.search(rx, line):
                findings.append({
                    "file": rel, "line": lineno, "pattern": "stale-path",
                    "context": why, "claimed": rx, "fact": "ocos/ layout",
                    "text": line.strip()[:200],
                })

# image inventory (for the image-consistency audit)
images = []
for rel, kind in SURFACES:
    path = os.path.join(root, rel)
    try:
        text = open(path, encoding="utf-8", errors="replace").read()
    except OSError:
        continue
    for m in re.finditer(r'<img[^>]+src="([^"]+)"', text):
        images.append({"file": rel, "ref": m.group(1)})
    for m in re.finditer(r'!\[[^\]]*\]\(([^)]+)\)', text):
        images.append({"file": rel, "ref": m.group(1)})

json.dump({"tool": "tools/audit/scan_presentation.sh",
           "facts_used": {k: FACT[k] for k in FACT},
           "scanned_files": scanned, "findings": findings,
           "image_references": images},
          open(out_path, "w"), indent=1)

print(f"scanned {scanned} presentation files")
print(f"image references: {len(images)}")
print(f"findings: {len(findings)}")
byfile = {}
for f in findings:
    byfile.setdefault(f["file"], []).append(f)
for fn in sorted(byfile):
    print(f"\n{fn} ({len(byfile[fn])}):")
    for f in byfile[fn][:40]:
        print(f"  L{f['line']} [{f['pattern']}] {f['context']}: "
              f"claimed={f['claimed']} fact={f['fact']}")
        print(f"      {f['text'][:150]}")
if findings:
    sys.exit(2)
print("presentation scan: CLEAN")
PYEOF
