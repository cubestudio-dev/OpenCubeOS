/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP06c-installer_gui
 * File: ocse/ocse_inst.c
 * Purpose: see ocse_inst.h.
 */
#include "ocse_inst.h"
#include "ocse_cl.h"
#include "ocse_part.h"
#include "ocse_disk.h"
#include "ocse_i18n.h"
#include "ocse_mkfs.h"
#include "fs_vfs.h"
#include "driver_block_blk.h"
#include "driver_block_part.h"   /* WP06g3-4.4: recovery-partition carve */
#include "lib_string.h"
#include "screen_console.h"

#define INST_MBR_PATH   "/install/OCBLMBR.BIN"
#define INST_TAR_PATH   "/install/OCOS.TAR"
#define INST_CONF_REL   "/etc/opencube.conf"
#define INST_CONF_MAX   1024

/* Output goes through ocse_out (console + serial), same as the CLI. */
void ocse_out(const char* s);
void ocse_outln(const char* s);

/* ==================================================================
 * ocbl-install
 * ================================================================== */
int ocse_inst_ocbl(int disk_idx, const char* medium_path,
                   u8* work, int work_sz) {
    if (!medium_path || !work || work_sz < 65536) return -100;
    driver_block_device_t* dv = driver_block_get_device(disk_idx);
    if (!dv || !dv->present) return -2;

    /* 1. read the medium image.
     * WP06f cross-review fix: the read used to be capped at 32768 B
     * (63 core sectors) while the OCBL core has long outgrown that
     * (37376 B image / 73 sectors at WP06f) - ocbl-install silently
     * wrote a TRUNCATED core and the installed disk hung at boot with
     * zero serial output (live-caught by the b5 phase-2 battery on a
     * pre-WP06f tree too: the bug is latent since the core passed
     * 32768 B).  The buffer layout below now takes the full 64 KiB
     * work buffer: image [0,40960) (80-sector cap, honest refusal
     * beyond), MBR scratch [40960,41472), verify window
     * [41472,65536) = 47 sectors per round. */
    int fd = fs_vfs_open(medium_path, VFS_O_RDONLY);
    if (fd < 0) {
        ocse_out("ocbl-install: cannot open ");
        ocse_outln(medium_path);
        return -1;
    }
    int img_len = fs_vfs_read(fd, work, 40960);
    fs_vfs_close(fd);
    if (img_len < 1024 || (img_len & 511) != 0 ||
        work[510] != 0x55 || work[511] != 0xAA) {
        ocse_outln("ocbl-install: medium image is not a valid OCBL boot image");
        return -1;
    }

    /* 2. the disk's current table (also the bytes we must preserve) */
    u8* mbr = work + 40960;                  /* 512 B scratch */
    if (driver_block_read_sectors_raw(disk_idx, 0, 1, mbr) != 0)
        return -4;
    u64 first_part_lba = 0;
    int nparts = 0;
    for (int i = 0; i < 4; i++) {
        const u8* e = mbr + 446 + 16 * i;
        u32 type = e[4];
        u32 lba = (u32)e[8] | ((u32)e[9] << 8) | ((u32)e[10] << 16) |
                  ((u32)e[11] << 24);
        if (type == 0 || lba == 0) continue;
        nparts++;
        if (first_part_lba == 0 || lba < first_part_lba) first_part_lba = lba;
    }
    if (nparts == 0 || first_part_lba == 0) {
        ocse_outln(ocse_tr(OCSE_T_O_NOPART));
        return -2;
    }

    /* 3. the core must fit the gap between LBA 1 and partition 1 */
    u32 core_secs = (u32)((img_len - 512 + 511) / 512);
    if ((u64)core_secs > first_part_lba - 1) {
        ocse_outln("ocbl-install: no room for the boot core before the first partition");
        return -3;
    }

    /* 4. patch block (see ocbl/bios/stage1.asm):
     *    +0x1A0 u32 magic, +0x1A4 u32 lba_lo, +0x1A8 u32 lba_hi,
     *    +0x1AC u16 core_sectors */
    work[0x1A0] = 0x4F; work[0x1A1] = 0x43;   /* 'O' 'C' */
    work[0x1A2] = 0x42; work[0x1A3] = 0x4C;   /* 'B' 'L' */
    u64 core_lba = 1;
    work[0x1A4] = (u8)(core_lba);        work[0x1A5] = (u8)(core_lba >> 8);
    work[0x1A6] = (u8)(core_lba >> 16);  work[0x1A7] = (u8)(core_lba >> 24);
    work[0x1A8] = 0; work[0x1A9] = 0; work[0x1AA] = 0; work[0x1AB] = 0;
    work[0x1AC] = (u8)core_secs; work[0x1AD] = (u8)(core_secs >> 8);

    /* 5. keep the real partition table (bytes 446..509) */
    memcpy(work + 446, mbr + 446, 64);

    /* 6. write stage1 (LBA 0) + core (LBA 1..) */
    if (driver_block_write_sectors(disk_idx, 0, 1, work) != 0)
        return -4;
    if (driver_block_write_sectors(disk_idx, core_lba, core_secs, work + 512) != 0)
        return -4;

    /* 7. writer-side verify (stage1 is silent; the installer speaks).
     * WP06f: the verify window is 24064 B (47 sectors) - the core is
     * verified in ROUNDS instead of one oversized read. */
    u8* vfy = work + 41472;                  /* 24064 B verify window */
    if (driver_block_read_sectors_raw(disk_idx, 0, 1, vfy) != 0)
        return -5;
    if (memcmp(vfy, work, 512) != 0) {
        ocse_outln("ocbl-install: stage1 verify FAILED");
        return -5;
    }
    {
        u32 left = core_secs;
        u64 lba = core_lba;
        const u8* src = work + 512;
        while (left > 0) {
            u32 chunk = left > 47 ? 47 : left;
            if (driver_block_read_sectors_raw(disk_idx, lba, chunk, vfy) != 0)
                return -5;
            if (memcmp(vfy, src, (usize)(chunk * 512)) != 0) {
                ocse_outln("ocbl-install: core verify FAILED");
                return -5;
            }
            left -= chunk;
            lba += chunk;
            src += (u64)chunk * 512;
        }
    }

    char n[24];
    ocse_out(ocse_tr(OCSE_T_O_BOOTCODE));
    u64_to_str(core_secs, n); ocse_out(n);
    ocse_out(" sectors @LBA ");
    u64_to_str(core_lba, n); ocse_out(n);
    ocse_outln(ocse_tr(OCSE_T_O_VERIFIED));
    return 0;
}

