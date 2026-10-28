#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - 3.0.0-WP03 G5
# File: tools/check_deps.py
# Purpose: the L0 architecture dependency-direction gate.
#
#   src/L0/boot/      the composition root - may depend on everything
#   src/L0/core/      kernel mechanisms - must NOT depend on drivers/
#                     or services/ (nothing above the mechanism layer)
#   src/L0/drivers/   hardware drivers - must NOT depend on services/
#   src/L0/services/  root services - may depend on core/ + drivers/
#
# Includes are plain basenames resolved against the real file
# inventory (the multi -I strategy of the build), so a violation is
# "a core file includes a header whose basename lives under
# drivers/ or services/".  Exit 1 on any violation, with the file,
# the include, and the resolved owner.
import os
import sys

OC_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
L0 = os.path.join(OC_ROOT, "src", "L0")

# Build the basename -> owning folder map (drivers/services/core/boot)
owners = {}
for layer in ("boot", "core", "drivers", "services"):
    d = os.path.join(L0, layer)
    for root, _dirs, files in os.walk(d):
        for f in files:
            if f.endswith((".h", ".c")):
                owners.setdefault(f, []).append(
                    os.path.relpath(os.path.join(root, f), L0))

FORBIDDEN = {
    "core": ("drivers", "services"),
    "drivers": ("services",),
}

def layer_of(rel):
    return rel.split(os.sep)[0] if os.sep in rel else rel

def check():
    bad = []
    checked = 0
    for layer, forbids in FORBIDDEN.items():
        d = os.path.join(L0, layer)
        for root, _dirs, files in os.walk(d):
            for f in files:
                if not f.endswith((".c", ".h")):
                    continue
                path = os.path.join(root, f)
                rel = os.path.relpath(path, L0)
                checked += 1
                with open(path, "r", errors="replace") as fh:
                    for ln, line in enumerate(fh, 1):
                        s = line.strip()
                        if not s.startswith("#include"):
                            continue
                        # only "..." forms (system <...> is none here)
                        if '"' not in s:
                            continue
                        inc = s.split('"')[1] if '"' in s else ""
                        if not inc or "/" in inc:
                            # a path-shaped include: resolve relative to
                            # the file's own folder (host tests style)
                            cand = os.path.normpath(
                                os.path.join(os.path.dirname(path), inc))
                            if os.path.exists(cand):
                                inc_rel = os.path.relpath(cand, L0)
                                inc_layer = layer_of(inc_rel)
                                if inc_layer in forbids:
                                    bad.append((rel, ln, inc, inc_rel))
                            continue
                        # basename include: which layer owns it?
                        hits = owners.get(inc, [])
                        for h in hits:
                            if layer_of(h) in forbids:
                                bad.append((rel, ln, inc, h))
    print(f"check_deps: {checked} files under core/ + drivers/ scanned")
    if bad:
        print(f"check_deps: {len(bad)} DEPENDENCY-DIRECTION VIOLATIONS:")
        for rel, ln, inc, target in bad:
            print(f"  {rel}:{ln}: includes {inc!r} -> {target} "
                  f"(forbidden direction)")
        return 1
    print("check_deps: 0 violations "
          "(core -> nothing above it; drivers -> no services)")
    return 0

if __name__ == "__main__":
    sys.exit(check())
