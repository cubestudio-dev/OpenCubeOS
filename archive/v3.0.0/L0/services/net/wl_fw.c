/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP03
 * File: services/net/wl_fw.c
 * Purpose: firmware-file provider for the wireless driver framework.
 *
 * 3.0.0-WP03 G2 dependency inversion: the wireless family drivers
 * (iwlwifi / rtlwifi / ath10k+ / brcm) used to call the fs root service
 * directly to load /etc/firmware blobs - a drivers->services edge.
 * The net root service now installs this provider into the framework
 * (wl_fw_set_reader / wl_fw_set_exists, same hook pattern as the
 * display console hook); the drivers read firmware only through
 * wl_fw_read_file / wl_fw_exists.
 *
 * Buffer contract (unchanged from the in-driver reads): the reader
 * returns a kmalloc'd buffer that the CALLING DRIVER owns for the
 * device lifetime; the existence probe opens + closes only.
 */
#include "types.h"
#include "mem_heap.h"
#include "fs_vfs.h"
#include "driver_wireless.h"

/* Firmware blob cap: the largest shipped container is the iwlwifi
 * ucode at 512 KiB (see driver_iwlwifi.c); one cap for all families
 * keeps the provider simple - a bigger blob is a corrupt file. */
#define WL_FW_MAX (512 * 1024)

int net_wl_fw_read(const char *path, u8 **buf_out, u32 *len_out)
{
    int fd, n;
    u8 *buf;
    u32 len = 0;

    if (!path || !buf_out || !len_out) return -1;
    *buf_out = 0;
    *len_out = 0;
    fd = fs_vfs_open(path, VFS_O_RDONLY);
    if (fd < 0) return -2;              /* no vendor firmware present */
    buf = (u8 *)kmalloc(WL_FW_MAX);
    if (!buf) { fs_vfs_close(fd); return -3; }
    for (;;) {
        n = fs_vfs_read(fd, buf + len, 4096);
        if (n <= 0) break;
        len += (u32)n;
        if (len >= WL_FW_MAX) { fs_vfs_close(fd); return -4; }
    }
    fs_vfs_close(fd);
    if (len == 0) return -5;
    *buf_out = buf;
    *len_out = len;
    return 0;
}

int net_wl_fw_exists(const char *path)
{
    int fd = fs_vfs_open(path, VFS_O_RDONLY);
    if (fd < 0) return 0;
    fs_vfs_close(fd);
    return 1;
}

void net_wl_fw_provider_install(void)
{
    wl_fw_set_reader(net_wl_fw_read);
    wl_fw_set_exists(net_wl_fw_exists);
}