/* ==================================================================
 * install state machine
 * ================================================================== */
static int inst_find_mount(const char* dev, char* path, int cap) {
    fs_vfs_mount_info_t m[VFS_MAX_MOUNTS];
    int n = fs_vfs_get_mounts(m, VFS_MAX_MOUNTS);
    for (int i = 0; i < n; i++) {
        if (m[i].in_use && strcmp(m[i].device, dev) == 0) {
            strncpy(path, m[i].mount_point, (usize)(cap - 1));
            path[cap - 1] = 0;
            return 0;
        }
    }
    return -1;
}

/* step 6.5 (WP06g3-4.4): the dedicated OCIRE recovery partition.
 * The tar already landed /boot/ocire.elf inside the system partition
 * (an OCBL-scanable recovery entry on its own); this step additionally
 * carves a DEDICATED recovery partition out of the disk's unallocated
 * tail, formats it FAT32 and copies the recovery kernel onto it, so
 * the installed machine keeps the recovery entry even if the system
 * volume is later damaged.  Skipped with an honest report when the
 * tail is too small or the table is full - the in-volume entry stays.
 * Returns 0 always: a skipped recovery partition is not a failed
 * install (the system volume carries the entry). */
static void inst_recovery_skip(const char* why) {
    ocse_out("install: recovery partition skipped - ");
    ocse_out(why);
    ocse_outln(" (the system volume keeps /boot/ocire.elf)");
}

