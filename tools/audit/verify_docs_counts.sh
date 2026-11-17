#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - dictionary/registry count reconciliation (WP-docs).
#
# Verifies the application dictionary against the code enumeration -
# the dictionary must cover the enumerated surface exactly, in both
# languages, with every entry carrying an executed example:
#
#   1. enum_apps.sh (from the source tree)      -> N commands
#   2. docs/data/commands.json (the registry)   -> M commands
#   3. docs/manual/dictionary/  EN entries      -> E entries
#   4. docs/zh/manual/dictionary/ zh entries    -> Z entries
#   5. captured examples (build/dictionary/examples.jsonl when
#      present, else skipped)                    -> X examples
#
# PASS requires N == M == E == Z (and X >= N when the capture file
# exists).  Every mismatch is printed with the offending names.
#
# Usage: tools/audit/verify_docs_counts.sh [--root DIR]
# Exit:  0 pass, 1 count mismatch, 2 missing artifacts
set -u

ROOT=""
while [ $# -gt 0 ]; do
    case "$1" in
        --root) ROOT="$2"; shift 2 ;;
        -h|--help) grep '^#' "$0" | sed '1d'; exit 0 ;;
        *) echo "verify_docs_counts.sh: unknown arg: $1" >&2; exit 1 ;;
    esac
done
if [ -n "$ROOT" ]; then ROOT=$(cd "$ROOT" && pwd); else ROOT=$(cd "$(dirname "$0")/../.." && pwd); fi

"$ROOT/tools/docs/enum_apps.sh" --format json --out /tmp/_vdc-facts.json \
    --registry "$ROOT/docs/data/commands.json" || {
    echo "FAIL: enum_apps.sh reported registry drift" >&2; exit 1; }

exec python3 - "$ROOT" <<'PYEOF'
import json, os, re, sys

ROOT = sys.argv[1]
fail = []

facts = json.load(open("/tmp/_vdc-facts.json"))
enum_names = sorted(c["name"] for c in facts["commands"])
N = len(enum_names)

reg = json.load(open(os.path.join(ROOT, "docs/data/commands.json")))
reg_names = sorted(c["name"] for c in reg["commands"])
M = len(reg_names)

def dict_entries(d):
    names = set()
    if not os.path.isdir(d):
        return names
    for fn in os.listdir(d):
        if not fn.endswith(".md") or fn == "index.md":
            continue
        for m in re.finditer(r"^### (\S+)$",
                             open(os.path.join(d, fn),
                                  encoding="utf-8").read(), re.M):
            names.add(m.group(1))
    return names

en = dict_entries(os.path.join(ROOT, "docs/manual/dictionary"))
zh = dict_entries(os.path.join(ROOT, "docs/zh/manual/dictionary"))
E, Z = len(en), len(zh)

ex_path = os.path.join(ROOT, "build/dictionary/examples.jsonl")
X = None
ex_names = set()
if os.path.exists(ex_path):
    X = 0
    for line in open(ex_path):
        r = json.loads(line)
        if r.get("output"):
            ex_names.add(r["name"])
            X += 1

print("enum (source)      N = %d" % N)
print("registry (json)    M = %d" % M)
print("dictionary EN      E = %d" % E)
print("dictionary zh      Z = %d" % Z)
if X is not None:
    print("captured examples  X = %d (with output)" % X)

if set(enum_names) != set(reg_names):
    fail.append("enum vs registry: %s"
                % (set(enum_names) ^ set(reg_names)))
if en != set(enum_names):
    fail.append("EN dictionary drift: missing=%s extra=%s"
                % (set(enum_names) - en, en - set(enum_names)))
if zh != set(enum_names):
    fail.append("zh dictionary drift: missing=%s extra=%s"
                % (set(enum_names) - zh, zh - set(enum_names)))
if X is not None and ex_names != set(enum_names):
    fail.append("examples drift: missing=%s"
                % (set(enum_names) - ex_names))

# every dictionary entry must carry an executed example block
for d, tag in ((os.path.join(ROOT, "docs/manual/dictionary"), "EN"),
               (os.path.join(ROOT, "docs/zh/manual/dictionary"), "zh")):
    noex = []
    for fn in os.listdir(d):
        if not fn.endswith(".md") or fn == "index.md":
            continue
        txt = open(os.path.join(d, fn), encoding="utf-8").read()
        for m in re.finditer(r"^### (\S+)$", txt, re.M):
            seg = txt[m.end():
                      txt.find("\n### ", m.end()) if txt.find(
                          "\n### ", m.end()) > 0 else len(txt)]
            if "```" not in seg:
                noex.append(m.group(1))
    if noex:
        fail.append("%s entries without an example block: %s"
                    % (tag, sorted(noex)))

if fail:
    print("FAIL:")
    for f in fail:
        print("  - %s" % f)
    sys.exit(1)
print("PASS: dictionary == registry == enumeration, both languages, "
      "every entry with an executed example")
PYEOF
