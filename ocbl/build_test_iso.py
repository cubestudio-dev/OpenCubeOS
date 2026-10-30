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
# Usage: build_test_iso.py <out.iso> <stage_dir> <boot_image_file> [efi_binary]
#
#          With the 4th argument the ISO becomes DUAL-FIRMWARE (WP-05
#          spec): the BIOS El Torito entry above plus a UEFI El Torito
#          entry whose image is a 1 MiB ESP FAT holding <efi_binary> as
#          /EFI/BOOT/BOOTX64.EFI.  One ISO, SeaBIOS boots the BIOS
#          chain, OVMF boots the UEFI chain - the same artifact runs
#          under both firmwares (QEMU dual-firmware acceptance).
import subprocess
import sys
import os

def build_esp(stage, efi_binary):
    """Assemble the 1 MiB ESP FAT image inside `stage` (mirrors the
    tools/build_iso.sh G13 determinism: fixed volume serial -N 1,
    zeroed directory dates via tools/fat_zero_dates.py)."""
    esp = os.path.join(stage, 'efi.img')
    if os.path.exists(esp):
        os.unlink(esp)
    with open(esp, 'wb') as f:
        f.write(b'\0' * (1024 * 1024))
    # mformat derives the serial from the clock unless pinned (-N 1)
    for cmd in (
        ['mformat', '-i', esp, '-N', '1', '-v', 'EFI', '::'],
        ['mmd', '-i', esp, '::/EFI'],
        ['mmd', '-i', esp, '::/EFI/BOOT'],
        ['mcopy', '-i', esp, os.path.join(stage, efi_binary),
         '::/EFI/BOOT/BOOTX64.EFI'],
    ):
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0:
            print(f"ocbl: FATAL ESP step failed: {' '.join(cmd)}\n{r.stderr}")
            sys.exit(1)
    z = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                     '..', 'tools', 'fat_zero_dates.py')
    if os.path.exists(z):
        subprocess.run(['python3', z, esp], check=False)
    print(f"[build_test_iso] ESP: BOOTX64.EFI "
          f"({os.path.getsize(os.path.join(stage, efi_binary))} B)")

def main():
    out, stage, boot = sys.argv[1], sys.argv[2], sys.argv[3]
    efi_binary = sys.argv[4] if len(sys.argv) > 4 else None
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
    sz = os.path.getsize(os.path.join(stage, boot))
    sectors = (sz + 511) // 512
    if sectors > 0xFFFF:
        print(f"ocbl: FATAL boot image needs {sectors} sectors (> 0xFFFF)")
        sys.exit(1)
    cmd[cmd.index('0')] = str(sectors)
    print(f"[build_test_iso] boot image {sz} B = {sectors} load sectors")
    if efi_binary:
        build_esp(stage, efi_binary)
        # the UEFI El Torito entry: no-emulation, image = the ESP FAT.
        # NOTE 1: '-e' takes the path RELATIVE TO THE ISO root, so the
        #   ESP must live inside `stage` and be grafted in by xorriso.
        # NOTE 2 (live-caught): mkisofs' multi-boot separator
        #   -eltorito-alt-boot MUST sit between the -b and -e entries;
        #   without it -e REPLACES the BIOS entry in the catalog and
        #   the ISO silently loses its BIOS boot (a single-UEFI ISO).
        # NOTE 3: options must come BEFORE the source directory argument.
        i = cmd.index(stage)
        cmd[i:i] = ['-eltorito-alt-boot', '-e', '/efi.img',
                    '-no-emul-boot']
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
