#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# WP-10-tui pre-fix batch: rewrite stale "* File:" header comments that
# still point at pre-restructure paths (e.g. kernel/snd.c while the file
# lives at drivers/snd/driver_snd.c). Comment-only change, no semantics.
import os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))  # oc-os/
SKIP_DIRS = {".git", "build", "website", "archive", "iso"}
# tools/embed_grub.py:59 is a template header it WRITES, not its own header.
SKIP_FILES = {os.path.join(ROOT, "tools", "embed_grub.py")}
PAT = re.compile(r"^(\s*\*\s*File:\s*)(\S+)\s*$")
changed, same, nopath = [], 0, []

for dirpath, dirnames, filenames in os.walk(ROOT):
    rel_dir = os.path.relpath(dirpath, ROOT)
    top = rel_dir.split(os.sep)[0]
    if top in SKIP_DIRS:
        dirnames[:] = []
        continue
    for fn in filenames:
        if not fn.endswith((".c", ".h", ".S", ".asm")):
            continue
        full = os.path.join(dirpath, fn)
        if full in SKIP_FILES:
            continue
        rel = os.path.relpath(full, ROOT).replace(os.sep, "/")
        try:
            with open(full, "r", encoding="utf-8", errors="replace") as f:
                lines = f.readlines()
        except OSError:
            continue
        hit = False
        for i, line in enumerate(lines[:15]):
            m = PAT.match(line.rstrip("\n"))
            if not m:
                continue
            hit = True
            old = m.group(2)
            if old != rel:
                lines[i] = m.group(1) + rel + "\n"
                changed.append(f"{rel}: {old} -> {rel}")
            else:
                same += 1
            break
        if not hit:
            nopath.append(rel)
        with open(full, "w", encoding="utf-8") as f:
            f.writelines(lines)

print(f"rewritten: {len(changed)}  already-correct: {same}  no-File-header: {len(nopath)}")
for c in changed[:40]:
    print(" ", c)
if len(changed) > 40:
    print(f"  ... and {len(changed)-40} more")
