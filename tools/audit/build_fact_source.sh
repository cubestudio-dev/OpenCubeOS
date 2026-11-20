#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS WP-presentation-consistency-audit - fact source builder.
#
# Enumerates the SIX fact sources straight from the code (the single
# authority every presentation layer must reconcile against):
#
#   1. commands   - tools/docs/enum_apps.sh (source-tree enumeration)
#                   cross-checked against docs/data/commands.json and
#                   (when provided) a captured `help -a` transcript
#   2. drivers    - OC_DRIVER_DECL (legacy .oc_drvs) + OC_DRIVER_INFO_DECL
#                   (WP-08 framework .oc_drvi) real declarations
#   3. services   - OC_SERVICE_DECL / OC_SERVICE_DECL_L1 real declarations
#   4. version    - Makefile default, update.json, the git tag surface
#   5. categories - per-category command counts aggregated from the
#                   command enumeration
#   6. dictionary - EN/zh entry counts from the dictionary gate data
#
# Output: JSON (default evidence/presentation-audit/fact-source.json)
#         plus a human summary on stdout.
#
# Usage: tools/audit/build_fact_source.sh [--root DIR] [--out FILE]
#              [--help-a FILE] [--registry FILE]
# Exit:  0 all internal cross-checks pass; 2 any mismatch.
set -u

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/evidence/presentation-audit/fact-source.json"
HELP_A=""
REGISTRY="$ROOT/docs/data/commands.json"

while [ $# -gt 0 ]; do
  case "$1" in
    --root)    ROOT="$(cd "$2" && pwd)"; shift 2 ;;
    --out)     OUT="$2"; shift 2 ;;
    --help-a)  HELP_A="$2"; shift 2 ;;
    --registry) REGISTRY="$2"; shift 2 ;;
    *) echo "usage: $0 [--root DIR] [--out FILE] [--help-a FILE] [--registry FILE]" >&2; exit 1 ;;
  esac
done
mkdir -p "$(dirname "$OUT")"

python3 - "$ROOT" "$OUT" "$HELP_A" "$REGISTRY" <<'PYEOF'
import json, os, re, subprocess, sys

root, out, help_a_path, registry_path = sys.argv[1:5]
facts = {"tool": "tools/audit/build_fact_source.sh", "root": "."}
problems = []

# ---------- 1. commands ----------
enum = json.loads(subprocess.run(
    ["bash", f"{root}/tools/docs/enum_apps.sh", "--format", "json",
     "--root", root, "--registry", registry_path],
    capture_output=True, text=True, check=True).stdout)
cmds = enum["commands"]
ring3 = [c for c in cmds if c["kind"] != "builtin"]
builtins = [c for c in cmds if c["kind"] == "builtin"]
# 4.1 caliber split: hidden entries stay in the registry/source
# enumeration but are not listed by help / help -a / help -w.
hidden_names = set(c["name"] for c in cmds if c.get("hidden"))
ring3_visible = [c for c in ring3 if c["name"] not in hidden_names]
facts["commands"] = {
    "total": len(cmds),
    "ring3_total": len(ring3),
    "ring3_listed": len(ring3_visible),
    "hidden": len(hidden_names),
    "hidden_names": sorted(hidden_names),
    "surface_listed": len(ring3_visible) + len(builtins),
    "ring3_app_dir": enum["counts"]["app_dir"],
    "ring3_flat": enum["counts"]["flat"],
    "builtins": len(builtins),
    "l1svc_wrapped": enum["counts"]["l1svc_wrapped"],
    "mega_apps": enum["counts"]["mega_apps"],
    "mega_ops_total": enum["counts"]["mega_ops_total"],
    "names": sorted(c["name"] for c in cmds),
}

registry = json.load(open(registry_path))
reg_cmds = registry.get("commands", [])
reg_names = sorted(r["name"] for r in reg_cmds) if reg_cmds else []
if reg_names != facts["commands"]["names"]:
    only_src = set(facts["commands"]["names"]) - set(reg_names)
    only_reg = set(reg_names) - set(facts["commands"]["names"])
    problems.append(f"command registry drift: src-only={sorted(only_src)} reg-only={sorted(only_reg)}")

