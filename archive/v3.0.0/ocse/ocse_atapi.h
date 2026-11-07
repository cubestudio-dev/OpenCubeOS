/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP06b-ocsecl
 * File: ocse/ocse_atapi.h
 * Purpose: ATAPI (IDE CD-ROM) driver for the OCSE install medium.
 *
 * The OCSE kernel boots from an El Torito ISO, but the v1.0.0 ATA
 * driver only speaks the hard-disk command set (its probe requires
 * the 0x00/0x00 ATA signature, so ATAPI drives are simply invisible).
 * This driver probes the same four IDE positions for the ATAPI
 * signature (LBA_MID/LBA_HI = 0x14/0xEB), issues IDENTIFY PACKET
 * DEVICE, reads the medium capacity with READ CAPACITY(10) and
 * registers each found drive as a block device.
 *
 * Sector-size contract: the block layer and its write-back cache are
 * hardwired to 512-byte sectors (BUG-0040+0054 gate refuses anything
 * else at registration). ATAPI media carry 2048-byte sectors, so the
 * registered device exposes PSEUDO 512-BYTE SECTORS: pseudo LBA =
 * physical 2048-sector * 4 + quarter. The last physical sector read
 * is kept in a one-sector cache so consecutive pseudo-sector reads of
 * the same physical sector cost one PACKET command, not four.
 *
 * The device is named "cd0", "cd1", ... OCSE disk enumeration
 * (ocse_disk.c) and the storage check (ocse_detect.c) skip these
 * names: an optical drive is the install MEDIUM, never an install
 * TARGET. The block layer itself stays fully generic - dd if=/dev/cd0
 * style raw access keeps working through the same pseudo mapping.
 */
#ifndef OCSE_ATAPI_H
#define OCSE_ATAPI_H

#include "types.h"

/* Probe every IDE position; register ATAPI drives as cd0/cd1/...
 * (512-byte pseudo sectors). Safe to call once after the ATA driver
 * has registered its hard disks. Returns the number of drives. */
int ocse_atapi_init(void);

/* True when the block-device name is one of ours ("cd0".."cd9"). */
int ocse_atapi_is_cd_name(const char* name);

#endif /* OCSE_ATAPI_H */
