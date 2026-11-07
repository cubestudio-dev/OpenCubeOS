#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - tools/check_bss.py (3.0.0-WP06c-installer_gui)
# Purpose: build guard - no regular .bss variable may land inside the
#   boot-structure zone. boot.S reserves the page tables (PML4/PDPT/
#   PD) and the boot stack INSIDE .bss; the linker happily places
#   other objects below them, and the WP-06b b4 session live-caught
#   what happens when the variable area grows past __boot_pml4: a
#   64 KiB CLI buffer landed on the live PML4 and the first >448-byte
#   ISO read corrupted the page tables into a triple fault. With the
#   tree now ~140 bytes under the limit, this check makes any future
#   overflow a BUILD ERROR instead of a boot-time heisenbug.
#   WP-06c: the linker gained a dedicated NOLOAD work zone
#   (.ocsework) after .bss for the GUI wizard state - symbols placed
#   between __ocse_work_start and __end_of_kernel are LEGAL there
#   (the loader zeroes it via p_memsz); the forbidden zone is now
#   [__boot_pml4, __ocse_work_start).
# Usage: check_bss.py <elf>
import subprocess
import sys

def main():
    elf = sys.argv[1]
    syms = []
    out = subprocess.run(["nm", "-S", "-n", elf],
                         capture_output=True, text=True).stdout
    boot_lo = work_lo = work_hi = None
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
        if name == "__ocse_work_start":
            work_lo = addr
        if name == "__end_of_kernel":
            work_hi = addr
        syms.append((addr, size, name))
    if boot_lo is None or work_lo is None or work_hi is None:
        print("[check_bss] FATAL: layout markers not found in", elf)
        sys.exit(1)
    bad = []
    for addr, size, name in syms:
        if name.startswith("__boot_") or name == "__end_of_kernel" \
           or name == "__ocse_work_start":
            continue
        # Work-zone placement (.ocsework) is legal by design.
        if addr >= work_lo and addr + size <= work_hi:
            continue
        if addr + size > boot_lo and addr < work_lo:
            bad.append(f"  {name} @0x{addr:x}+0x{size:x}")
    if bad:
        print("[check_bss] FATAL: variables overlap the boot zone "
              f"(0x{boot_lo:x}..0x{work_lo:x}):")
        for b in bad:
            print(b)
        sys.exit(1)
    regular = [(a, s, n) for a, s, n in syms
               if not n.startswith("__boot_")
               and n not in ("__end_of_kernel", "__ocse_work_start")
               and a < boot_lo]
    last = max((a + s for a, s, n in regular), default=0)
    print(f"[check_bss] OK: vars end 0x{last:x}, boot zone starts "
          f"0x{boot_lo:x} ({boot_lo - last} B headroom)")
    print(f"[check_bss] work zone 0x{work_lo:x}..0x{work_hi:x} "
          f"({work_hi - work_lo} B, NOLOAD, loader-zeroed)")

if __name__ == "__main__":
    main()