# cross-check against a captured `help -a` transcript
if help_a_path and os.path.exists(help_a_path):
    txt = open(help_a_path, encoding="utf-8", errors="replace").read()
    m = re.search(r"registry \(A-Z, (\d+) programs\)", txt)
    help_n = int(m.group(1)) if m else None
    # the per-program rows are "  name  [cat]  description"
    rows = re.findall(r"^  (\S+)  \[", txt, re.M)
    help_names = sorted(set(rows))
    facts["commands"]["help_a"] = {
        "declared_count": help_n,
        "listed_programs": len(help_names),
        "matches_ring3_listed": help_names == sorted(c["name"] for c in ring3_visible),
    }
    if help_n is None or help_n != len(ring3_visible):
        problems.append(f"help -a count {help_n} != ring3-listed {len(ring3_visible)}")
    if help_names != sorted(c["name"] for c in ring3_visible):
        only_h = set(help_names) - {c["name"] for c in ring3_visible}
        only_s = {c["name"] for c in ring3_visible} - set(help_names)
        problems.append(f"help -a name drift: help-only={sorted(only_h)} src-only={sorted(only_s)}")
    # the hidden set must NOT be listed
    leaked = hidden_names & set(help_names)
    if leaked:
        problems.append(f"hidden entries leaked into help -a: {sorted(leaked)}")

# ---------- 2. drivers ----------
def real_macro_hits(pattern, dirs):
    n = 0
    names = []
    for d in dirs:
        for dirpath, _dirs, files in os.walk(os.path.join(root, d)):
            for fn in files:
                if not fn.endswith((".c",)):
                    continue
                p = os.path.join(dirpath, fn)
                for line in open(p, encoding="utf-8", errors="replace"):
                    s = line.strip()
                    if s.startswith("/*") or s.startswith("*"):
                        continue
                    if re.search(pattern, s):
                        n += 1
    return n

legacy = real_macro_hits(r"OC_DRIVER_DECL\(", ["ocos/drivers"])
fw = real_macro_hits(r"OC_DRIVER_INFO_DECL\(", ["ocos/drivers", "tests"])
facts["drivers"] = {
    "legacy_oc_drv_decl": legacy,
    "framework_oc_drvi_decl": fw,
    "total": legacy + fw,
}

# ---------- 3. services ----------
svc = real_macro_hits(r"OC_SERVICE_DECL(?:_L1)?\(", ["ocos/services"])
facts["services"] = {"oc_svc_decl": svc}

# ---------- 4. version ----------
mk = open(f"{root}/Makefile", encoding="utf-8").read()
m = re.search(r"^OC_RELEASE_VERSION \?= (\S+)", mk, re.M)
makefile_default = m.group(1) if m else None
m = re.search(r"^OC_KERNEL_BUILD_OFFSET := (\d+)", mk, re.M)
build_offset = int(m.group(1)) if m else None
rc = subprocess.run(["git", "-C", root, "rev-list", "--count", "HEAD"],
                    capture_output=True, text=True)
commit_count = int(rc.stdout.strip()) if rc.returncode == 0 else None
makefile_build = (commit_count + build_offset) if (commit_count is not None and build_offset is not None) else None
uj = {}
for cand in (f"{root}/website/update.json", f"{root}/website/public/update.json"):
    if os.path.exists(cand):
        uj = json.load(open(cand)); break
tag = subprocess.run(["git", "-C", root, "describe", "--tags", "--abbrev=0"],
                     capture_output=True, text=True).stdout.strip()
facts["version"] = {
    "makefile_default": makefile_default,
    "makefile_kernel_build_default": makefile_build,
    "makefile_build_offset": build_offset,
    "makefile_commit_count": commit_count,
    "update_json_version": uj.get("version"),
    "update_json_build": uj.get("build"),
    "latest_tag": tag or None,
}

# ---------- 5. categories (the dictionary page grouping) ----------
import importlib.util
spec = importlib.util.spec_from_file_location("gen_dictionary", f"{root}/tools/docs/gen_dictionary.py")
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)
cats = {}
for c in cmds:
    g = mod.group_of(c)[0]
    cats[g] = cats.get(g, 0) + 1
