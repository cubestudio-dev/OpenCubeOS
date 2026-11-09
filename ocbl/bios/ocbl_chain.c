/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP05-ocbl
 * File: ocbl/common/ocbl_chain.c
 * Purpose: BIOS chainload paths for the non-mb2 boot entries.
 *
 *   WINDOWS / LINUX / GENERIC entries carry the volume that holds the
 *   target's loader.  In the BIOS flavor the honest mechanism is a
 *   VBR chainload (read the volume's first sector to 0x7C00, drop to
 *   real mode with the BIOS IVT intact, far jump with DL=drive) - the
 *   same contract GRUB's `chainloader +1` uses.  ESP-only loaders
 *   (BOOTX64.EFI) need a UEFI system to execute; that path arrives
 *   with the UEFI flavor and the BIOS chainload refuses anything that
 *   does not carry a valid 0xAA55 boot sector.
 */
#include "ocbl_chain.h"
#include "ocbl_boot.h"
#include "ocbl_fs.h"
#include "ocbl_disk.h"
#include "ocbl_serial.h"

#define CHAIN_VBR 0x7C00u

int ocbl_chain_vbr(u8 drive, u64 first_lba, u32 ssz)
{
    (void)ssz;
    static u8 vbr[512];
    if (!ocbl_disk_read(drive, first_lba, 1, vbr)) {
        ocbl_serial_puts("[chain] VBR read failed\r\n");
        return -1;
    }
    if ((vbr[510] | (vbr[511] << 8)) != 0xAA55) {
        ocbl_serial_puts("[chain] no boot signature on the target "
                         "volume (ESP-only loader needs the UEFI "
                         "flavor)\r\n");
        return -1;
    }
    /* copy through the low buffer (the read already landed below
     * 1 MiB; bounce to 0x7C00 with a plain loop) */
    volatile u8* dst = (volatile u8*)CHAIN_VBR;
    for (u32 i = 0; i < 512; i++)
        dst[i] = vbr[i];
    ocbl_serial_puts("[chain] jumping to VBR of drive 0x");
    ocbl_serial_hex(drive, 2);
    ocbl_serial_puts(" lba ");
    ocbl_serial_hex(first_lba, 6);
    ocbl_serial_puts("\r\n");
    ocbl_chain16(drive, first_lba);
    return -1;                              /* never returns */
}