static void inst_recovery_partition(const char* disk, const char* sys_mount,
                                     u8* work, int work_sz) {
    int didx = ocse_part_disk_idx(disk);
    if (didx < 0) { inst_recovery_skip("no such disk"); return; }
    driver_block_part_table_t tbl;
    if (driver_block_part_parse(didx, &tbl) != 0) {
        inst_recovery_skip("partition table unreadable");
        return;
    }
    driver_block_device_t* dev = driver_block_get_device(didx);
    if (!dev || !dev->present) { inst_recovery_skip("disk gone"); return; }

    /* the end of the last partition + a free table slot */
    u64 last_end = 2047;   /* LBA 0..2047 stays reserved (alignment) */
    for (int i = 0; i < tbl.count; i++) {
        if (!tbl.parts[i].present) continue;
        u64 e = tbl.parts[i].start_lba + tbl.parts[i].sectors - 1;
        if (e > last_end) last_end = e;
    }
    if (tbl.table_type == PART_TYPE_MBR && tbl.count >= 4) {
        inst_recovery_skip("MBR table full");
        return;
    }
    /* at least 16 MiB of tail space, 1 MiB-aligned start */
    u64 start_mib = (last_end + 1 + 2047) / 2048;
    u64 disk_mib  = dev->sectors / 2048;
    if (disk_mib < 16 || start_mib + 16 > disk_mib) {
        inst_recovery_skip("no unallocated tail space");
        return;
    }

    /* the recovery kernel, read from the freshly extracted system volume */
    char src[VFS_PATH_LEN];
    int sl = (int)strlen(sys_mount);
    if (sl + 20 >= (int)sizeof(src)) { inst_recovery_skip("path too long"); return; }
    memcpy(src, sys_mount, (usize)sl);
    strcpy(src + sl, "/boot/ocire.elf");
    fs_vfs_stat_t st;
    if (fs_vfs_stat(src, &st) != 0 || st.size == 0 ||
        st.size > (u64)work_sz) {
        inst_recovery_skip("recovery kernel unreadable");
        return;
    }
    int fd = fs_vfs_open(src, VFS_O_RDONLY);
    if (fd < 0) { inst_recovery_skip("recovery kernel open failed"); return; }
    int got = 0;
    while (got < (int)st.size) {
        int r = fs_vfs_read(fd, work + got, (int)st.size - got);
        if (r <= 0) break;
        got += r;
    }
    fs_vfs_close(fd);
    if (got != (int)st.size) {
        inst_recovery_skip("recovery kernel short read");
        return;
    }

    /* carve, format, mount, write */
    ocse_outln("install: creating the dedicated OCIRE recovery partition ...");
    if (ocse_part_mkpart(disk, start_mib, disk_mib) != 0) {
        inst_recovery_skip("mkpart failed");
        return;
    }
    driver_block_part_table_t fresh;
    int rec_no = -1;
    if (driver_block_part_parse(didx, &fresh) == 0) {
        for (int i = 0; i < fresh.count; i++) {
            if (fresh.parts[i].present &&
                fresh.parts[i].start_lba == start_mib * 2048) {
                rec_no = i + 1;
                break;
            }
        }
    }
    if (rec_no < 0) { inst_recovery_skip("created partition not found"); return; }
    char recdev[40];
    int dl = (int)strlen(disk);
    if (dl + 8 >= (int)sizeof(recdev)) { inst_recovery_skip("name too long"); return; }
    memcpy(recdev, disk, (usize)dl);
    recdev[dl] = 'p';
    char nb[8];
    u64_to_str((u64)rec_no, nb);
    strcpy(recdev + dl + 1, nb);
    int ridx = driver_block_find_device(recdev);
    if (ridx < 0) { inst_recovery_skip("partition device missing"); return; }
    if (ocse_mkfs_fat32(ridx) != 0) {
        inst_recovery_skip("mkfs.fat32 failed");
        return;
    }
    if (fs_vfs_mount("fat32", "/rec", recdev) != 0) {
        inst_recovery_skip("mount failed");
        return;
    }
    fs_vfs_mkdir("/rec/boot");
    int wfd = fs_vfs_open("/rec/boot/ocire.elf",
                          VFS_O_WRONLY | VFS_O_CREAT | VFS_O_TRUNC);
    if (wfd < 0) {
        fs_vfs_umount("/rec");
        inst_recovery_skip("cannot create /boot/ocire.elf on it");
        return;
    }
    int w = fs_vfs_write(wfd, work, got);
    fs_vfs_close(wfd);
    fs_vfs_umount("/rec");
    if (w != got) {
        inst_recovery_skip("short write");
        return;
    }
    ocse_out("install: recovery partition #");
    ocse_out(recdev);
    ocse_out(" ready (boot/ocire.elf, ");
    char n[24];
    u64_to_str((u64)got, n); ocse_out(n);
    ocse_outln(" B)");
}

