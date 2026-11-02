#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - tools/check_bss.py (3.0.0-WP06b-ocsecl)
# Purpose: build guard - no regular .bss variable may land inside the
#   boot-structure zone. boot.S reserves the page tables (PML4/PDPT/
#   PD) and the boot stack INSIDE .bss; the linker happily places
#   other objects below them, and the WP-06b b4 session live-caught
#   what happens when the variable area grows past __boot_pml4: a
#   64 KiB CLI buffer landed on the live PML4 and the first >448-byte
#   ISO read corrupted the page tables into a triple fault. With the
#   tree now ~140 bytes under the limit, this check makes any future
#   overflow a BUILD ERROR instead of a boot-time heisenbug.
# Usage: check_bss.py <elf>
import subprocess
import sys

def main():
    elf = sys.argv[1]
    syms = []
    out = subprocess.run(["nm", "-S", "-n", elf],
                         capture_output=True, text=True).stdout
    boot_lo = boot_hi = None
    for line in out.splitlines():
        parts = line.split()
        # nm -S omits the size column for sizeless symbols:
        #   "addr size type name" (4) or "addr type name" (3)
        if len(parts) == 4 and parts[2] in ("b", "B"):
            addr, size, name = int(parts[0], 16), int(parts[1], 16), parts[3]
        elif len(parts) == 3 and parts[1] in ("b", "B"):
            addr, size, name = int(parts[0], 16), 0, parts[2]
        else:
            continue
        if name == "__boot_pml4":
            boot_lo = addr
        if name == "__end_of_kernel":
            boot_hi = addr
        syms.append((addr, size, name))
    if boot_lo is None or boot_hi is None:
        print("[check_bss] FATAL: boot markers not found in", elf)
        sys.exit(1)
    bad = []
    for addr, size, name in syms:
        if name.startswith("__boot_") or name == "__end_of_kernel":
            continue
        if addr + size > boot_lo and addr < boot_hi:
            bad.append(f"  {name} @0x{addr:x}+0x{size:x}")
    if bad:
        print("[check_bss] FATAL: variables overlap the boot zone "
              f"(0x{boot_lo:x}..0x{boot_hi:x}):")
        for b in bad:
            print(b)
        sys.exit(1)
    last = max((a + s for a, s, n in syms
                if not n.startswith("__boot_") and n != "__end_of_kernel"),
               default=0)
    print(f"[check_bss] OK: vars end 0x{last:x}, boot zone starts "
          f"0x{boot_lo:x} ({boot_lo - last} B headroom)")

if __name__ == "__main__":
    main()
