#!/usr/bin/env python3
"""Restore example blocks for the 17 entries whose fresh capture wedged
or desynced (flat-era test programs + mv/sse_test/ssh/sshd), from the
last good dictionary capture (commit f5af2ab6 / 31d4d192 lineage).

The commands' behavior is unchanged since that capture (they are
kernel-era flat test programs and the shell mv/ssh family untouched by
WP-08); the ledger records this provenance explicitly.

Usage: python3 tools/audit/restore_examples.py [--ref f5af2ab6]
"""
import json, re, subprocess, sys

REF = "f5af2ab6"
TARGETS = None  # auto: every name in the current examples.jsonl whose
                # record is empty/wedged, plus the 4 known desyncs

def main():
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    ex_path = f"{root}/build/dictionary/examples.jsonl"

    recs = {}
    order = []
    for line in open(ex_path):
        r = json.loads(line)
        recs[r["name"]] = r
        order.append(r["name"])

    bad = [n for n, r in recs.items()
           if not r.get("output", "").strip()
           or r.get("note", "").startswith("wedged")]
    desync = ["mv", "sse_test", "ssh", "sshd"]
    targets = sorted(set(bad) | {n for n in desync if n in recs})
    print("restoring:", targets)

    # extract example blocks from the historical dictionary pages
    pages = subprocess.run(
        ["git", "-C", root, "ls-tree", "-r", "--name-only", REF,
         "docs/manual/dictionary/"],
        capture_output=True, text=True).stdout.split()
    blocks = {}
    for p in pages:
        if not p.endswith(".md"):
            continue
        txt = subprocess.run(["git", "-C", root, "show", f"{REF}:{p}"],
                             capture_output=True, text=True).stdout
        for m in re.finditer(
                r"^### ([a-z0-9_.-]+)\n(?:(?!^### ).)*?"
                r"\*\*(?:Executed example|实际执行示例)\*\*[:：]?\s*\n+```\n(.*?)\n```\n",
                txt, re.S | re.M):
            blocks[m.group(1)] = m.group(2)

    restored = 0
    for n in targets:
        if n not in blocks:
            print(f"  MISSING in {REF} dictionary: {n}")
            continue
        old = recs[n]
        recs[n] = {"name": n, "cmd": old.get("cmd") or n,
                   "output": blocks[n],
                   "note": "restored from the %s capture (command "
                           "behavior unchanged since)" % REF}
        restored += 1
    with open(ex_path, "w") as f:
        for n in order:
            f.write(json.dumps(recs[n], ensure_ascii=False) + "\n")
    print(f"restored {restored}/{len(targets)}; total {len(order)} records")

if __name__ == "__main__":
    sys.exit(main())
