#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""
nak_fix_refs.py: post-move reference fixer for the NAK restructure.

Reads tools/nak_map.tsv and rewrites every old-path / old-basename
reference (Makefile, build scripts, host tests, mk fragments) to the
new NAK names.  Paths first (longest match), then bare basenames with
word boundaries.  os.path.join("src", "L0", ...) spellings are handled
by the src/ path replacement only when written as a single literal;
segmented joins are fixed by hand afterwards.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAP = os.path.join(ROOT, "tools", "nak_map.tsv")

pairs = []
for line in open(MAP):
    old, new = line.rstrip("\n").split("\t")
    pairs.append((old, new))

# path replacements (old repo path -> new repo path), longest first
path_pairs = sorted(pairs, key=lambda t: -len(t[0]))
# basename replacements (with extension), longest first, only renamed
base_pairs = []
for old, new in pairs:
    ob, nb = os.path.basename(old), os.path.basename(new)
    if ob != nb and (ob, nb) not in [(a, b) for a, b in base_pairs]:
        base_pairs.append((ob, nb))
base_pairs.sort(key=lambda t: -len(t[0]))

TARGETS = [os.path.join(ROOT, "Makefile"), os.path.join(ROOT, "build.ps1")]
for base in ("tools", "tests", "website"):
    d = os.path.join(ROOT, base)
    if not os.path.isdir(d):
        continue
    for dirpath, _dn, fns in os.walk(d):
        for fn in fns:
            if fn.endswith((".py", ".sh", ".mk", ".c", ".h", ".ps1")):
                TARGETS.append(os.path.join(dirpath, fn))
# ocbl / ocire / ocse are INDEPENDENT components with their own file
# trees (same basenames, different files) - only their build glue
# (.mk/.py/.sh) may reference the main tree, never their sources.
for base in ("ocbl", "ocire", "ocse"):
    d = os.path.join(ROOT, base)
    if not os.path.isdir(d):
        continue
    for dirpath, _dn, fns in os.walk(d):
        for fn in fns:
            if fn.endswith((".mk", ".py", ".sh")):
                TARGETS.append(os.path.join(dirpath, fn))


def fix(text):
    for old, new in path_pairs:
        if old in text:
            text = text.replace(old, new)
    for old, new in base_pairs:
        text = re.sub(r'(?<![\w./-])' + re.escape(old) + r'(?![\w-])',
                      new.replace("\\", "\\\\"), text)
    return text


changed = 0
for path in TARGETS:
    try:
        src = open(path, encoding="utf-8").read()
    except (UnicodeDecodeError, OSError):
        continue
    out = fix(src)
    if out != src:
        open(path, "w", encoding="utf-8").write(out)
        changed += 1
        print("fixed:", os.path.relpath(path, ROOT))
print("nak_fix_refs: %d files updated" % changed)
