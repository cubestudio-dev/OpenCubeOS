/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-10a
 * File: kernel/disk_test_cmds.c
 * Purpose: WP-10a storage-driver test commands (see header for the list).
 *
 * Design rules:
 *  - No fake data: every round-trip writes a real pattern through the
 *    driver under test, reads it back through the same driver, and
 *    verifies byte-for-byte.
 *  - No silent damage: pattern writes target the tail of the disk
 *    (sectors [size-16, size)), and the original content is saved and
 *    restored afterwards.  partition_test saves/restores LBA 0..33.
 *  - fs_mount_test requires an explicit device argument (never formats
 *    anything implicitly, protects the /etc volume on hda).
 *  - real_hw_test reports NOT RUN when running in a VM - honest output
 *    instead of a fake PASS.
 */
#include "disk_test_cmds.h"
#include "shell.h"
#include "blk.h"
#include "blk_cache.h"
#include "part.h"
#include "ahci.h"
#include "nvme.h"
#include "ata_dma.h"
#include "ata.h"
#include "nic.h"       /* WP-10b: real_hw_test NIC section */
#include "snd.h"       /* WP-10c: real_hw_test sound section */
#include "usb.h"        /* WP-10d: real_hw_test USB section */
#include "usb_hid.h"
#include "usb_msc.h"
#include "usb_serial.h"
#include "usb_audio.h"
#include "disk_cmds.h"
#include "fat32.h"
#include "vfs.h"
#include "timer.h"
#include "console.h"
#include "string.h"
#include "heap.h"

/* ---- small report helpers ---- */

static void t_pass(const char *test, const char *what) {
    char line[96];
    oc_strcpy(line, "["); oc_strcat(line, test); oc_strcat(line, "] ");
    oc_strcat(line, what); oc_strcat(line, " => PASS\n");
    oc_console_puts(line);
}

static void t_fail(const char *test, const char *what, const char *actual) {
    char line[140];
    oc_strcpy(line, "["); oc_strcat(line, test); oc_strcat(line, "] ");
    oc_strcat(line, what); oc_strcat(line, " => FAIL (");
    oc_strcat(line, actual); oc_strcat(line, ")\n");
    oc_console_puts(line);
}

/* Read/write round-trip through the RAW (uncached) blk path so the
 * driver under test really moves data.  Sectors [lba, lba+count) must be
 * safe to clobber; the original content is restored.  Returns 0 PASS,
 * -1 FAIL. */
static int rw_roundtrip(const char *test, int dev_idx, u64 lba, u32 count) {
    /* Heap buffers: 3 x 8 KiB is too large for the kernel stack. */
    u8 *orig = (u8 *)kmalloc((u64)count * 512);
    u8 *back = (u8 *)kmalloc((u64)count * 512);
    u8 *pattern = (u8 *)kmalloc((u64)count * 512);
    if (!orig || !back || !pattern) {
        if (orig) kfree(orig);
        if (back) kfree(back);
        if (pattern) kfree(pattern);
        t_fail(test, "alloc buffers", "out of memory");
        return -1;
    }

    blk_device_t *d = blk_get_device(dev_idx);
    if (!d) {
        kfree(orig); kfree(back); kfree(pattern);
        t_fail(test, "get device", "not found"); return -1;
    }

    int rc = blk_read_sectors_raw(dev_idx, lba, count, orig);
    if (rc != 0) {
        char diag[120]; char n[24];
        oc_strcpy(diag, "read original sectors rc=");
        oc_u64_to_str((u64)(rc < 0 ? -rc : rc), n); oc_strcat(diag, n);
        oc_strcat(diag, rc < 0 ? "(-)" : "");
        kfree(orig); kfree(back); kfree(pattern);
        t_fail(test, diag, "io error"); return -1;
    }
    /* Deterministic pattern: byte i at sector s = 0xA5 ^ (s*7 + i). */
    for (u32 s = 0; s < count; s++)
        for (int i = 0; i < 512; i++)
            pattern[s * 512 + i] = (u8)(0xA5u ^ ((s * 7u + (u32)i) & 0xFFu));

    rc = blk_write_sectors_raw(dev_idx, lba, count, pattern);
    if (rc != 0) {
        char diag[120]; char n[24];
        oc_strcpy(diag, "write pattern rc=");
        oc_u64_to_str((u64)(rc < 0 ? -rc : rc), n); oc_strcat(diag, n);
        kfree(orig); kfree(back); kfree(pattern);
        t_fail(test, diag, "io error"); return -1;
    }
    rc = blk_read_sectors_raw(dev_idx, lba, count, back);
    if (rc != 0) {
        blk_write_sectors_raw(dev_idx, lba, count, orig);   /* restore */
        char diag[120]; char n[24];
        oc_strcpy(diag, "read back pattern rc=");
        oc_u64_to_str((u64)(rc < 0 ? -rc : rc), n); oc_strcat(diag, n);
        kfree(orig); kfree(back); kfree(pattern);
        t_fail(test, diag, "io error"); return -1;
    }
    int ok = (oc_memcmp(back, pattern, (u64)count * 512) == 0);
    /* Restore original content regardless. */
    blk_write_sectors_raw(dev_idx, lba, count, orig);
    kfree(orig); kfree(back); kfree(pattern);
    if (!ok) { t_fail(test, "data compare", "read-back differs"); return -1; }

    char what[80]; char n[24];
    oc_strcpy(what, "rw roundtrip lba=");
    oc_u64_to_str(lba, n); oc_strcat(what, n);
    oc_strcat(what, " count=");
    oc_u64_to_str(count, n); oc_strcat(what, n);
    t_pass(test, what);
    return 0;
}

