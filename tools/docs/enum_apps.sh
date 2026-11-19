#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS 3.0.0 - application/commands fact enumerator.
#
# Enumerates EVERY user command of the running system straight from
# the source tree (no memory, no sampling - the dictionary truth):
#
#   1. command discovery mirrors tools/gen_userprogs.py exactly:
#        - ocos/l1apps/<name>/  (dir with main.c)     -> command <name>
#        - ocos/l1apps/<f>.c|.asm (legacy flat)       -> command <f>
#          (libfoo.c is the embedded solib source, NOT a program)
#   2. for each l1svc-wrapped app the (cat, op) pair is parsed out of
#      its main.c, then resolved against the numbering contract
#      (ocos/services/sysapi/interface/svc_sysapi_l1svc.h) whose
#      trailing comments carry the usage syntax.
#   3. the owning handler is located in services/<cat>/src/svc_<cat>_l1.c
#      (the `case L1SVC_X: fn = ...` dispatch) and the handler body is
#      scanned for: usage strings, argument keyword literals
#      (streq/strcmp/strncmp against argv/args), put("...") output
#      strings and `return N` codes.
#   4. self-contained apps (textutils, editors, net tools, ...) are
#      scanned with the same extractor over their own main.c.
#   5. the 8 ush built-ins are enumerated from l1apps/ush/main.c.
#   6. the 8 mega-apps get their op tables (g_ops[] / g_progs[])
#      enumerated as separate per-op rows.
#   7. the result is cross-checked against docs/data/commands.json
#      (the published registry) and any drift is reported.
#
# Output (default stdout):
#   --format tsv   one row per command (dictionary-friendly flat view)
#   --format json  machine-readable full fact list (default)
#
# Usage:
#   tools/docs/enum_apps.sh [--root DIR] [--out FILE] [--format tsv|json]
#                           [--registry FILE]
#
# Exit status: 0 ok; 1 usage error; 2 drift between source and registry.
set -u

ROOT=""
OUT=""
FORMAT="json"
REGISTRY=""
while [ $# -gt 0 ]; do
    case "$1" in
        --root)    ROOT="$2"; shift 2 ;;
        --out)     OUT="$2"; shift 2 ;;
        --format)  FORMAT="$2"; shift 2 ;;
        --registry) REGISTRY="$2"; shift 2 ;;
        -h|--help) grep '^#' "$0" | sed '1d'; exit 0 ;;
        *) echo "enum_apps.sh: unknown arg: $1" >&2; exit 1 ;;
    esac
done

if [ -n "$ROOT" ]; then ROOT=$(cd "$ROOT" && pwd); else ROOT=$(cd "$(dirname "$0")/../.." && pwd); fi
if [ "$FORMAT" != "tsv" ] && [ "$FORMAT" != "json" ]; then
    echo "enum_apps.sh: --format must be tsv or json" >&2; exit 1
fi

# One code path: redirect to $OUT when given, else to stdout.
exec python3 - "$ROOT" "$FORMAT" "$REGISTRY" > "${OUT:-/dev/stdout}" <<'PYEOF'
import json, os, re, sys

ROOT = sys.argv[1]
FORMAT = sys.argv[2]
REGISTRY = sys.argv[3] if len(sys.argv) > 3 else ""

L1 = os.path.join(ROOT, "ocos", "l1apps")
SVC = os.path.join(ROOT, "ocos", "services")
CONTRACT = os.path.join(SVC, "sysapi", "interface", "svc_sysapi_l1svc.h")

MEGA = ["update", "disk-tool", "net-tool", "sound-tool",
        "files", "sysdiag", "hwinfo", "power"]

_read_cache = {}

def read(p):
    if p not in _read_cache:
        try:
            with open(p, "r", encoding="utf-8", errors="replace") as f:
                _read_cache[p] = f.read()
        except OSError:
            _read_cache[p] = ""
    return _read_cache[p]

def rd(p, base=None):
    """path relative to ROOT (kept relative in the fact list)."""
    if base is None:
        base = ROOT
    return os.path.relpath(p, base)

# ---------------------------------------------------------------- contract
# Parse the numbering contract: #define L1SVC_<X> <n> /* usage comment */
contract = {}
cat_names = {}   # L1SVC_CAT_<NAME> -> folder name
contract_txt = read(CONTRACT)
for m in re.finditer(r'^#define\s+(L1SVC_\w+)\s+(\d+)\s*/\*\s*(.*?)\s*\*/',
                     contract_txt, re.M):
    sym, num, comment = m.group(1), int(m.group(2)), m.group(3)
    contract[sym] = (num, comment)
    if sym.startswith("L1SVC_CAT_"):
        cm = re.match(r'services/(\w+)', comment)
        if cm:
            cat_names[sym] = cm.group(1)