int ocse_inst_install(const char* disk, int part_no, int confirm,
                      u8* work, int work_sz) {
    char n[24];
    char dev[40];

    /* step 1: the medium */
    ocse_outln(ocse_tr(OCSE_T_I_MEDIUMCHK));
    fs_vfs_stat_t st;
    if (fs_vfs_stat(INST_TAR_PATH, &st) != 0 ||
        fs_vfs_stat(INST_MBR_PATH, &st) != 0) {
        ocse_outln(ocse_tr(OCSE_T_I_MFATAL));
        return 1;
    }
    ocse_out(ocse_tr(OCSE_T_I_MEDIUMOK));
    u64_to_str(st.size, n); ocse_out(n);
    ocse_outln(" B)");

    /* step 2: the target device */
    int dl = (int)strlen(disk);
    if (dl > 32) return 2;
    memcpy(dev, disk, (usize)dl);
    dev[dl] = 'p';
    u64_to_str((u64)part_no, dev + dl + 1);
    int tidx = driver_block_find_device(dev);
    if (tidx < 0) {
        ocse_out("install: FATAL - no such partition: ");
        ocse_outln(dev);
        return 2;
    }

    /* step 3: filesystem - OCBL boots from FAT volumes only */
    ocse_fs_t fs = ocse_disk_probe_fs(dev);
    if (fs != OCSE_FS_FAT32) {
        ocse_out("install: FATAL - '");
        ocse_out(dev);
        ocse_out("'");
        ocse_outln(ocse_tr(OCSE_T_I_NOTFAT));
        ocse_outln(ocse_tr(OCSE_T_I_NOTFAT2));
        return 3;
    }

    /* step 4: the target must be mounted (that is where files land) */
    char mount[VFS_PATH_LEN];
    if (inst_find_mount(dev, mount, sizeof(mount)) != 0) {
        ocse_out("install: FATAL - ");
        ocse_out(dev);
        ocse_outln(ocse_tr(OCSE_T_I_NOTMOUNTED));
        return 4;
    }
    ocse_out("install: target ");
    ocse_out(dev);
    ocse_out(ocse_tr(OCSE_T_I_TARGETW));
    ocse_outln(mount);

    /* step 5: the confirm gate */
    if (!confirm) {
        ocse_outln(ocse_tr(OCSE_T_I_GATE));
        ocse_out("install: ");
        ocse_outln(ocse_tr(OCSE_T_M_NOWRITE));
        return 5;
    }

    /* step 6: extract the system image */
    ocse_outln(ocse_tr(OCSE_T_I_EXTRACT));
    if (ocse_cl_tar_extract(INST_TAR_PATH, mount) != 0) {
        ocse_outln("install: FATAL - extraction failed");
        return 6;
    }
    char kpath[VFS_PATH_LEN];
    int ml = (int)strlen(mount);
    if (ml + (int)strlen("/boot/opencube.elf") + 1 > (int)sizeof(kpath)) return 6;
    memcpy(kpath, mount, (usize)ml);
    strcpy(kpath + ml, "/boot/opencube.elf");
    if (fs_vfs_stat(kpath, &st) != 0) {
        ocse_outln("install: FATAL - /boot/opencube.elf missing after extraction");
        return 6;
    }
    ocse_out(ocse_tr(OCSE_T_I_KERNELIN));
    u64_to_str(st.size, n); ocse_out(n);
    ocse_outln(" B)");

    /* step 6.5 (WP06g3-4.4): the dedicated OCIRE recovery partition.
     * The extracted system volume already carries /boot/ocire.elf
     * (the in-volume recovery entry); this carves the dedicated
     * recovery partition from the disk's unallocated tail. */
    inst_recovery_partition(disk, mount, work, work_sz);

    /* step 7: boot code */
    ocse_outln(ocse_tr(OCSE_T_I_BOOTCODE));
    int didx = ocse_part_disk_idx(disk);
    if (didx < 0) return 2;
    int rc = ocse_inst_ocbl(didx, INST_MBR_PATH, work, work_sz);
    if (rc != 0) {
        ocse_outln("install: FATAL - ocbl-install failed");
        return 7;
    }

    /* step 8: boot flag */
    if (ocse_part_setflag(disk, part_no, "boot", 1) != 0) {
        ocse_outln("install: FATAL - could not set the boot flag");
        return 8;
    }
    ocse_out("install: partition #");
    u64_to_str((u64)part_no, n); ocse_out(n);
    ocse_outln(ocse_tr(OCSE_T_I_BOOTABLE));

    ocse_outln("");
    ocse_outln(ocse_tr(OCSE_T_I_COMPLETE));
    ocse_outln(ocse_tr(OCSE_T_I_REMOVE));
    ocse_outln(ocse_tr(OCSE_T_I_REBOOT));
    return 0;
}

