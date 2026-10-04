/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-10d-pre (rule-9 system self-sufficiency fix)
 * File: kernel/disk_setup.h
 * Purpose: in-system disk setup commands - abdisk / install / grub-install.
 *
 * Rule-9 audit finding: creating an A/B update disk, installing the OS to
 * a disk and installing the GRUB boot loader previously required host
 * tools (tools/make_ab_disk.sh, grub-mkrescue, QEMU drive args).  These
 * commands make the whole flow self-sufficient inside the oc> shell:
 *
 *   abdisk <dev>       Lay out <dev> as a complete, independently
 *                      bootable A/B update disk (MBR p1..p4, FAT32,
 *                      grub.cfg, GRUB boot loader, current kernel seeded
 *                      into slot A).  Same layout as kernel/ota_ab_update.h.
 *   install <dev>      Install the running system to <dev> as a single
 *                      bootable FAT32 system partition (MBR p1 = rest of
 *                      disk, /boot/opencube.elf + grub.cfg + GRUB).
 *   grub-install <dev> Write only the GRUB BIOS boot loader onto an
 *                      already-partitioned disk (MBR gap or GPT BIOS
 *                      boot partition).
 *
 * The GRUB boot images and the kernel's own ELF are embedded at build
 * time (tools/embed_grub.py -> kernel/grub_boot_data.h).
 */
#ifndef OC_DISK_SETUP_H
#define OC_DISK_SETUP_H

/* Write the GRUB BIOS boot loader onto block device dev_idx.
 * MBR disks: boot.img -> LBA 0 (patched with the core start LBA and the
 *            on-disk partition table), core.img -> LBA 1.. (MBR gap).
 * GPT disks: core.img goes into the BIOS boot partition
 *            (21686148-6449-6E6F-744E-656564454649); error if absent.
 * Returns 0 on success. */
int grub_install_device(int dev_idx);

/* Register the abdisk / install / grub-install shell commands. */
void driver_block_disk_setup_register(void);

#endif /* OC_DISK_SETUP_H */
