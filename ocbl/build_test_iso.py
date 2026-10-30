#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - 3.0.0-WP05-ocbl
# File: ocbl/build_test_iso.py
# Purpose: assemble the standalone OCBL test ISO (BIOS El Torito boot).
#          Deterministic: every date field pinned to the epoch and the
#          GPT GUID pinned, mirroring the G13 six-layer stack used by
#          tools/build_iso.sh, so two clean builds are byte-identical.
#
#          NOTE (B1 live-caught): NO -boot-info-table here.  That mkisofs
#          option zeroes image bytes 8..0x47 to stamp the LBA info block -
#          it exists for loaders that read their own LBA (the ISOLINUX/
#          GRUB pattern, whose images keep their first 72 bytes free).
#          OCBL stage1 uses every byte of the sector and does not need
#          its LBA (the BIOS preloads the whole image), so the option
#          must stay OFF - with it on, stage1's entry code is destroyed
#          and the CPU executes zero-filled memory (found live: a silent
#          hang plus a 5 MB flood of 0x00 on the serial line).
# Usage: build_test_iso.py <out.iso> <stage_dir> <boot_image_file>
import subprocess
import sys

def main():
    out, stage, boot = sys.argv[1], sys.argv[2], sys.argv[3]
    cmd = [
        'xorriso', '-as', 'mkisofs',
        '-graft-points',
        '-o', out,
        '-b', boot,
        '-no-emul-boot',
        '-boot-load-size', '0',      # filled in below from the real size
        '--protective-msdos-label',
        '--modification-date=1970010100000000',
        '--set_all_file_dates', '1970010100000000',
        '--gpt_disk_guid', 'DEADBEEF-0000-0000-0000-00000000C0DE',
        stage,
    ]
    import os
    sz = os.path.getsize(os.path.join(stage, boot))
    sectors = (sz + 511) // 512
    if sectors > 0xFFFF:
        print(f"ocbl: FATAL boot image needs {sectors} sectors (> 0xFFFF)")
        sys.exit(1)
    cmd[cmd.index('0')] = str(sectors)
    print(f"[build_test_iso] boot image {sz} B = {sectors} load sectors")
    r = subprocess.run(cmd, capture_output=True, text=True)
    sys.stdout.write(r.stdout)
    sys.stderr.write(r.stderr)
    if r.returncode != 0:
        print(f"ocbl: xorriso failed rc={r.returncode}")
        sys.exit(1)
    print(f"[build_test_iso] ISO: {out} ({os.path.getsize(out)} B)")
    rep = subprocess.run(['xorriso', '-indev', out, '-report_el_torito', 'plain'],
                         capture_output=True, text=True)
    for line in rep.stdout.splitlines():
        if 'El Torito' in line or 'boot' in line.lower():
            print('[build_test_iso]', line.strip())

if __name__ == '__main__':
    main()