/* ==================================================================
 * config viewer / editor
 * ================================================================== */
static int conf_target(char* mount, int cap) {
    /* the mounted FAT32 partition = the installed system */
    fs_vfs_mount_info_t m[VFS_MAX_MOUNTS];
    int n = fs_vfs_get_mounts(m, VFS_MAX_MOUNTS);
    for (int i = 0; i < n; i++) {
        if (!m[i].in_use) continue;
        if (ocse_disk_probe_fs(m[i].device) != OCSE_FS_FAT32) continue;
        strncpy(mount, m[i].mount_point, (usize)(cap - 1));
        mount[cap - 1] = 0;
        return 0;
    }
    return -1;
}

static int conf_read(const char* path, char* buf, int cap) {
    int fd = fs_vfs_open(path, VFS_O_RDONLY);
    if (fd < 0) return -1;
    int n = fs_vfs_read(fd, buf, cap - 1);
    fs_vfs_close(fd);
    if (n < 0) return -1;
    buf[n] = 0;
    return n;
}

int ocse_inst_config_show(void) {
    char mount[VFS_PATH_LEN];
    if (conf_target(mount, sizeof(mount)) != 0) {
        ocse_outln(ocse_tr(OCSE_T_C_NOMOUNT));
        return 1;
    }
    char path[VFS_PATH_LEN];
    int ml = (int)strlen(mount);
    if (ml + (int)strlen(INST_CONF_REL) + 1 > (int)sizeof(path)) return 1;
    memcpy(path, mount, (usize)ml);
    strcpy(path + ml, INST_CONF_REL);
    char buf[INST_CONF_MAX];              /* stack: .bss is full
                                            * (boot PTs start at 0x20e000,
                                            * ~140 B of headroom) */
    int n = conf_read(path, buf, sizeof(buf));
    if (n < 0) {
        ocse_out("config: no configuration at ");
        ocse_outln(path);
        return 1;
    }
    ocse_out("--- ");
    ocse_out(path);
    ocse_outln(" ---");
    for (int i = 0; i < n; i++) {
        char one[2] = { buf[i], 0 };
        if (buf[i] == '\n') ocse_outln("");
        else if (buf[i] == '\r') { /* skip */ }
        else ocse_out(one);
    }
    if (n > 0 && buf[n - 1] != '\n') ocse_outln("");
    return 0;
}

