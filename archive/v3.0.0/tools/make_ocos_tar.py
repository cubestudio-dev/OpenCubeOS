#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - tools/make_ocos_tar.py
# Purpose: build the deterministic OCOS system image tarball (OCOS.TAR)
#          carried by the OCSE install medium. `tar -xf` inside OCSECL
#          unpacks it onto the freshly formatted target partition.
#
# Content (single source of truth for what "install OCOS" means):
#   boot/opencube.elf   - the OCOS kernel (OCBL scans for this name)
#   boot/ocire.elf      - the OCIRE recovery kernel (WP06g3-4.4: the
#                         in-volume recovery entry; install additionally
#                         copies it onto a dedicated recovery partition
#                         when the disk has unallocated tail space)
#   etc/opencube.conf   - config template (config write overrides it)
#
# Determinism: POSIX ustar format, zero mtimes, uid/gid 0, sorted
# entries, headers padded with NULs - two clean builds produce
# byte-identical archives (the G13 rule the whole release chain
# follows).
#
# Usage: make_ocos_tar.py <out.tar> <kernel.elf> <ocire.elf>
import sys

OCT = lambda n, w: ("%0*o" % (w - 1, n)) + "\0"

def ustar_header(name, size):
    h = bytearray(512)
    h[0:len(name)] = name.encode()                      # name
    h[100:108] = OCT(0o644, 8).encode()                 # mode
    h[108:116] = OCT(0, 8).encode()                     # uid
    h[116:124] = OCT(0, 8).encode()                     # gid
    h[124:136] = OCT(size, 12).encode()                 # size
    h[136:148] = OCT(0, 12).encode()                    # mtime = epoch
    h[148:156] = b" " * 8                               # chksum (below)
    h[156:157] = b"0"                                   # typeflag: file
    h[257:263] = b"ustar\0"                             # magic
    h[263:265] = b"00"                                  # version
    h[265:297] = b"root\0".ljust(32, b"\0")             # uname
    h[297:329] = b"root\0".ljust(32, b"\0")             # gname
    chk = sum(h)
    h[148:156] = ("%06o" % chk).encode() + b"\0 "
    return bytes(h)

def dir_header(name):
    h = bytearray(512)
    h[0:len(name)] = name.encode()
    h[100:108] = OCT(0o755, 8).encode()
    h[108:116] = OCT(0, 8).encode()
    h[116:124] = OCT(0, 8).encode()
    h[124:136] = OCT(0, 12).encode()
    h[136:148] = OCT(0, 12).encode()
    h[148:156] = b" " * 8
    h[156:157] = b"5"                                   # typeflag: dir
    h[257:263] = b"ustar\0"
    h[263:265] = b"00"
    h[265:297] = b"root\0".ljust(32, b"\0")
    h[297:329] = b"root\0".ljust(32, b"\0")
    chk = sum(h)
    h[148:156] = ("%06o" % chk).encode() + b"\0 "
    return bytes(h)

def main():
    out, kern = sys.argv[1], sys.argv[2]
    kernel = open(kern, "rb").read()
    # WP06g3-4.4: the recovery kernel rides the system image so the
    # installed volume carries /boot/ocire.elf (an OCBL-scanable
    # recovery entry) even before the dedicated partition step.
    ocire = open(sys.argv[3], "rb").read()
    conf = (
        b"# OpenCubeOS configuration\n"
        b"hostname=opencube\n"
        b"lang=en\n"
        b"boot.slot=A\n"
    )
    # Directory entries first (sorted), then files (sorted).
    entries = [
        (dir_header("boot/"), None),
        (dir_header("etc/"), None),
        (ustar_header("boot/ocire.elf", len(ocire)), ocire),
        (ustar_header("boot/opencube.elf", len(kernel)), kernel),
        (ustar_header("etc/opencube.conf", len(conf)), conf),
    ]
    with open(out, "wb") as f:
        for hdr, data in entries:
            f.write(hdr)
            if data is not None:
                f.write(data)
                pad = (-len(data)) % 512
                f.write(b"\0" * pad)
        f.write(b"\0" * 1024)                           # end-of-archive
    import os
    print(f"[ocos_tar] {out}: {os.path.getsize(out)} B "
          f"(boot/opencube.elf {len(kernel)} B + boot/ocire.elf "
          f"{len(ocire)} B + etc/opencube.conf)")

if __name__ == "__main__":
    main()
