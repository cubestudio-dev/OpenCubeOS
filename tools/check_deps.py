#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS NAK - tools/check_deps.py: the architecture dependency-
# direction gate for the ocos/ tree.
#
#   ocos/kernel/      kernel mechanisms - must NOT depend on drivers/
#                     or services/ (nothing above the mechanism layer).
#                     EXCEPTION: core_boot_*.c (the composition root)
#                     may depend on every layer (it is the top of the
#                     chain), and the two registration frameworks
#                     include each other's owning headers by design.
#   ocos/drivers/     hardware drivers - must NOT depend on services/
#   ocos/services/    root services - may depend on kernel/ + drivers/
#   ocos/l1apps/      ring-3 apps - must ONLY include headers owned by
#                     ocos/interface/ (the build already enforces this
#                     via the -I set; this gate double-checks it)
#
# Includes are plain basenames resolved against the real file
# inventory (the multi -I strategy of the build), so a violation is
# "a kernel file includes a header whose basename lives under
# ocos/drivers/ or ocos/services/".  Exit 1 on any violation, with the
# file, the include, and the resolved owner.
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
OCOS = os.path.join(ROOT, "ocos")

# Build the basename -> owning layer map
owners = {}
for layer in ("kernel", "drivers", "services", "interface"):
    d = os.path.join(OCOS, layer)
    for root, _dirs, files in os.walk(d):
        for f in files:
            if f.endswith((".h", ".c")):
                owners.setdefault(f, []).append(
                    os.path.relpath(os.path.join(root, f), OCOS))

FORBIDDEN = {
    "kernel": ("drivers", "services"),
    "drivers": ("services",),
    "l1apps": ("kernel", "drivers", "services"),
}


def layer_of(rel):
    return rel.split(os.sep)[0] if os.sep in rel else rel


def is_composition_root(rel):
    base = os.path.basename(rel)
    return rel.startswith("kernel" + os.sep) and base.startswith("core_boot_")


def check():
    bad = []
    checked = 0
    for layer, forbids in FORBIDDEN.items():
        d = os.path.join(OCOS, layer)
        for root, _dirs, files in os.walk(d):
            for f in files:
                if not f.endswith((".c", ".h")):
                    continue
                path = os.path.join(root, f)
                rel = os.path.relpath(path, OCOS)
                if layer == "kernel" and is_composition_root(rel):
                    continue    # the boot composition root sees everything
                checked += 1
                with open(path, "r", errors="replace") as fh:
                    for ln, line in enumerate(fh, 1):
                        s = line.strip()
                        if not s.startswith("#include"):
                            continue
                        if '"' not in s:
                            continue
                        inc = s.split('"')[1]
                        if not inc or "/" in inc:
                            continue
                        hits = owners.get(inc, [])
                        for h in hits:
                            if layer_of(h) in forbids:
                                bad.append((rel, ln, inc, h))
    print(f"check_deps: {checked} files under ocos/{{kernel,drivers,l1apps}} "
          f"scanned")
    if bad:
        print(f"check_deps: {len(bad)} DEPENDENCY-DIRECTION VIOLATIONS:")
        for rel, ln, inc, target in bad:
            print(f"  {rel}:{ln}: includes {inc!r} -> {target} "
                  f"(forbidden direction)")
        return 1
    print("check_deps: 0 violations "
          "(kernel -> nothing above it; drivers -> no services; "
          "l1apps -> interface only)")
    return 0


if __name__ == "__main__":
    sys.exit(check())