# service -> candidate dispatcher sources (ALL .c in services/<cat>/src;
# the *_l1.c naming is the convention but e.g. services/l1 keeps its
# dispatch in svc_l1_core.c)
l1_dispatchers = {}
svc_srcs = []
for catdir in sorted(os.listdir(SVC)):
    d = os.path.join(SVC, catdir, "src")
    if not os.path.isdir(d):
        continue
    for f in sorted(os.listdir(d)):
        if f.endswith(".c"):
            svc_srcs.append(os.path.join(d, f))
            l1_dispatchers.setdefault(catdir, []).append(
                os.path.join(d, f))

# case-map built procedurally (linear, no backtracking blowups):
# for every `case L1SVC_X:` label, take the text up to the next
# case/default/} boundary and inside that window recognize the three
# dispatcher styles:
#   fn = handler;          (assignment style)
#   return handler(...     (direct-return style)
#   rc = handler(...       (assign-call style)
case_map = {}   # sym -> (handler_fn, file)
RE_CASE = re.compile(r'case\s+(L1SVC_\w+)\s*:')
RE_IFOP = re.compile(r'if\s*\(\s*op\s*==\s*(L1SVC_\w+)\s*\)')
RE_BOUND = re.compile(r'\bcase\b|\bdefault\b|}')
for catdir, files in l1_dispatchers.items():
    for fp in files:
        txt = read(fp)
        for m in list(RE_CASE.finditer(txt)) + list(RE_IFOP.finditer(txt)):
            sym = m.group(1)
            if sym in case_map:
                continue
            b = RE_BOUND.search(txt, m.end())
            if not b:
                b = re.compile(r';').search(txt, m.end())
            win = txt[m.end():b.start() if b else m.end() + 400]
            hm = (re.search(r'\breturn\s+(\w+)\s*\(', win)
                  or re.search(r'\bfn\s*=\s*([A-Za-z_]\w*)\s*;', win)
                  or re.search(r'\b\w+\s*=\s*([A-Za-z_]\w*)\s*\(', win))
            if hm:
                case_map[sym] = (hm.group(1), rd(fp))

# handler function bodies across all service sources
handler_bodies = {}   # (fn) -> body text  (first definition wins)
for fp in svc_srcs:
    txt = read(fp)
    starts = [m.start() for m in
              re.finditer(r'^static\s+(?:int|void)\s+(\w+)\s*\(', txt, re.M)]
    for i, st in enumerate(starts):
        fn = re.match(r'^static\s+(?:int|void)\s+(\w+)\s*\(',
                      txt[st:st + 120]).group(1)
        if fn in handler_bodies:
            continue
        end = starts[i + 1] if i + 1 < len(starts) else len(txt)
        handler_bodies[fn] = txt[st:end]

# ---------------------------------------------------------------- extractor
RE_STREQ = re.compile(
    r'(?:streq|streq_|strcmp|strncmp|strcmp_|word_is)\s*\(\s*'
    r'(?:&?\w+|argv\[\d+\]|argv\[0\])[^,]*,\s*"([^"]*)"')
RE_PUT = re.compile(r'(?:put|puts|puts_|put_)\s*\(\s*"((?:[^"\\]|\\.)*)"')
RE_RET = re.compile(r'return\s+(-?\d+)\s*;')
# usage strings only inside C string literals ("usage: ...")
RE_USAGE = re.compile(r'"(usage:[^"]{0,150})')

def extract_facts(body):
    usage = sorted(set(RE_USAGE.findall(body)))
    lits = []
    for m in RE_STREQ.finditer(body):
        lits.append(m.group(1))
    lits = sorted(set(lits))
    outs = []
    for m in RE_PUT.finditer(body):
        s = m.group(1)
        if len(s) > 1 and not s.startswith("%"):
            outs.append(s)
        if len(outs) >= 60:
            break
    outs = sorted(set(outs))
    rets = sorted({int(x) for x in RE_RET.findall(body)})
    return usage, lits, outs, rets

def i18n_hint(name):
    """The _hint field of the app's i18n/en.json (authoritative
    one-line description for the self-contained app family)."""
    p = os.path.join(L1, name, "i18n", "en.json")
    if not os.path.exists(p):
        return ""
    try:
        j = json.loads(read(p))
        return j.get("_hint", "")
    except Exception:
        return ""

def hidden_literals(lits, usage_texts):
    """argv keyword literals never mentioned in any usage text."""
    blob = " ".join(usage_texts)
    return [l for l in lits
            if l not in blob and (l not in blob.replace(" ", ""))]

# ---------------------------------------------------------------- discovery
commands = {}

