/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-10d-pre (rule-9 system self-sufficiency fix)
 * File: kernel/disk_setup.c
 * Purpose: abdisk / install / grub-install - partition, format, install
 *          and make bootable a disk entirely from the oc> shell.
 *
 * See disk_setup.h for the command overview.  Implementation notes:
 *
 * GRUB BIOS installation (grub_install_device) mirrors what
 * grub-bios-setup does for a plain MBR layout:
 *   - boot.img (512-byte template) is written to LBA 0 with the
 *     core-start LBA patched at offset 0x5C (GRUB_BOOT_MACHINE_KERNEL_
 *     SECTOR, little-endian) and the on-disk disk signature + partition
 *     table area ([0x1B4..0x200)) preserved.
 *   - core.img is written to the free sectors after the MBR (the "MBR
 *     gap": LBA 1 .. first partition start - 1) or, on GPT disks, into
 *     the BIOS boot partition.
 *   - core.img carries an embedded early config that searches every
 *     partition for /boot/grub/grub.cfg and hands over to the normal
 *     menu, so the same loader works for abdisk (slot layout) and
 *     install (single system) disks.
 */
#include "disk_setup.h"
#include "grub_boot_data.h"
#include "multiboot2.h"  /* oc_mb2_get_kernel_self(): boot-attached kernel */
#include "ab_update.h"   /* oc_ab_rescan(): re-scan after abdisk creates the layout */
#include "blk.h"
#include "part.h"
#include "disk_cmds.h"
#include "shell.h"
#include "vfs.h"
#include "console.h"
#include "string.h"

/* GRUB boot.img offsets (grub 2.x include/grub/offsets.h, i386-pc). */
#define GRUB_BOOT_MACHINE_KERNEL_SECTOR 0x5C

/* BIOS boot partition type GUID (mixed-endian as stored in the GPT). */
static const u8 g_bios_boot_guid[16] = {
    0x48, 0x61, 0x68, 0x21, 0x49, 0x64, 0x6F, 0x6E,
    0x74, 0x4E, 0x65, 0x65, 0x64, 0x45, 0x46, 0x49,
};

/* ---- A/B disk grub.cfg (byte-for-byte the grub-ab.cfg layout) ------ */
static const char *g_grub_ab_cfg_lines[] = {
    "",
    "insmod fat",
    "insmod part_msdos",
    "insmod serial",
    "set timeout=5",
    "set default=0",
    "serial --unit=0 --speed=115200",
    "terminal_input console",
    "terminal_output serial console",
    "",
    "set slotid=\"A\"",
    "set slotroot=\"(hd0,msdos2)\"",
    "if [ -f (hd0,msdos1)/boot/next_B ]; then",
    "  if [ -f (hd0,msdos1)/boot/bootfail_B ]; then",
    "    set slotroot=\"(hd0,msdos2)\"",
    "    set slotid=\"A\"",
    "  else",
    "    set slotroot=\"(hd0,msdos3)\"",
    "    set slotid=\"B\"",
    "  fi",
    "else",
    "  if [ -f (hd0,msdos1)/boot/ok_B ]; then",
    "    set slotroot=\"(hd0,msdos3)\"",
    "    set slotid=\"B\"",
    "  fi",
    "fi",
    "echo \"BOOT_SLOT=${slotid}\"",
    "",
    "menuentry \"Open Cube OS (slot ${slotid})\" {",
    "  multiboot2 ${slotroot}/boot/opencube.elf oc.slot=${slotid}",
    "  module2 ${slotroot}/boot/opencube.elf self",
    "  boot",
    "}",
    "menuentry \"Open Cube OS (slot A, force)\" {",
    "  multiboot2 (hd0,msdos2)/boot/opencube.elf oc.slot=A",
    "  module2 (hd0,msdos2)/boot/opencube.elf self",
    "  boot",
    "}",
    "menuentry \"Open Cube OS (slot B, force)\" {",
    "  multiboot2 (hd0,msdos3)/boot/opencube.elf oc.slot=B",
    "  module2 (hd0,msdos3)/boot/opencube.elf self",
    "  boot",
    "}",
    NULL,
};

/* ---- single-system grub.cfg written by `install` ------------------- */
static const char *g_grub_install_cfg_lines[] = {
    "",
    "insmod fat",
    "insmod part_msdos",
    "insmod serial",
    "serial --unit=0 --speed=115200",
    "terminal_input console",
    "terminal_output serial console",
    "set timeout=3",
    "set default=0",
    "menuentry \"Open Cube OS\" {",
    "  multiboot2 /boot/opencube.elf oc.slot=ISO",
    "  module2 /boot/opencube.elf self",
    "  boot",
    "}",
    NULL,
};

static void disk_setup_puts(const char *s) { oc_console_puts(s); }

static void print_lines(const char **lines) {
    for (int i = 0; lines[i]; i++) {
        disk_setup_puts(lines[i]);
        disk_setup_puts("\n");
    }
}

/* ---- small helpers -------------------------------------------------- */

static int dev_from_arg(const char *args) {
    if (!args || !args[0]) return -1;
    return blk_find_device(args);
}

static int part_is_mounted(const char *name) {
    vfs_mount_info_t mounts[8];
    int n = vfs_get_mounts(mounts, 8);
    for (int i = 0; i < n; i++) {
        if (oc_strcmp(mounts[i].device, name) == 0) return 1;
    }
    return 0;
}

static int write_file_bytes(const char *path,
                            const unsigned char *data, unsigned int len) {
    int fd = vfs_open(path, VFS_O_WRONLY | VFS_O_CREAT | VFS_O_TRUNC);
    if (fd < 0) return -1;
    unsigned int off = 0;
    while (off < len) {
        unsigned int chunk = len - off;
        if (chunk > 4096) chunk = 4096;
        int w = vfs_write(fd, data + off, (int)chunk);
        if (w <= 0) { vfs_close(fd); return -1; }
        off += (unsigned int)w;
    }
    vfs_close(fd);
    return 0;
}

/* WP-10d-pre: the kernel ELF to install.  The grub.cfg files attach the
 * booting kernel as a multiboot2 module ("module2 ... self"), so both
 * the ISO and an installed disk boot with the payload attached (the
 * mainstream installer pattern).  Returns 0 on success. */
static int get_kernel_self(const u8 **data, u64 *size) {
    if (oc_mb2_get_kernel_self(data, size) != 0) return -1;
    if (!data || !size || *size == 0) return -1;
    return 0;
}

static void install_write_failed(const char *cmd)
{
    char line[160];
    oc_strcpy(line, cmd);
    oc_strcat(line, ": kernel image not attached by the bootloader "
                    "(grub.cfg must carry: module2 /boot/opencube.elf self)\n");
    disk_setup_puts(line);
}

static int write_cfg_file(const char *path, const char **lines) {
    /* Build the file in one buffer (config files are < 1 KiB). */
    char buf[2048];
    unsigned int off = 0;
    for (int i = 0; lines[i]; i++) {
        const char *l = lines[i];
        unsigned int len = (unsigned int)oc_strlen(l);
        if (off + len + 2 > sizeof(buf)) return -1;
        oc_memcpy(buf + off, l, len);
        off += len;
        buf[off++] = '\n';
    }
    return write_file_bytes(path, (const unsigned char *)buf, off);
}

/* Mount <part-name> at mount_point, mkdir dirs relative to it, run the
 * caller's writes, then umount.  Kept inline below for clarity. */

/* ---- GRUB BIOS installation ----------------------------------------- */

int grub_install_device(int dev_idx) {
    if (dev_idx < 0) return -1;
    if (oc_grub_boot_img_len == 0 || oc_grub_core_img_len == 0) {
        disk_setup_puts("grub-install: boot loader data not embedded "
                        "(build system error)\n");
        return -1;
    }
    blk_device_t *dev = blk_get_device(dev_idx);
    if (!dev || !dev->present) return -1;

    u8 mbr0[512];
    if (blk_read_sectors_raw(dev_idx, 0, 1, mbr0) != 0) return -1;

    part_table_t tbl;
    int have_tbl = (part_parse(dev_idx, &tbl) == 0);

    u64 core_start;
    const char *where;
    u64 core_sectors =
        ((u64)oc_grub_core_img_len + 511) / 512;

    if (have_tbl && tbl.table_type == PART_TYPE_GPT) {
        int found = -1;
        for (int i = 0; i < tbl.count; i++) {
            if (!tbl.parts[i].present) continue;
            if (oc_memcmp(tbl.parts[i].gpt_type_guid, g_bios_boot_guid,
                          16) == 0) { found = i; break; }
        }
        if (found < 0) {
            disk_setup_puts("grub-install: GPT disk has no BIOS boot "
                            "partition (21686148-...-4649); add one or "
                            "use an MBR layout\n");
            return -1;
        }
        if (core_sectors > tbl.parts[found].sectors) {
            disk_setup_puts("grub-install: BIOS boot partition too "
                            "small for core.img\n");
            return -1;
        }
        core_start = tbl.parts[found].start_lba;
        where = "GPT BIOS boot partition";
    } else if (have_tbl && tbl.table_type == PART_TYPE_MBR &&
               tbl.count >= 1 && tbl.parts[0].present) {
        u64 gap = tbl.parts[0].start_lba;
        if (gap < 2 || core_sectors > gap - 1) {
            disk_setup_puts("grub-install: MBR gap too small for "
                            "core.img (first partition starts too "
                            "early)\n");
            return -1;
        }
        core_start = 1;
        where = "MBR gap";
    } else {
        disk_setup_puts("grub-install: no partition table on ");
        disk_setup_puts(dev->name);
        disk_setup_puts(" (run abdisk or install first)\n");
        return -1;
    }

    /* Patch the boot image template. */
    u8 boot[512];
    oc_memcpy(boot, oc_grub_boot_img, 512);
    u32 start_le = (u32)core_start;
    oc_memcpy(boot + GRUB_BOOT_MACHINE_KERNEL_SECTOR, &start_le, 4);
    /* Preserve the on-disk disk signature + partition table area. */
    oc_memcpy(boot + 0x1B4, mbr0 + 0x1B4, 512 - 0x1B4);

    if (blk_write_sectors_raw(dev_idx, 0, 1, boot) != 0) {
        disk_setup_puts("grub-install: failed to write boot sector\n");
        return -1;
    }

    /* Write core.img (4 sectors per chunk). */
    const u8 *src = oc_grub_core_img;
    u64 done = 0;
    while (done < (u64)oc_grub_core_img_len) {
        u32 chunk_sectors = 4;
        u64 chunk_bytes = (u64)chunk_sectors * 512;
        if (done + chunk_bytes > (u64)oc_grub_core_img_len)
            chunk_bytes = (u64)oc_grub_core_img_len - done;
        u8 wbuf[4 * 512];
        oc_memset(wbuf, 0, sizeof(wbuf));
        oc_memcpy(wbuf, src + done, (unsigned int)chunk_bytes);
        if (blk_write_sectors_raw(dev_idx, core_start + done / 512,
                                  (chunk_bytes + 511) / 512, wbuf) != 0) {
            disk_setup_puts("grub-install: failed to write core.img\n");
            return -1;
        }
        done += chunk_bytes;
    }

    char line[128]; char n[24];
    oc_strcpy(line, "grub-install: GRUB installed (");
    oc_strcat(line, where);
    oc_strcat(line, ", core LBA ");
    oc_u64_to_str(core_start, n); oc_strcat(line, n);
    oc_strcat(line, ", ");
    oc_u64_to_str((u64)oc_grub_core_img_len, n); oc_strcat(line, n);
    oc_strcat(line, " bytes) on ");
    oc_strcat(line, dev->name);
    oc_strcat(line, "\n");
    disk_setup_puts(line);
    return 0;
}

/* ---- abdisk ---------------------------------------------------------- */

/* Layout: identical to tools/make_ab_disk.sh and kernel/ab_update.h. */
#define AB_P1_START    2048u
#define AB_P1_SECTORS  (64u * 1024u * 1024u / 512u)
#define AB_SLOT_SECTORS (128u * 1024u * 1024u / 512u)
#define AB_TOTAL_SECTORS \
    (AB_P1_START + AB_P1_SECTORS + 3u * AB_SLOT_SECTORS)

static int cmd_abdisk(const char *args) {
    if (!args || !args[0]) {
        disk_setup_puts("usage: abdisk <device>  (creates A/B update "
                        "disk; DESTROYS existing data)\n");
        return 1;
    }
    const u8 *kself; u64 kself_size;
    if (get_kernel_self(&kself, &kself_size) != 0) {
        install_write_failed("abdisk");
        return 1;
    }
    int dev_idx = dev_from_arg(args);
    if (dev_idx < 0) {
        disk_setup_puts("abdisk: device not found\n");
        return 1;
    }
    blk_device_t *dev = blk_get_device(dev_idx);
    if (dev->sectors < AB_TOTAL_SECTORS) {
        char line[96]; char n[24];
        oc_strcpy(line, "abdisk: device too small (need ");
        oc_u64_to_str(AB_TOTAL_SECTORS, n); oc_strcat(line, n);
        oc_strcat(line, " sectors, have ");
        oc_u64_to_str(dev->sectors, n); oc_strcat(line, n);
        oc_strcat(line, ")\n");
        disk_setup_puts(line);
        return 1;
    }
    /* Refuse when any of this disk's partitions is currently mounted
     * (the old FAT would stay cached in VFS while we reformat below). */
    for (int p = 1; p <= 4; p++) {
        char pname[BLK_DEV_NAME_LEN];
        oc_strncpy(pname, dev->name, BLK_DEV_NAME_LEN - 4);
        pname[BLK_DEV_NAME_LEN - 4] = 0;
        oc_strcat(pname, "p");
        char num[4];
        oc_u64_to_str((u64)p, num);
        oc_strcat(pname, num);
        if (part_is_mounted(pname)) {
            disk_setup_puts("abdisk: partition ");
            disk_setup_puts(pname);
            disk_setup_puts(" is mounted (reboot to release mounts, "
                            "then retry)\n");
            return 1;
        }
    }

    disk_setup_puts("abdisk: WARNING - all data on ");
    disk_setup_puts(dev->name);
    disk_setup_puts(" will be destroyed\n");

    /* 1. MBR partition table: p1 64M boot/flags, p2..p4 128M each. */
    u8 types[4] = { 0x0B, 0x0B, 0x0B, 0x0B };
    u32 starts[4] = {
        AB_P1_START,
        AB_P1_START + AB_P1_SECTORS,
        AB_P1_START + AB_P1_SECTORS + AB_SLOT_SECTORS,
        AB_P1_START + AB_P1_SECTORS + 2 * AB_SLOT_SECTORS,
    };
    u32 sect[4] = { AB_P1_SECTORS, AB_SLOT_SECTORS, AB_SLOT_SECTORS,
                    AB_SLOT_SECTORS };
    if (part_write_mbr_table(dev_idx, types, starts, sect) != 0) {
        disk_setup_puts("abdisk: failed to write partition table\n");
        return 1;
    }
    disk_setup_puts("abdisk: MBR written (p1 64M boot/flags, p2 128M "
                    "slot A, p3 128M slot B, p4 128M data)\n");

    /* 2. Register the partitions as block devices. */
    int pidx[4];
    for (int p = 0; p < 4; p++) {
        pidx[p] = part_register_child(dev->name, dev_idx, p + 1,
                                      starts[p], sect[p]);
        if (pidx[p] < 0) {
            disk_setup_puts("abdisk: failed to register partition\n");
            return 1;
        }
    }

    /* 3. Format every partition FAT32. */
    for (int p = 0; p < 4; p++) {
        if (mkfs_fat32_device(pidx[p]) != 0) {
            disk_setup_puts("abdisk: mkfs.fat32 failed on p");
            char num[4]; char n[4];
            oc_u64_to_str((u64)(p + 1), n);
            oc_strcpy(num, "p"); oc_strcat(num, n);
            disk_setup_puts(num);
            disk_setup_puts("\n");
            return 1;
        }
    }
    disk_setup_puts("abdisk: 4 partitions formatted FAT32\n");

    /* 4. grub.cfg onto the boot/flags partition (p1). */
    {
        char p1name[BLK_DEV_NAME_LEN];
        oc_strncpy(p1name, dev->name, BLK_DEV_NAME_LEN - 4);
        p1name[BLK_DEV_NAME_LEN - 4] = 0;
        oc_strcat(p1name, "p1");
        vfs_mkdir("/abmnt");
        if (vfs_mount("fat32", "/abmnt", p1name) != 0) {
            disk_setup_puts("abdisk: mount p1 failed\n");
            return 1;
        }
        vfs_mkdir("/abmnt/boot");
        vfs_mkdir("/abmnt/boot/grub");
        if (write_cfg_file("/abmnt/boot/grub/grub.cfg",
                           g_grub_ab_cfg_lines) != 0) {
            disk_setup_puts("abdisk: failed to write grub.cfg\n");
            vfs_umount("/abmnt");
            return 1;
        }
        vfs_umount("/abmnt");
    }
    disk_setup_puts("abdisk: p1 /boot/grub/grub.cfg installed\n");

    /* 5. Seed slot A (p2) with the running kernel. */
    {
        char p2name[BLK_DEV_NAME_LEN];
        oc_strncpy(p2name, dev->name, BLK_DEV_NAME_LEN - 4);
        p2name[BLK_DEV_NAME_LEN - 4] = 0;
        oc_strcat(p2name, "p2");
        if (vfs_mount("fat32", "/abmnt", p2name) != 0) {
            disk_setup_puts("abdisk: mount p2 failed\n");
            return 1;
        }
        vfs_mkdir("/abmnt/boot");
        if (write_file_bytes("/abmnt/boot/opencube.elf",
                             kself, (unsigned int)kself_size) != 0) {
            disk_setup_puts("abdisk: failed to write slot A kernel\n");
            vfs_umount("/abmnt");
            return 1;
        }
        vfs_umount("/abmnt");
    }
    {
        char line[96]; char n[24];
        oc_strcpy(line, "abdisk: slot A seeded with the booting kernel (");
        oc_u64_to_str(kself_size, n);
        oc_strcat(line, n);
        oc_strcat(line, " bytes)\n");
        disk_setup_puts(line);
    }

    /* 6. GRUB boot loader. */
    if (grub_install_device(dev_idx) != 0) {
        disk_setup_puts("abdisk: grub-install failed\n");
        return 1;
    }

    disk_setup_puts("abdisk: DONE - reboot with this disk as the first "
                    "hard disk to boot slot A; OTA update is now "
                    "self-hosted\n");

    /* Re-run the A/B framework scan so update --status / update work
     * immediately, without a reboot (the boot-time scan ran before the
     * layout existed).  Partitions are already registered, so this just
     * mounts /ab/boot + /ab/a + /ab/b + /data. */
    oc_ab_rescan();
    return 0;
}

/* ---- install ---------------------------------------------------------- */

static int cmd_install(const char *args) {
    if (!args || !args[0]) {
        disk_setup_puts("usage: install <device>  (install the running "
                        "system; DESTROYS existing data)\n");
        return 1;
    }
    const u8 *kself; u64 kself_size;
    if (get_kernel_self(&kself, &kself_size) != 0) {
        install_write_failed("install");
        return 1;
    }
    int dev_idx = dev_from_arg(args);
    if (dev_idx < 0) {
        disk_setup_puts("install: device not found\n");
        return 1;
    }
    blk_device_t *dev = blk_get_device(dev_idx);
    if (dev->sectors < 2048 + 131072) {
        disk_setup_puts("install: device too small (need >= 64 MiB "
                        "beyond LBA 2048)\n");
        return 1;
    }
    char p1name[BLK_DEV_NAME_LEN];
    oc_strncpy(p1name, dev->name, BLK_DEV_NAME_LEN - 4);
    p1name[BLK_DEV_NAME_LEN - 4] = 0;
    oc_strcat(p1name, "p1");
    if (part_is_mounted(p1name) || part_is_mounted(dev->name)) {
        disk_setup_puts("install: device or p1 is mounted (reboot to "
                        "release mounts, then retry)\n");
        return 1;
    }

    disk_setup_puts("install: WARNING - all data on ");
    disk_setup_puts(dev->name);
    disk_setup_puts(" will be destroyed\n");

    /* 1. MBR: one FAT32 partition from LBA 2048 to the end. */
    u8 types[4] = { 0x0B, 0, 0, 0 };
    u32 starts[4] = { 2048, 0, 0, 0 };
    u32 sect[4] = { (u32)(dev->sectors - 2048), 0, 0, 0 };
    if (part_write_mbr_table(dev_idx, types, starts, sect) != 0) {
        disk_setup_puts("install: failed to write partition table\n");
        return 1;
    }
    int p1 = part_register_child(dev->name, dev_idx, 1, 2048, sect[0]);
    if (p1 < 0) {
        disk_setup_puts("install: failed to register p1\n");
        return 1;
    }
    disk_setup_puts("install: MBR written (p1 FAT32, whole disk)\n");

    /* 2. Format + mount. */
    if (mkfs_fat32_device(p1) != 0) {
        disk_setup_puts("install: mkfs.fat32 failed\n");
        return 1;
    }
    vfs_mkdir("/instmnt");
    if (vfs_mount("fat32", "/instmnt", p1name) != 0) {
        disk_setup_puts("install: mount p1 failed\n");
        return 1;
    }

    /* 3. Copy the running kernel + grub.cfg. */
    vfs_mkdir("/instmnt/boot");
    vfs_mkdir("/instmnt/boot/grub");
    if (write_file_bytes("/instmnt/boot/opencube.elf",
                         kself, (unsigned int)kself_size) != 0) {
        disk_setup_puts("install: failed to write /boot/opencube.elf\n");
        vfs_umount("/instmnt");
        return 1;
    }
    if (write_cfg_file("/instmnt/boot/grub/grub.cfg",
                       g_grub_install_cfg_lines) != 0) {
        disk_setup_puts("install: failed to write grub.cfg\n");
        vfs_umount("/instmnt");
        return 1;
    }
    vfs_umount("/instmnt");
    {
        char line[96]; char n[24];
        oc_strcpy(line, "install: /boot/opencube.elf (");
        oc_u64_to_str(kself_size, n);
        oc_strcat(line, n);
        oc_strcat(line, " bytes) + grub.cfg written\n");
        disk_setup_puts(line);
    }

    /* 4. GRUB boot loader. */
    if (grub_install_device(dev_idx) != 0) {
        disk_setup_puts("install: grub-install failed\n");
        return 1;
    }

    disk_setup_puts("install: DONE - set this disk first in boot order "
                    "and reboot to start Open Cube OS from it\n");
    return 0;
}

/* ---- grub-install ------------------------------------------------------ */

static int cmd_grubinstall(const char *args) {
    if (!args || !args[0]) {
        disk_setup_puts("usage: grub-install <device>  (write the GRUB "
                        "BIOS boot loader)\n");
        return 1;
    }
    int dev_idx = dev_from_arg(args);
    if (dev_idx < 0) {
        disk_setup_puts("grub-install: device not found\n");
        return 1;
    }
    if (grub_install_device(dev_idx) != 0) return 1;
    disk_setup_puts("grub-install: OK\n");
    return 0;
}

/* The grub.cfg sources are printed for transparency (cfg show). */
static int cmd_abcfg_show(const char *args) {
    (void)args;
    disk_setup_puts("# abdisk boot/flags grub.cfg (slot A/B selection):\n");
    print_lines(g_grub_ab_cfg_lines);
    disk_setup_puts("# install single-system grub.cfg:\n");
    print_lines(g_grub_install_cfg_lines);
    return 0;
}

void disk_setup_cmds_register(void) {
    shell_register_command_ex("abdisk", cmd_abdisk, "create a bootable A/B update disk "
                           "(abdisk <dev>; destroys data)", "WP-10c-selfhost");
    shell_register_command_ex("install", cmd_install, "install the running system to a disk "
                           "(install <dev>; destroys data)", "WP-10c-selfhost");
    shell_register_command_ex("grub-install", cmd_grubinstall, "write the GRUB BIOS boot loader "
                           "(grub-install <dev>)", "WP-10c-selfhost");
    shell_register_command_ex("abcfg", cmd_abcfg_show, "show the grub.cfg files abdisk/install write", "WP-10c-selfhost");
}