/* First device of a given blk type, or -1. */
static int first_dev_of_type(blk_type_t type) {
    for (int i = 0; i < BLK_MAX_DEVICES; i++) {
        blk_device_t *d = blk_get_device(i);
        if (d && d->type == type) return i;
    }
    return -1;
}

/* Tail LBA that is safe to clobber on a device. */
static u64 safe_test_lba(blk_device_t *d) {
    return (d->sectors > 32) ? d->sectors - 16 : d->sectors;
}

/* ---- ahci_test ---- */

static int cmd_ahci_test(const char *args) {
    (void)args;
    int idx = first_dev_of_type(BLK_TYPE_AHCI);
    if (idx < 0) {
        t_fail("ahci_test", "AHCI drive presence", "no AHCI drive registered");
        return 1;
    }
    blk_device_t *d = blk_get_device(idx);
    char line[120]; char n[24];
    oc_strcpy(line, "[ahci_test] input: AHCI drive ");
    oc_strcat(line, d->name);
    oc_strcat(line, " sectors=");
    oc_u64_to_str(d->sectors, n); oc_strcat(line, n);
    oc_strcat(line, "\n");
    oc_console_puts(line);
    ahci_print_state();

    int fails = 0;
    if (d->sectors == 0) {
        t_fail("ahci_test", "capacity", "0 sectors"); fails++;
    } else {
        t_pass("ahci_test", "controller init + port discovery + capacity");
    }
    if (rw_roundtrip("ahci_test", idx, safe_test_lba(d), 8) != 0) fails++;
    return fails ? 1 : 0;
}

/* ---- nvme_test ---- */

static int cmd_nvme_test(const char *args) {
    (void)args;
    int idx = first_dev_of_type(BLK_TYPE_NVME);
    if (idx < 0) {
        t_fail("nvme_test", "NVMe drive presence", "no NVMe drive registered");
        return 1;
    }
    blk_device_t *d = blk_get_device(idx);
    nvme_print_state();

    int fails = 0;
    if (d->sectors == 0) {
        t_fail("nvme_test", "capacity", "0 sectors"); fails++;
    } else {
        t_pass("nvme_test", "controller init + Identify");
    }
    if (nvme_num_io_queues() >= 1) {
        t_pass("nvme_test", "I/O queue pairs live");
    } else {
        t_fail("nvme_test", "I/O queue pairs", "0"); fails++;
    }
    if (rw_roundtrip("nvme_test", idx, safe_test_lba(d), 8) != 0) fails++;
    return fails ? 1 : 0;
}

/* ---- ata_dma_test ---- */

