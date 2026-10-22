#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 */
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""WP-12: build a synthetic-but-valid ACPI WDAT table for QEMU
-acpitable injection (the E2E gate for the WDAT discovery path).

The table drives the REAL QEMU q35 TCO registers through SystemIO GAS
entries (PMBASE 0x600 -> TCOBASE 0x660), exactly the way an OEM WDAT
would:

  action 4 (SET_COUNTDOWN) instr 1 (WRITE)  GAS 0x672  value 0  mask 0xffffffff
  action 7 (SET_REBOOT)    instr 3 (RMW)    GAS 0xFED1C410 (SystemMemory,
                            GCS) value 0 mask 0x20 (clear NO_REBOOT)
  action 5 (RUNNING)       instr 3 (RMW)    GAS 0x668  value 0 mask 0x1
  action 6 (STOP)          instr 3 (RMW)    GAS 0x668  value 1 mask 0x1

Header layout (36B ACPI header + WDAT body, entries at offset 68).
"""
import struct, sys

def gas(space, width, addr):
    return struct.pack("<BBBBQ", space, width, 0, 0, addr)

def entry(action, instr, space, width, addr, value, mask):
    return struct.pack("<BBH", action, instr, 0) + gas(space, width, addr) + \
           struct.pack("<II", value, mask)

def build():
    entries = b""
    # SET_COUNTDOWN: write the seconds to TCOv2_TMR (+0x12) - value is dynamic
    entries += entry(4, 1, 1, 16, 0x672, 0, 0xFFFFFFFF)
    # SET_REBOOT: RMW GCS (SystemMemory 0xFED1C410), clear bit5
    entries += entry(7, 3, 0, 32, 0xFED1F410, 0, 0x20)  # RCBA 0xFED1C000 + 0x3410
    # RUNNING: RMW TCO1_CNT (+0x08), clear bit0 (TMR_HLT)
    entries += entry(5, 3, 1, 16, 0x668, 0, 0x1)
    # STOP: RMW TCO1_CNT, set bit0
    entries += entry(6, 3, 1, 16, 0x668, 1, 0x1)

    body = struct.pack("<I", 68)          # Header Length (entries offset)
    body += struct.pack("<HHHH", 0, 0, 0x1f, 0)  # PCI seg/bus/dev/fn (LPC)
    body += struct.pack("<III", 600, 0x3F, 2)    # period ms / max / min
    body += struct.pack("<B3B", 0, 0, 0, 0)      # flags / reserved
    body += struct.pack("<I", 4)                  # number of entries
    body += entries

    total = 36 + len(body)
    hdr = b"WDAT"
    hdr += struct.pack("<IBB", total, 1, 0)      # length, revision, checksum(0)
    hdr += b"OCQEMU"                              # OEM id (6)
    hdr += b"OCWDAT01"                            # OEM table id (8)
    hdr += struct.pack("<III", 1, 0, 0)           # OEM rev, creator id, creator rev (12)
    # 36 bytes total header; compute checksum over header+body
    tbl = bytearray(hdr + body)
    s = (-sum(tbl)) & 0xFF
    tbl[9] = s
    return bytes(tbl)

if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "/tmp/wdat.aml"
    data = build()
    open(out, "wb").write(data)
    print(f"wrote {out}: {len(data)} bytes, checksum {data[9]}")