def add(name, kind, **kw):
    e = {"name": name, "kind": kind}
    e.update(kw)
    commands[name] = e

flat_skip = {"libfoo.c"}   # solib source, not a program
for entry in sorted(os.listdir(L1)):
    p = os.path.join(L1, entry)
    if os.path.isdir(p):
        if entry == "_i18n":
            continue
        if not (os.path.exists(os.path.join(p, "main.c"))
                or os.path.exists(os.path.join(p, "main.asm"))):
            continue
        add(entry, "app-dir", src=rd(os.path.join(p, "main.c")),
            srcdir=rd(p))
    elif entry.endswith(".c") and entry not in flat_skip:
        add(entry[:-2], "flat", src=rd(p), srcdir=None)
    elif entry.endswith(".asm"):
        txt = read(p)
        am = (re.search(r';\s*(Open Cube OS[^\n]{5,120})', txt)
              or re.search(r';\s*((?:WP-[\w-]+|Open Cube OS)[^\n]{5,120})',
                           txt))
        add(entry[:-4], "flat-asm", src=rd(p), srcdir=None,
            usage="", src_header=(am.group(1).strip() if am else ""),
            usage_strings=[], arg_literals=[], output_strings=[],
            return_codes=[], hidden_args=[], i18n_hint="")

# ---------------------------------------------------------------- l1svc wrap
RE_L1SVC_MAIN = re.compile(
    r'l1svc_main\s*\(\s*argc\s*,\s*argv\s*,\s*(L1SVC_CAT_\w+)\s*,\s*'
    r'(L1SVC_\w+)\s*\)')

for name, e in commands.items():
    if e["kind"] not in ("app-dir", "flat"):
        continue
    txt = read(os.path.join(ROOT, e["src"]))
    m = RE_L1SVC_MAIN.search(txt)
    if not m:
        continue
    cat_sym, op_sym = m.group(1), m.group(2)
    cat_folder = cat_names.get(cat_sym, cat_sym.replace("L1SVC_CAT_", "")
                               .lower())
    op_num, usage = contract.get(op_sym, (None, ""))
    cat_num, _ = contract.get(cat_sym, (None, ""))
    e.update(l1svc=True, cat_sym=cat_sym, op_def=op_sym,
             category=cat_folder, cat_num=cat_num, op_num=op_num,
             usage=usage)
    # handler facts
    if op_sym in case_map:
        fn, fp = case_map[op_sym]
        body = handler_bodies.get(fn, "")
        u, lits, outs, rets = extract_facts(body)
        e.update(handler=fn, handler_file=fp,
                 usage_strings=u, arg_literals=lits,
                 output_strings=outs, return_codes=rets,
                 hidden_args=hidden_literals(lits, u + [usage]))
    else:
        e.update(handler=None, usage_strings=[], arg_literals=[],
                 output_strings=[], return_codes=[],
                 hidden_args=[])
    e["i18n_hint"] = i18n_hint(name)
    # description from the file header comment (bounded head only)
    hm = re.search(r'L1 app "[^"]*"[.\n]\s*/\*[ ^.]{0,10}\*/?'
                   r'|Open Cube OS[^\n]{0,10}L1 app',
                   txt[:2000])
    cm = re.search(r'\*\s*([^\n*]{20,180})\.', txt[:2000])
    e["src_header"] = (cm.group(1).strip() if cm else "")

# self-contained apps: same extractor over their own main.c (+ folder .c)
for name, e in commands.items():
    if e.get("l1svc"):
        continue
    if e["kind"] not in ("app-dir", "flat"):
        continue
    srcs = []
    if e.get("srcdir"):
        d = os.path.join(ROOT, e["srcdir"])
        for f in sorted(os.listdir(d)):
            if f.endswith(".c"):
                srcs.append(os.path.join(d, f))
    else:
        srcs.append(os.path.join(ROOT, e["src"]))
    body = "\n".join(read(s) for s in srcs)
    u, lits, outs, rets = extract_facts(body)
    e.update(usage_strings=u, arg_literals=lits, output_strings=outs,
             return_codes=rets,
             hidden_args=hidden_literals(lits, u))
    e["i18n_hint"] = i18n_hint(name)
    if "usage" not in e:
        e["usage"] = (u[0][:120] if u else "")
    cm = re.search(r'\*\s*([^\n*]{20,180})\.', body[:2000])
    e.setdefault("src_header", cm.group(1).strip() if cm else "")

# ---------------------------------------------------------------- builtins
ush_c = os.path.join(L1, "ush", "main.c")
ush_txt = read(ush_c)
BUILTINS = ["alias", "cd", "env", "exit", "export",
            "history", "set", "unalias"]