static int cmd_ata_dma_test(const char *args) {
    (void)args;
    int idx = first_dev_of_type(BLK_TYPE_ATA);
    if (idx < 0) {
        t_fail("ata_dma_test", "ATA drive presence", "no ATA drive registered");
        return 1;
    }
    blk_device_t *d = blk_get_device(idx);
    ata_dma_print_state();

    int fails = 0;
    int dma_drive = -1;
    for (int i = 0; i < 4; i++) if (ata_dma_available(i)) dma_drive = i;
    if (dma_drive >= 0) {
        t_pass("ata_dma_test", "BMDMA controller + PRDT programmed");
    } else {
        t_fail("ata_dma_test", "BMDMA availability", "no drive on DMA path");
        fails++;
    }
    if (rw_roundtrip("ata_dma_test", idx, safe_test_lba(d), 8) != 0) fails++;

    /* DMA vs PIO timing on the same drive (256 sectors, tail area). */
    if (dma_drive >= 0 && d->sectors > 256 + 16) {
        u64 lba = d->sectors - 256 - 16;
        u8 *buf = (u8 *)kmalloc(256 * 512);
        if (buf) {
            u64 t0, t1, t2, t3;
            t0 = oc_timer_ticks();
            (void)ata_dma_read_sectors(dma_drive, lba, 256, buf);
            t1 = oc_timer_ticks();
            (void)ata_read_sectors(dma_drive, lba, 256, buf);
            t2 = oc_timer_ticks();
            t3 = t2 - t1;   /* PIO ticks */
            t1 = t1 - t0;   /* DMA ticks */
            char line[120]; char n[24];
            oc_strcpy(line, "[ata_dma_test] input: read 256 sectors, expect DMA faster/equal\n");
            oc_console_puts(line);
            oc_strcpy(line, "[ata_dma_test] actual: DMA=");
            oc_u64_to_str(t1, n); oc_strcat(line, n);
            oc_strcat(line, " ticks PIO=");
            oc_u64_to_str(t3, n); oc_strcat(line, n);
            oc_strcat(line, " ticks => ");
            oc_strcat(line, (t1 <= t3) ? "PASS\n" : "FAIL (DMA slower)\n");
            oc_console_puts(line);
            if (t1 > t3) fails++;
            kfree(buf);
        }
    }
    return fails ? 1 : 0;
}

/* ---- virtio_blk_test ---- */

static int cmd_virtio_blk_test(const char *args) {
    (void)args;
    int idx = first_dev_of_type(BLK_TYPE_VIRTIO);
    if (idx < 0) {
        t_fail("virtio_blk_test", "virtio drive presence", "no virtio-blk device");
        return 1;
    }
    blk_device_t *d = blk_get_device(idx);
    char line[120]; char n[24];
    oc_strcpy(line, "[virtio_blk_test] input: device ");
    oc_strcat(line, d->name);
    oc_strcat(line, " sectors=");
    oc_u64_to_str(d->sectors, n); oc_strcat(line, n);
    oc_strcat(line, "\n");
    oc_console_puts(line);

    int fails = 0;
    /* WP-10a FIX verification: capacity must be non-zero (was 0 because
     * the legacy config was read from I/O offset 0x64 instead of 0x24). */
    if (d->sectors == 0) {
        t_fail("virtio_blk_test", "capacity nonzero", "0 sectors"); fails++;
    } else {
        t_pass("virtio_blk_test", "capacity nonzero (legacy cfg @0x14)");
    }
    if (rw_roundtrip("virtio_blk_test", idx, safe_test_lba(d), 8) != 0) fails++;
    return fails ? 1 : 0;
}

/* ---- disk_rw_test <dev> ---- */

static int cmd_disk_rw_test(const char *args) {
    if (!args[0]) {
        oc_console_puts("usage: disk_rw_test <device>  (hda|sda|vda|nvme0|...)\n");
        return 1;
    }
    int idx = blk_find_device(args);
    if (idx < 0) {
        t_fail("disk_rw_test", "device lookup", "not found");
        return 1;
    }
    blk_device_t *d = blk_get_device(idx);
    int fails = 0;
    if (rw_roundtrip("disk_rw_test", idx, safe_test_lba(d), 4) != 0) fails++;
    if (rw_roundtrip("disk_rw_test", idx, safe_test_lba(d) + 4, 1) != 0) fails++;
    /* Cached-layer consistency: write through the cached API, flush,
     * then verify through the raw API. */
    u64 lba = safe_test_lba(d) + 8;
    u8 orig[512], back[512], pat[512];
    blk_read_sectors_raw(idx, lba, 1, orig);
    for (int i = 0; i < 512; i++) pat[i] = (u8)(0x5Au ^ i);
    blk_write_sectors(idx, lba, 1, pat);
    blk_flush(blk_get_device(idx));
    blk_read_sectors_raw(idx, lba, 1, back);
    blk_write_sectors_raw(idx, lba, 1, orig);
    if (oc_memcmp(back, pat, 512) == 0) {
        t_pass("disk_rw_test", "cached write + blk_flush + raw verify");
    } else {
        t_fail("disk_rw_test", "cached write + blk_flush", "mismatch");
        fails++;
    }
    return fails ? 1 : 0;
}

