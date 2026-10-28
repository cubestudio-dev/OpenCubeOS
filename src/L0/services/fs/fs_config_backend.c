/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS 3.0.0-WP03 G5
 * File: services/fs/fs_config_backend.c
 * Purpose: the fs STORAGE backend for the core config store
 *          (core/lib/lib_config.c) - the FAT32-or-ramfs /etc volume
 *          handling that used to live inside lib_config_init().
 *
 *          The bring-up logic is verbatim from the pre-G5 body: try
 *          the legacy "ata0" mount alias first, then every registered
 *          block device in order (fix3: the DMA driver names its
 *          drives hda..hdd); fall back to ramfs /etc; ensure the
 *          config file exists and seed it with the defaults when
 *          missing.
 */
#include "types.h"
#include "lib_config.h"
#include "fs_vfs.h"
#include "fs_fat32.h"
#include "block_svc.h"
#include "mem_heap.h"
#include "lib_string.h"
#include "lib_log.h"

/* OC_CONFIG_PATH / OC_CONFIG_PATH_DIR / OC_CONFIG_FILE_MAX come
 * from lib_config.h (single source of truth). */

static char *cfg_read(int *len_out) {
    *len_out = 0;
    int fd = fs_vfs_open(OC_CONFIG_PATH, VFS_O_RDONLY);
    if (fd < 0) return NULL;
    char *buf = (char *)kmalloc(OC_CONFIG_FILE_MAX);
    if (!buf) { fs_vfs_close(fd); return NULL; }
    u64 total = 0; int n;
    while (total < OC_CONFIG_FILE_MAX - 1 &&
           (n = fs_vfs_read(fd, buf + total, OC_CONFIG_FILE_MAX - 1 - total)) > 0) {
        total += (u64)n;
    }
    fs_vfs_close(fd);
    buf[total] = 0;
    *len_out = (int)total;
    return buf;                       /* caller kmalloc'd, caller kfrees */
}

static int cfg_write(const char *text, int len) {
    int fd = fs_vfs_open(OC_CONFIG_PATH,
                         VFS_O_WRONLY | VFS_O_CREAT | VFS_O_TRUNC);
    if (fd < 0) return -1;
    int w = fs_vfs_write(fd, text, len);
    fs_vfs_close(fd);
    return (w == len) ? 0 : -1;
}

static int cfg_bring_up(const char *defaults, int defaults_len) {
    /* 1. mount FAT32 /etc when present (legacy alias, then every
     * registered block device). */
    int mounted = (fs_fat32_mount("ata0", "/etc") == 0);
    if (!mounted) {
        int n = driver_block_num_devices();
        for (int i = 0; i < n && !mounted; i++) {
            driver_block_device_t *d = driver_block_get_device(i);
            if (d && d->name[0])
                mounted = (fs_fat32_mount(d->name, "/etc") == 0);
        }
    }
    if (!mounted) {
        /* ramfs fallback: create /etc so config paths resolve */
        if (fs_vfs_mkdir(OC_CONFIG_PATH_DIR) < 0) {
            /* /etc may already exist in ramfs - that is fine */
        }
    }

    /* 2. ensure the file exists (seed with defaults when missing). */
    fs_vfs_stat_t st;
    if (fs_vfs_stat(OC_CONFIG_PATH, &st) < 0) {
        if (cfg_write(defaults, defaults_len) == 0) {
            lib_log_info(mounted
                ? "config: created default /etc/opencube.conf (fat32)"
                : "config: created default /etc/opencube.conf (ramfs)");
        } else {
            lib_log_info("config: FAILED to create /etc/opencube.conf");
            return -1;
        }
    }
    return 0;
}

/* the fs backend: FAT32 when a volume mounted, ramfs otherwise (the
 * name/persistent fields are filled at bring-up time... they are
 * const in the struct, so report the honest superset: "fat32/ramfs".
 * The log line in lib_config_init_with prints this name.) */
static const lib_config_backend_t g_fs_config_backend = {
    "fat32/ramfs", 1,
    cfg_bring_up,
    cfg_read,
    cfg_write,
};

const lib_config_backend_t *fs_config_backend(void) {
    return &g_fs_config_backend;
}