static int conf_key_valid(const char* key, const char* value) {
    if (strcmp(key, "lang") == 0)
        return (strcmp(value, "en") == 0 || strcmp(value, "zh") == 0);
    if (strcmp(key, "boot.slot") == 0)
        return (strcmp(value, "A") == 0 || strcmp(value, "B") == 0);
    if (strcmp(key, "hostname") == 0) {
        int l = (int)strlen(value);
        if (l < 1 || l > 31) return 0;
        for (int i = 0; i < l; i++) {
            char c = value[i];
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.'))
                return 0;
        }
        return 1;
    }
    /* WP-06c: the wizard's system step sets the two update keys.
     * update_url must be an http(s) URL; auto_check is yes/no. */
    if (strcmp(key, "update_url") == 0) {
        int l = (int)strlen(value);
        if (l < 10 || l > 79) return 0;
        return strncmp(value, "http://", 7) == 0 ||
               strncmp(value, "https://", 8) == 0;
    }
    if (strcmp(key, "auto_check") == 0)
        return strcmp(value, "yes") == 0 || strcmp(value, "no") == 0;
    return 0;
}

int ocse_inst_config_set(const char* key, const char* value) {
    if (!conf_key_valid(key, value)) {
        ocse_outln(ocse_tr(OCSE_T_C_INVALID));
        ocse_outln(ocse_tr(OCSE_T_C_KEYS2));
        return 1;
    }
    char mount[VFS_PATH_LEN];
    if (conf_target(mount, sizeof(mount)) != 0) {
        ocse_outln(ocse_tr(OCSE_T_C_NOMOUNT));
        return 1;
    }
    char path[VFS_PATH_LEN];
    int ml = (int)strlen(mount);
    if (ml + (int)strlen(INST_CONF_REL) + 1 > (int)sizeof(path)) return 1;
    memcpy(path, mount, (usize)ml);
    strcpy(path + ml, INST_CONF_REL);
    char buf[INST_CONF_MAX];              /* stack: .bss is full
                                            * (boot PTs start at 0x20e000,
                                            * ~140 B of headroom) */
    int n = conf_read(path, buf, sizeof(buf));
    if (n < 0) {
        ocse_out("config: no configuration at ");
        ocse_outln(path);
        return 1;
    }
    /* replace the key line in place, or append it */
    char line[96];
    int ll = 0;
    const char* p = key;
    while (*p && ll < 60) line[ll++] = *p++;
    line[ll++] = '=';
    p = value;
    while (*p && ll < 90) line[ll++] = *p++;
    line[ll] = 0;

    char out[INST_CONF_MAX];
    int ol = 0;
    int i = 0;
    int replaced = 0;
    int klen = (int)strlen(key);
    while (i < n) {
        int e = i;
        while (e < n && buf[e] != '\n') e++;
        int len = e - i;
        if (!replaced && len > klen && buf[i + klen] == '=' &&
            strncmp(buf + i, key, (usize)klen) == 0) {
            for (int j = 0; j < ll && ol < INST_CONF_MAX - 2; j++)
                out[ol++] = line[j];
            out[ol++] = '\n';
            replaced = 1;
        } else {
            for (int j = 0; j < len && ol < INST_CONF_MAX - 2; j++)
                out[ol++] = buf[i + j];
            out[ol++] = '\n';
        }
        i = (e < n) ? e + 1 : e;
    }
    if (!replaced) {
        for (int j = 0; j < ll && ol < INST_CONF_MAX - 2; j++)
            out[ol++] = line[j];
        out[ol++] = '\n';
    }
    int fd = fs_vfs_open(path, VFS_O_WRONLY | VFS_O_CREAT | VFS_O_TRUNC);
    if (fd < 0) {
        ocse_outln("config: cannot write the configuration");
        return 1;
    }
    int w = fs_vfs_write(fd, out, ol);
    fs_vfs_close(fd);
    if (w != ol) {
        ocse_outln("config: short write");
        return 1;
    }
    ocse_out("config: ");
    ocse_out(key);
    ocse_out("=");
    ocse_out(value);
    ocse_outln(ocse_tr(OCSE_T_C_WRITTEN));
    return 0;
}