/* ---- partition_test <dev> ---- */

static int cmd_partition_test(const char *args) {
    char devname[BLK_DEV_NAME_LEN];
    if (args[0]) {
        oc_strncpy(devname, args, BLK_DEV_NAME_LEN - 1);
        devname[BLK_DEV_NAME_LEN - 1] = 0;
    } else {
        int idx = first_dev_of_type(BLK_TYPE_ATA);
        if (idx < 0) idx = first_dev_of_type(BLK_TYPE_AHCI);
        if (idx < 0) {
            oc_console_puts("usage: partition_test <device>\n");
            return 1;
        }
        blk_device_t *d0 = blk_get_device(idx);
        oc_strcpy(devname, d0->name);
    }
    int idx = blk_find_device(devname);
    if (idx < 0) {
        t_fail("partition_test", "device lookup", "not found");
        return 1;
    }

    /* Save LBA 0..33 (MBR + GPT area). */
    static u8 saved[34 * 512];
    if (blk_read_sectors_raw(idx, 0, 34, saved) != 0) {
        t_fail("partition_test", "save LBA0..33", "io error");
        return 1;
    }

    int fails = 0;
    part_table_t tbl;

    /* --- MBR fixture: one FAT32 partition at LBA 2048, 2048 sectors. --- */
    {
        u8 mbr[512];
        oc_memset(mbr, 0, 512);
        mbr[446 + 4] = 0x0B;                       /* type FAT32 */
        *(u32 *)(mbr + 446 + 8)  = 2048;           /* start LBA */
        *(u32 *)(mbr + 446 + 12) = 2048;           /* sectors */
        mbr[510] = 0x55; mbr[511] = 0xAA;
        blk_write_sectors_raw(idx, 0, 1, mbr);

        oc_console_puts("[partition_test] input: MBR fixture (1 part, start=2048)\n");
        oc_console_puts("[partition_test] expect: parse type=MBR count=1 start=2048\n");
        if (part_parse(idx, &tbl) == 0 && tbl.table_type == PART_TYPE_MBR
            && tbl.count == 1 && tbl.parts[0].start_lba == 2048) {
            t_pass("partition_test", "MBR parse");
        } else {
            char actual[96]; char n[24];
            oc_strcpy(actual, "type=");
            oc_strcat(actual, tbl.table_type == PART_TYPE_GPT ? "GPT" : "MBR");
            oc_strcat(actual, " count=");
            oc_u64_to_str(tbl.count, n); oc_strcat(actual, n);
            if (tbl.count > 0) {
                oc_strcat(actual, " start=");
                oc_u64_to_str(tbl.parts[0].start_lba, n);
                oc_strcat(actual, n);
            }
            t_fail("partition_test", "MBR parse", actual);
            fails++;
        }
    }

    /* --- GPT fixture: protective MBR + header @1 + 2 entries @2. --- */
    {
        static u8 gpt[34 * 512];   /* 17 KiB - too large for the kernel stack */
        oc_memset(gpt, 0, sizeof(gpt));
        /* Protective MBR. */
        gpt[446 + 4] = 0xEE;
        *(u32 *)(gpt + 446 + 8)  = 1;
        *(u32 *)(gpt + 446 + 12) = 33;
        gpt[510] = 0x55; gpt[511] = 0xAA;
        /* GPT header at LBA 1 (real field offsets per UEFI spec). */
        u8 *hdr = gpt + 512;
        oc_memcpy(hdr, "EFI PART", 8);
        *(u32 *)(hdr + 12) = 92;         /* header size */
        *(u32 *)(hdr + 80) = 128;        /* num entries */
        *(u32 *)(hdr + 84) = 128;        /* entry size  */
        *(u64 *)(hdr + 72) = 2;          /* entries start LBA */
        /* Two entries at LBA 2 (names are UTF-16LE per spec). */
        u8 *e0 = gpt + 2 * 512;
        e0[0] = 0x63;                    /* rough type byte (part.c reads [0]) */
        *(u64 *)(e0 + 32) = 2048;        /* first lba */
        *(u64 *)(e0 + 40) = 4095;        /* last lba  */
        e0[56] = 'R'; e0[58] = 'O'; e0[60] = 'O'; e0[62] = 'T';  /* UTF-16LE */
        u8 *e1 = gpt + 2 * 512 + 128;
        e1[0] = 0x0B;
        *(u64 *)(e1 + 32) = 4096;
        *(u64 *)(e1 + 40) = 8191;
        e1[56] = 'D'; e1[58] = 'A'; e1[60] = 'T'; e1[62] = 'A';  /* UTF-16LE */
        blk_write_sectors_raw(idx, 0, 34, gpt);

        oc_console_puts("[partition_test] input: GPT fixture (protective MBR + 2 entries)\n");
        oc_console_puts("[partition_test] expect: parse type=GPT count=2 names ROOT DATA\n");
        if (part_parse(idx, &tbl) == 0 && tbl.table_type == PART_TYPE_GPT
            && tbl.count == 2
            && oc_strncmp(tbl.parts[0].name, "ROOT", 5) == 0
            && oc_strncmp(tbl.parts[1].name, "DATA", 5) == 0) {
            t_pass("partition_test", "GPT parse");
        } else {
            char actual[140]; char n[24];
            oc_strcpy(actual, "type=");
            oc_strcat(actual, tbl.table_type == PART_TYPE_GPT ? "GPT" : "MBR");
            oc_strcat(actual, " count=");
            oc_u64_to_str(tbl.count, n); oc_strcat(actual, n);
            for (int i = 0; i < tbl.count && i < 4; i++) {
                oc_strcat(actual, " p");
                oc_u64_to_str((u64)i, n); oc_strcat(actual, n);
                oc_strcat(actual, "=");
                oc_strcat(actual, tbl.parts[i].name);
            }
            t_fail("partition_test", "GPT parse", actual);
            fails++;
        }
    }

    /* Restore. */
    blk_write_sectors_raw(idx, 0, 34, saved);
    blk_flush(blk_get_device(idx));
    return fails ? 1 : 0;
}