facts["categories"] = {"total": len(cats), "per_category": dict(sorted(cats.items()))}

# ---------- 6. dictionary ----------
def count_entries(path_glob_root):
    n = 0
    for dirpath, _dirs, files in os.walk(path_glob_root):
        for fn in files:
            if fn.endswith(".md"):
                n += 1
    return n

dict_en = count_entries(f"{root}/docs/manual/dictionary")
dict_zh = count_entries(f"{root}/docs/zh/manual/dictionary")
facts["dictionary"] = {"en_pages": dict_en, "zh_pages": dict_zh,
                       "en_equals_zh": dict_en == dict_zh}
if dict_en != dict_zh:
    problems.append(f"dictionary pages EN {dict_en} != zh {dict_zh}")

facts["cross_checks"] = {
    "enum_total": len(cmds),
    "registry_total": len(reg_names),
    "help_a_available": bool(help_a_path and os.path.exists(help_a_path)),
    "dictionary_gate": subprocess.run(
        ["bash", f"{root}/tools/audit/verify_docs_counts.sh", "--root", root],
        capture_output=True, text=True).returncode == 0,
}

# ---------- extra numeric facts (parsed from the ABI headers) ----------
# L1SVC numbered ops: every #define L1SVC_* with a number, excluding
# the L1SVC_CAT_* category identifiers (20) and the L1SVC_CAT_NAME
# macro - the numbered op surface the website/docs cite.
ops_n = 0
for line in open(f"{root}/ocos/services/sysapi/interface/svc_sysapi_l1svc.h",
                  encoding="utf-8"):
    m = re.match(r"#define\s+L1SVC_(?!CAT_)\w+\s+\d+", line)
    if m:
        ops_n += 1
# syscalls: SYS_* numbered defines
sys_n = 0
for line in open(f"{root}/ocos/services/sysapi/interface/svc_sysapi_syscall.h",
                  encoding="utf-8"):
    if re.match(r"#define\s+SYS_[A-Z0-9_]+\s+\d+", line):
        sys_n += 1
facts["abi"] = {"l1svc_ops": ops_n, "syscalls": sys_n,
                "l1svc_categories": len(facts["categories"])}

with open(out, "w") as f:
    json.dump(facts, f, indent=1)

print("=== fact source ===")
print(f"commands : total={facts['commands']['total']} "
      f"(ring3={facts['commands']['ring3_total']} = {facts['commands']['ring3_app_dir']} dir + "
      f"{facts['commands']['ring3_flat']} flat, builtins={facts['commands']['builtins']})")
if "help_a" in facts["commands"]:
    h = facts["commands"]["help_a"]
    print(f"help -a  : declared={h['declared_count']} listed={h['listed_programs']} "
          f"names-match={h['matches_ring3_listed']}")
    print(f"hidden   : {facts['commands']['hidden']} (registered, unlisted: "
          f"{', '.join(facts['commands']['hidden_names'])})")
    print(f"listed   : surface={facts['commands']['surface_listed']} "
          f"(ring3 {facts['commands']['ring3_listed']} + builtins {facts['commands']['builtins']})")
print(f"drivers  : legacy={legacy} framework={fw} total={facts['drivers']['total']}")
print(f"services : {svc}")
print(f"version  : makefile={makefile_default} (build {makefile_build}) "
      f"update.json={uj.get('version')} (build {uj.get('build')}) tag={tag or 'none'}")
print(f"categories: {facts['categories']['total']} " +
      " ".join(f"{k}={v}" for k, v in facts["categories"]["per_category"].items()))
print(f"dictionary: EN={dict_en} zh={dict_zh} equal={dict_en == dict_zh}")
print(f"abi      : l1svc_ops={ops_n} syscalls={sys_n}")
print(f"registry reconciliation: {'OK' if not reg_names or reg_names == facts['commands']['names'] else 'DRIFT'}")
print(f"dictionary gate: {'PASS' if facts['cross_checks']['dictionary_gate'] else 'FAIL'}")
if problems:
    print("PROBLEMS:")
    for p in problems:
        print(" -", p)
    sys.exit(2)
print("fact source: ALL CROSS-CHECKS PASS")
PYEOF