# WP-presentation-consistency-audit: the shell job-control builtins
# (jobs/fg/bg, handled on the expanded-arg branch in ush main.c and
# listed by `help -a`) were missing from this enumeration - help -a
# lists 12 builtin rows while this list carried 8.
JOB_BUILTINS = ["jobs", "fg", "bg"]
for b in BUILTINS + JOB_BUILTINS:
    # locate the builtin branch: streq(argv[0], "b") or the
    # streq(expanded, "b") job-control form - and slice to the next
    # builtin branch (or 6000 chars) - branch-local facts only
    m = re.search(r'streq_?\s*\(\s*(?:argv\[0\]|expanded)\s*,\s*"%s"\s*\)'
                  % b, ush_txt)
    seg = ""
    if m:
        nxt = re.search(r'streq_?\s*\(\s*(?:argv\[0\]|expanded)\s*,\s*"(?:%s)"\s*\)'
                        % "|".join(BUILTINS + JOB_BUILTINS), ush_txt[m.end():])
        seg = ush_txt[m.start():
                      m.end() + (nxt.start() if nxt else 6000)]
    u, lits, outs, rets = extract_facts(seg)
    add(b, "builtin", src=rd(ush_c), l1svc=False, usage_strings=u,
        arg_literals=lits, output_strings=outs, return_codes=rets,
        hidden_args=hidden_literals(lits, u), usage=(u[0][:120] if u else ""),
        src_header="ush built-in command")

# ---------------------------------------------------------------- mega-apps
mega_ops = {}
for m in MEGA:
    e = commands.get(m)
    if not e:
        continue
    e["mega"] = True
    ops = []
    txt = read(os.path.join(ROOT, e["src"]))
    for om in re.finditer(
            r'\{\s*"([^"]+)"\s*,\s*(L1SVC_CAT_\w+)\s*,\s*(L1SVC_\w+)\s*\}',
            txt):
        oname, csym, osym = om.group(1), om.group(2), om.group(3)
        op_num, usage = contract.get(osym, (None, ""))
        ops.append({"op": oname, "cat": cat_names.get(csym, csym),
                    "op_def": osym, "usage": usage})
    for pm in re.finditer(r'g_progs\[\]\s*=\s*\{(.*?)\}', txt, re.S):
        for s in re.finditer(r'"([^"]+)"', pm.group(1)):
            ops.append({"op": s.group(1), "cat": None, "op_def": None,
                        "usage": "execve passthrough"})
    e["ops"] = ops
    e["ops_n"] = len(ops)
    mega_ops[m] = ops

# ---------------------------------------------------------------- registry
drift = None
if REGISTRY and os.path.exists(REGISTRY):
    reg = json.load(open(REGISTRY))
    reg_names = {c["name"] for c in reg["commands"]}
    src_names = set(commands)
    drift = {"registry_only": sorted(reg_names - src_names),
             "source_only": sorted(src_names - reg_names),
             "registry_count": len(reg_names),
             "source_count": len(src_names)}

# ---------------------------------------------------------------- output
ordered = [commands[k] for k in sorted(commands)]
result = {
    "tool": "tools/docs/enum_apps.sh",
    "root": ROOT,
    "counts": {
        "commands": len(ordered),
        "app_dir": sum(1 for c in ordered if c["kind"] == "app-dir"),
        "flat": sum(1 for c in ordered if c["kind"].startswith("flat")),
        "builtins": sum(1 for c in ordered if c["kind"] == "builtin"),
        "l1svc_wrapped": sum(1 for c in ordered if c.get("l1svc")),
        "mega_apps": sum(1 for c in ordered if c.get("mega")),
        "mega_ops_total": sum(len(v) for v in mega_ops.values()),
    },
    "mega_ops": mega_ops,
    "commands": ordered,
}
if drift is not None:
    result["registry_drift"] = drift

if FORMAT == "json":
    out = json.dumps(result, ensure_ascii=False, indent=1)
else:
    cols = ["name", "kind", "category", "op_sym", "usage", "handler",
            "arg_literals", "hidden_args", "usage_strings",
            "return_codes", "mega", "ops_n"]
    lines = ["\t".join(cols)]
    for c in ordered:
        row = []
        for k in cols:
            v = c.get(k, "")
            if isinstance(v, list):
                v = ";".join(str(x) for x in v)
            row.append(str(v).replace("\t", " ").replace("\n", " "))
        lines.append("\t".join(row))
    out = "\n".join(lines) + "\n"

if drift is not None and (drift["registry_only"] or drift["source_only"]):
    sys.stderr.write(
        "enum_apps.sh: REGISTRY DRIFT registry_only=%s source_only=%s\n"
        % (drift["registry_only"], drift["source_only"]))
    sys.exit(2)

sys.stdout.write(out)
PYEOF