/* ---- fs_mount_test <dev> ---- */

static int cmd_fs_mount_test(const char *args) {
    if (!args[0]) {
        oc_console_puts("usage: fs_mount_test <device>  (formats the device!)\n");
        oc_console_puts("  explicit argument required - never touches a device implicitly\n");
        return 1;
    }
    int idx = blk_find_device(args);
    if (idx < 0) {
        t_fail("fs_mount_test", "device lookup", "not found");
        return 1;
    }
    blk_device_t *d = blk_get_device(idx);
    if (d->sectors < 40) {
        t_fail("fs_mount_test", "device size", "too small for FAT32");
        return 1;
    }

    char line[120];
    int fails = 0;
    oc_strcpy(line, "[fs_mount_test] input: mkfs.fat32 + fatmount on ");
    oc_strcat(line, d->name);
    oc_strcat(line, "\n");
    oc_console_puts(line);
    oc_console_puts("[fs_mount_test] expect: format OK, mount OK, file round-trip OK\n");

    /* 1. Format through the same code as the mkfs.fat32 command. */
    if (mkfs_fat32_device(idx) != 0) {
        t_fail("fs_mount_test", "mkfs.fat32", "failed");
        return 1;
    }
    t_pass("fs_mount_test", "mkfs.fat32");

    /* 2. Mount. */
    const char *mnt = "/mnt10a";
    if (fat32_mount(d->name, mnt) < 0) {
        t_fail("fs_mount_test", "fatmount", "mount failed");
        return 1;
    }
    t_pass("fs_mount_test", "fatmount");

    /* 3. File round-trip through the VFS. */
    const char *path = "/mnt10a/wp10a.txt";
    const char *msg = "Open Cube OS WP-10a storage driver test - FAT32 on ";
    int fd = vfs_open(path, VFS_O_RDWR | VFS_O_CREAT | VFS_O_TRUNC);
    if (fd < 0) {
        t_fail("fs_mount_test", "create file", "vfs_open failed");
        vfs_umount(mnt);
        return 1;
    }
    char msgbuf[96];
    oc_strcpy(msgbuf, msg);
    oc_strcpy(msgbuf + oc_strlen(msgbuf), d->name);
    int len = oc_strlen(msgbuf);
    if (vfs_write(fd, msgbuf, len) != len) {
        t_fail("fs_mount_test", "write file", "short/failed write");
        vfs_close(fd);
        vfs_umount(mnt);
        return 1;
    }
    vfs_close(fd);

    u8 rb[128];
    oc_memset(rb, 0, sizeof(rb));
    fd = vfs_open(path, VFS_O_RDONLY);
    if (fd < 0) {
        t_fail("fs_mount_test", "reopen file", "vfs_open failed");
        vfs_umount(mnt);
        return 1;
    }
    int got = vfs_read(fd, rb, sizeof(rb) - 1);
    vfs_close(fd);
    rb[got > 0 ? got : 0] = 0;
    if (got == len && oc_memcmp(rb, msgbuf, (u64)len) == 0) {
        t_pass("fs_mount_test", "file write/read round-trip");
    } else {
        t_fail("fs_mount_test", "file read-back", "content mismatch");
        fails++;
    }

    /* 4. Unmount. */
    if (vfs_umount(mnt) == 0) {
        t_pass("fs_mount_test", "umount");
    } else {
        t_fail("fs_mount_test", "umount", "failed");
        fails++;
    }
    return fails ? 1 : 0;
}

/* ---- real_hw_test ---- */

/* CPUID leaf 1, ECX bit 31 = hypervisor present. */
static int under_hypervisor(void) {
    u32 a, b, c, d;
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1u));
    return (int)((c >> 31) & 1u);
}

static int cmd_real_hw_test(const char *args) {
    (void)args;
    oc_console_puts("[real_hw_test] input: check execution environment\n");
    if (under_hypervisor()) {
        oc_console_puts("[real_hw_test] actual: hypervisor bit set (QEMU/sandbox)\n");
        oc_console_puts("[real_hw_test] => NOT RUN (no physical storage or NIC in a VM;\n");
        oc_console_puts("    real-machine SATA/NVMe/ATA and WP-10b NIC validation\n");
        oc_console_puts("    needs bare metal)\n");
        oc_console_puts("[real_hw_test] --- WP-10u in-system update ---\n");
        oc_console_puts("[real_hw_test] A/B update flows are validated in QEMU\n");
        oc_console_puts("    (update_pkg_test, ab_partition_test, update_*_test,\n");
        oc_console_puts("    real_update_test with a real reboot); real-machine A/B\n");
        oc_console_puts("    validation (GRUB installed to a physical disk) NOT RUN\n");
        oc_console_puts("    in this sandbox\n");
        oc_console_puts("[real_hw_test] --- WP-10d USB (real emulated HC + devices) ---\n");
        usb_print_state();
        usb_msc_print_state();
        usb_serial_print_state();
        usb_mouse_print_state();
        usb_audio_print_state();
        return 0;
    }
    /* Bare metal: report what the drivers detected (real validation). */
    oc_console_puts("[real_hw_test] actual: bare metal detected\n");
    ahci_print_state();
    ata_dma_print_state();
    nvme_print_state();
    oc_console_puts("[real_hw_test] --- WP-10b NIC drivers ---\n");
    e1000e_print_state();
    igb_print_state();
    ixgbe_print_state();
    rtl8139_print_state();
    rtl8168_print_state();
    rtl8125_print_state();
    rtl810x_print_state();
    bcm57xx_print_state();
    other_nics_print_state();
    oc_console_puts("[real_hw_test] --- WP-10c sound cards ---\n");
    hda_print_state();
    ac97_print_state();
    sb16_print_state();
    es1370_print_state();
    virtio_snd_print_state();
    usb_audio_print_state();
    oc_console_puts("[real_hw_test] --- WP-10d USB ---\n");
    usb_print_state();
    usb_msc_print_state();
    usb_serial_print_state();
    usb_mouse_print_state();
    oc_console_puts("[real_hw_test] => see driver reports above\n");
    return 0;
}

/* ---- registration ---- */

void disk_test_cmds_register(void) {
    shell_register_command_ex("ahci_test", cmd_ahci_test, "WP-10a: AHCI init/ports/read-write test", "WP-10a");
    shell_register_command_ex("nvme_test", cmd_nvme_test, "WP-10a: NVMe init/queues/read-write test", "WP-10a");
    shell_register_command_ex("ata_dma_test", cmd_ata_dma_test, "WP-10a: ATA Bus-Master DMA test + PIO timing", "WP-10a");
    shell_register_command_ex("virtio_blk_test", cmd_virtio_blk_test, "WP-10a: virtio-blk capacity + read/write test", "WP-10a");
    shell_register_command_ex("disk_rw_test", cmd_disk_rw_test, "WP-10a: rw round-trip on one device (disk_rw_test <dev>)", "WP-10a");
    shell_register_command_ex("partition_test", cmd_partition_test, "WP-10a: MBR+GPT parse test (partition_test [dev])", "WP-10a");
    shell_register_command_ex("fs_mount_test", cmd_fs_mount_test, "WP-10a: mkfs+mount FAT32 file round-trip (fs_mount_test <dev>)", "WP-10a");
    shell_register_command_ex("real_hw_test", cmd_real_hw_test, "WP-10a: real-hardware detection report", "WP-10a");
}
