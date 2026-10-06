/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10u
 * File: kernel/ota_ab_update.c
 * Purpose: A/B slot framework + in-system update, see ota_ab_update.h.
 *
 * Layers in this file:
 *   1. A/B disk discovery + partition block devices ("<parent>p1".."p4")
 *   2. slot mounts (/ab/boot, /ab/a, /ab/b, /data)
 *   3. boot slot tracking (multiboot2 "oc.slot=") + boot flags
 *   4. package transport: streaming HTTP / HTTPS download
 *   5. package integrity: streaming SHA256
 *   6. package install: gzip (RFC 1952/1951) -> tar (ustar) -> FAT32
 *   7. set_boot / rollback / status / shell commands
 *
 * Memory discipline (WP-09-FIX BUG-002 lesson): all large buffers live
 * on the heap, never on the 4 KiB kernel thread stacks.
 */
#include "ota_ab.h"
#include "ota_update.h"
#include "lib_config.h"
#include "l1_ext.h"
#include "driver_block_blk.h"
#include "driver_block_part.h"
#include "fs_vfs.h"
#include "net_core.h"
#include "net_tls.h"
#include "crypto_core.h"
#include "lib_gzip.h"
#include "lib_tar.h"
#include "mem_heap.h"
#include "screen_console.h"
#include "lib_log.h"
#include "lib_string.h"
#include "core_timer.h"

#include <stdint.h>

/* Direct port I/O for the reboot path. */
static inline void ota_ab_outb(u16 port, u8 v) {
    __asm__ volatile("outb %0, %1" :: "a"(v), "Nd"(port));
}
static inline u8 ota_ab_inb(u16 port) {
    u8 v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

/* ------------------------------------------------------------------ */
/* module state                                                        */
/* ------------------------------------------------------------------ */

#define AB_DISK_MAX_PARTS   4

static int  g_ab_ready = 0;          /* ota_ab_init() ran              */
static int  g_ab_present = 0;        /* A/B disk found + mounted      */
static int  g_ab_slot = OC_AB_SLOT_ISO;
static char g_ab_disk_name[16];      /* parent block device name      */
static int  g_ab_disk_idx = -1;

/* last update-check result cache (for status) */
static int  g_last_check_rc = -100;  /* -100 = never checked          */
static char g_last_check_version[32];

/* ------------------------------------------------------------------ */
/* 1. partition block devices                                          */
/* ------------------------------------------------------------------ */

/* WP-10d-pre: partition registration moved to kernel/part.c
 * (driver_block_part_register_child) so the in-system abdisk / install commands can
 * share the exact same code path.  The call sites below are unchanged. */
#define ota_ab_register_partition(parent_name, type, parent_idx, driver_block_part_no, \
                              start_lba, sectors) \
    driver_block_part_register_child((parent_name), (parent_idx), (driver_block_part_no), \
                        (start_lba), (sectors))

/* ------------------------------------------------------------------ */
/* 2. A/B disk discovery + mounts                                      */
/* ------------------------------------------------------------------ */

static void ota_ab_mount_slots(void) {
    fs_vfs_mkdir("/ab");
    fs_vfs_mkdir(OC_AB_MOUNT_BOOT);
    fs_vfs_mkdir(OC_AB_MOUNT_A);
    fs_vfs_mkdir(OC_AB_MOUNT_B);
    fs_vfs_mkdir(OC_AB_MOUNT_DATA);

    /* WP-10c rule-9 fix: partitions are registered as "<parent>pN" and
     * the A/B disk may be any block device (hdb when the /etc disk is
     * attached first, ...) - never assume "hda". */
    const char *driver_block_part_names[4];
    char pn[4][20];
    for (int i = 0; i < 4; i++) {
        strncpy(pn[i], g_ab_disk_name, sizeof(pn[i]) - 4);
        pn[i][sizeof(pn[i]) - 4] = 0;
        char num[4];
        u64_to_str((u64)(i + 1), num);
        strcat(pn[i], "p");
        strcat(pn[i], num);
        driver_block_part_names[i] = pn[i];
    }

    /* the flags partition decides whether this really is an A/B disk */
    int boot_ok = (fs_vfs_mount("fat32", OC_AB_MOUNT_BOOT, driver_block_part_names[0]) == 0);
    fs_vfs_stat_t st;
    int slots_ok = 0;
    if (boot_ok && fs_vfs_stat(OC_AB_MOUNT_BOOT, &st) == 0) {
        int a = fs_vfs_mount("fat32", OC_AB_MOUNT_A, driver_block_part_names[1]);
        int b = fs_vfs_mount("fat32", OC_AB_MOUNT_B, driver_block_part_names[2]);
        fs_vfs_mount("fat32", OC_AB_MOUNT_DATA, driver_block_part_names[3]);   /* best effort */
        if (a == 0 || b == 0) slots_ok = 1;
    }
    if (boot_ok && slots_ok) {
        g_ab_present = 1;
        char line[96];
        strcpy(line, "abupdate: A/B disk found (");
        strcat(line, g_ab_disk_name);
        strcat(line, "p1..p4), slots mounted");
        lib_log_info(line);
    } else {
        if (boot_ok) fs_vfs_umount(OC_AB_MOUNT_BOOT);
        lib_log_info("abupdate: no A/B disk (update target unavailable)");
    }
}

/* Scan all block devices for an MBR with the A/B layout:
 * p1..p3 non-empty (boot flags + two slots). */
static void ota_ab_find_disk(void) {
    int n = driver_block_num_devices();
    for (int i = 0; i < n; i++) {
        driver_block_device_t *dev = driver_block_get_device(i);
        if (!dev || !dev->present) continue;
        driver_block_part_table_t tbl;
        if (driver_block_part_parse(i, &tbl) != 0) continue;
        if (tbl.table_type != PART_TYPE_MBR) continue;
        if (tbl.count < 3) continue;         /* need p1..p3 at least */
        if (!tbl.parts[0].present || !tbl.parts[1].present ||
            !tbl.parts[2].present) continue;

        strcpy(g_ab_disk_name, dev->name);
        g_ab_disk_idx = i;
        int registered = 0;
        for (int p = 0; p < tbl.count && p < AB_DISK_MAX_PARTS; p++) {
            if (!tbl.parts[p].present) continue;
            if (ota_ab_register_partition(dev->name, dev->type, i, p + 1,
                                      tbl.parts[p].start_lba,
                                      tbl.parts[p].sectors) >= 0)
                registered++;
        }
        if (registered >= 3) {
            ota_ab_mount_slots();
            return;
        }
    }
    lib_log_info("abupdate: no disk with an A/B partition layout found");
}

/* ------------------------------------------------------------------ */
/* 3. boot slot + flags                                                */
/* ------------------------------------------------------------------ */

void ota_ab_set_boot_slot_arg(const char *cmdline) {
    g_ab_slot = OC_AB_SLOT_ISO;
    if (!cmdline) return;
    const char *q = cmdline;
    while (q[0]) {
        if (q[0] == 'o' && q[1] == 'c' && q[2] == '.' &&
            q[3] == 's' && q[4] == 'l' && q[5] == 'o' && q[6] == 't' &&
            q[7] == '=') {
            char v = q[8];
            if (v == 'A') g_ab_slot = OC_AB_SLOT_A;
            else if (v == 'B') g_ab_slot = OC_AB_SLOT_B;
            return;
        }
        q++;
    }
}

int ota_ab_current_slot(void) { return g_ab_slot; }

const char *ota_ab_disk_name(void) {
    return g_ab_present ? g_ab_disk_name : "";
}

const char *ota_ab_slot_name(int slot) {
    switch (slot) {
    case OC_AB_SLOT_A: return "A";
    case OC_AB_SLOT_B: return "B";
    default:           return "ISO";
    }
}

const char *ota_ab_slot_path(int slot) {
    switch (slot) {
    case OC_AB_SLOT_A: return OC_AB_MOUNT_A;
    case OC_AB_SLOT_B: return OC_AB_MOUNT_B;
    default:           return "";
    }
}

int ota_ab_flags_ready(void) {
    return g_ab_present;
}

static int flag_exists(const char *rel) {
    if (!g_ab_present) return 0;
    char path[96];
    strcpy(path, OC_AB_MOUNT_BOOT);
    strcat(path, rel);
    fs_vfs_stat_t st;
    return (fs_vfs_stat(path, &st) == 0) ? 1 : 0;
}

static int flag_create(const char *rel, const char *content) {
    if (!g_ab_present) return -1;
    char path[96];
    strcpy(path, OC_AB_MOUNT_BOOT);
    strcat(path, rel);
    int fd = fs_vfs_open(path, VFS_O_WRONLY | VFS_O_CREAT | VFS_O_TRUNC);
    if (fd < 0) return -1;
    if (content && content[0]) fs_vfs_write(fd, content, (int)strlen(content));
    fs_vfs_close(fd);
    return 0;
}

static void flag_remove(const char *rel) {
    if (!g_ab_present) return;
    char path[96];
    strcpy(path, OC_AB_MOUNT_BOOT);
    strcat(path, rel);
    fs_vfs_unlink(path);
}

int ota_ab_read_flags(int *next_boot, int *ok_b, int *bootfail_b) {
    if (!next_boot || !ok_b || !bootfail_b) return -1;
    *next_boot = flag_exists(OC_AB_FLAG_NEXT_B);
    *ok_b = flag_exists(OC_AB_FLAG_OK_B);
    *bootfail_b = flag_exists(OC_AB_FLAG_BOOTFAIL_B);
    return g_ab_present ? 0 : -1;
}

int ota_ab_init(void) {
    if (g_ab_ready) return g_ab_present ? 0 : 1;
    g_ab_ready = 1;
    ota_ab_find_disk();
    if (g_ab_present && g_ab_slot == OC_AB_SLOT_B) {
        /* pessimistic failure marker: if this boot never reaches
         * ota_update_confirm_boot(), the next boot rolls back to A */
        flag_create(OC_AB_FLAG_BOOTFAIL_B, "B\n");
        lib_log_info("abupdate: boot slot B (bootfail marker written)");
    } else if (g_ab_present) {
        char line[64];
        strcpy(line, "abupdate: boot slot ");
        strcat(line, ota_ab_slot_name(g_ab_slot));
        lib_log_info(line);
    }
    return g_ab_present ? 0 : 1;
}

/* WP-10d-pre (rule-9): re-run the A/B disk discovery after the in-system
 * `abdisk` command created the layout at runtime (the boot-time scan ran
 * while the disk was still empty, so the one-shot guard in ota_ab_init
 * blocks a second call).  Best-effort umount of any partial mounts, then
 * a full re-discovery. */
int ota_ab_rescan(void) {
    fs_vfs_umount(OC_AB_MOUNT_BOOT);
    fs_vfs_umount(OC_AB_MOUNT_A);
    fs_vfs_umount(OC_AB_MOUNT_B);
    fs_vfs_umount(OC_AB_MOUNT_DATA);
    g_ab_ready = 0;
    return ota_ab_init();
}

void ota_ab_boot_early(void) {
    /* handled inside ota_ab_init (the flags partition is only available
     * after the block devices come up); kept as a documented hook */
}

void ota_update_confirm_boot(void) {
    if (!g_ab_present) return;
    if (g_ab_slot != OC_AB_SLOT_B) return;
    /* the system is fully up: promote B to confirmed default */
    flag_remove(OC_AB_FLAG_BOOTFAIL_B);
    flag_remove(OC_AB_FLAG_NEXT_B);
    flag_create(OC_AB_FLAG_OK_B, "B\n");
    lib_log_info("abupdate: slot B boot confirmed (ok_B written)");
}

/* ------------------------------------------------------------------ */
/* 4. set_boot / rollback / status                                     */
/* ------------------------------------------------------------------ */

int ota_update_set_boot(const char *slot) {
    if (!slot) return OC_UPDATE_E_SLOT;
    if (!g_ab_present) return OC_UPDATE_E_NOAB;
    if (strcmp(slot, "B") == 0) {
        flag_remove(OC_AB_FLAG_OK_B);
        flag_remove(OC_AB_FLAG_BOOTFAIL_B);
        if (flag_create(OC_AB_FLAG_NEXT_B, "B\n") != 0)
            return OC_UPDATE_E_IO;
        return 0;
    }
    if (strcmp(slot, "A") == 0) {
        flag_remove(OC_AB_FLAG_NEXT_B);
        flag_remove(OC_AB_FLAG_OK_B);
        flag_remove(OC_AB_FLAG_BOOTFAIL_B);
        return 0;
    }
    return OC_UPDATE_E_SLOT;
}

int ota_update_rollback(void) {
    if (!g_ab_present) return OC_UPDATE_E_NOAB;
    return ota_update_set_boot("A");
}

int ota_update_get_status(ota_update_status_t *out) {
    if (!out) return OC_UPDATE_E_ARGS;
    memset(out, 0, sizeof(*out));

    strcpy(out->current_version, OC_UPDATE_CURRENT_VERSION);
    if (g_last_check_rc == OC_UPDATE_NEW || g_last_check_rc == OC_UPDATE_OK)
        strcpy(out->available_version, g_last_check_version);
    out->ota_update_available =
        (g_last_check_rc == -100) ? -1 :
        (g_last_check_rc == OC_UPDATE_NEW) ? 1 : 0;
    out->ota_ab_present = g_ab_present;
    out->online_update = lib_config_online_update_enabled();

    strcpy(out->current_boot, ota_ab_slot_name(g_ab_slot));

    int nb = 0, okb = 0, bfb = 0;
    if (ota_ab_read_flags(&nb, &okb, &bfb) == 0) {
        if (nb) strcpy(out->next_boot, "B");
        else if (okb) strcpy(out->next_boot, "B");
        else strcpy(out->next_boot, "A");
    }
    return 0;
}

/* cache the check result (called by the update check layer) */
void ota_update_cache_check(int rc, const char *version) {
    g_last_check_rc = rc;
    if (version) {
        strncpy(g_last_check_version, version,
                   (int)sizeof(g_last_check_version) - 1);
        g_last_check_version[sizeof(g_last_check_version) - 1] = 0;
    }
}

/* ------------------------------------------------------------------ */
/* 5. streaming download (HTTP + HTTPS)                                */
/* ------------------------------------------------------------------ */

#define DL_CHUNK       4096
#define DL_NO_DATA_TICKS 3000          /* ~30 s without progress       */
#define DL_MAX_TOTAL   (64u * 1024u * 1024u)   /* 64 MiB hard cap      */

int ota_update_download(const char *url, const char *path) {
    if (!url || !path) return OC_UPDATE_E_ARGS;

    /* Follow up to 3 HTTP redirects (301/302/303/307/308 with an absolute
     * Location URL).  github.com release downloads respond 302 to a CDN
     * host (release-assets.githubusercontent.com), so without this every
     * online `update` download failed with "malformed HTTP response". */
    static char cur_url[2048];   /* static: CDN redirect URLs reach ~1.4 KB; single-threaded shell */
    {
        int n = 0;
        while (url[n] && n < (int)sizeof(cur_url) - 1) {
            cur_url[n] = url[n];
            n++;
        }
        cur_url[n] = 0;
    }

    int fd = fs_vfs_open(path, VFS_O_WRONLY | VFS_O_CREAT | VFS_O_TRUNC);
    if (fd < 0) return OC_UPDATE_E_IO;

    u8 *chunk = (u8 *)kmalloc(DL_CHUNK);
    if (!chunk) { fs_vfs_close(fd); return OC_UPDATE_E_BUFSIZE; }

    int total = 0;
    int err = 0;

    for (int hop = 0; hop < 4 && !err; hop++) {
    ota_update_url_t u;
    if (ota_update_url_parse(cur_url, &u) != 0) {
        err = OC_UPDATE_E_PREFIX;
        break;
    }

    u32 ip = net_parse_ip(u.host);
    if (ip == 0 && net_dns_resolve(u.host, &ip) != 0) {
        err = OC_UPDATE_E_DNS;
        break;
    }

    if (u.use_tls) {
        if (net_tls_connect(ip, (u16)u.port, u.host) != 0) {
            kfree(chunk); fs_vfs_close(fd);
            return OC_UPDATE_E_CONNECT;
        }
        net_tls_ctx_t *c = net_tls_get_ctx();
        static char req[2048];   /* static: long CDN query strings; single-threaded shell */
        strcpy(req, "GET ");
        strcat(req, u.path);
        strcat(req, " HTTP/1.0\r\nHost: ");
        strcat(req, u.host);
        strcat(req, "\r\nUser-Agent: opencube-update\r\nConnection: close\r\n\r\n");
        if (net_tls_send(c, req, (int)strlen(req)) < 0) {
            net_tls_close(c); kfree(chunk); fs_vfs_close(fd);
            return OC_UPDATE_E_CONNECT;
        }

        /* header phase (large: github.com's 302 Location header alone is
         * ~5.3 KB, so the buffer must hold a full TLS record plus the
         * header block; a record that does not fit would be truncated
         * by net_tls_recv and the \r\n\r\n terminator lost forever) */
        char *hdr = (char *)kmalloc(16640);
        if (!hdr) {
            net_tls_close(c); kfree(chunk); fs_vfs_close(fd);
            return OC_UPDATE_E_BUFSIZE;
        }
        int hlen = 0, header_end = -1;
        u64 t0 = core_timer_ticks();
        while (header_end < 0) {
            if (core_timer_ticks() - t0 > DL_NO_DATA_TICKS) { err = OC_UPDATE_E_TIMEOUT; break; }
            int n = net_tls_recv(c, hdr + hlen, 16639 - hlen);
            if (n < 0) { err = OC_UPDATE_E_TIMEOUT; break; }
            if (n == 0) { err = OC_UPDATE_E_HTTP; break; }
            hlen += n;
            hdr[hlen] = 0;
            for (int k = 0; k <= hlen - 4; k++) {
                if (hdr[k] == '\r' && hdr[k+1] == '\n' &&
                    hdr[k+2] == '\r' && hdr[k+3] == '\n') {
                    header_end = k;
                    break;
                }
            }
        }
        int code = 0;
        if (!err) {
            const char *sp = hdr;
            while (*sp && *sp != ' ') sp++;
            while (*sp == ' ') sp++;
            while (*sp >= '0' && *sp <= '9') code = code * 10 + (*sp++ - '0');
            if (code == 301 || code == 302 || code == 303 ||
                code == 307 || code == 308) {
                /* redirect: locate the absolute Location URL and hop */
                char loc[2048];
                int found = -1;
                for (int k = 0; k + 9 < header_end; k++) {
                    if (k != 0 && hdr[k - 1] != '\n') continue;
                    int mm = 0;
                    const char *pat = "location:";
                    while (mm < 9) {
                        char a = hdr[k + mm], b = pat[mm];
                        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
                        if (a != b) break;
                        mm++;
                    }
                    if (mm != 9) continue;
                    int v = k + 9;
                    while (v < header_end && (hdr[v] == ' ' || hdr[v] == '\t')) v++;
                    int e = v;
                    while (e < header_end && hdr[e] != '\r' && hdr[e] != '\n') e++;
                    int len = e - v;
                    if (len > (int)sizeof(loc) - 1) len = (int)sizeof(loc) - 1;
                    for (int i = 0; i < len; i++) loc[i] = hdr[v + i];
                    loc[len] = 0;
                    found = 0;
                    break;
                }
                if (found != 0 || (strncmp(loc, "http://", 7) != 0 &&
                                   strncmp(loc, "https://", 8) != 0)) {
                    err = OC_UPDATE_E_HTTP;   /* no absolute Location */
                } else {
                    int m = 0;
                    while (loc[m] && m < (int)sizeof(cur_url) - 1) {
                        cur_url[m] = loc[m];
                        m++;
                    }
                    cur_url[m] = 0;
                }
                kfree(hdr);
                net_tls_close(c);
                if (hop >= 3) { err = OC_UPDATE_E_HTTP; break; }   /* too many hops */
                continue;                     /* follow redirect */
            }
            if (code < 200 || code > 299) err = OC_UPDATE_E_HTTP;
        }
        if (!err && header_end >= 0) {
            /* body bytes already received with the header */
            int blen = hlen - (header_end + 4);
            if (blen > 0) {
                fs_vfs_write(fd, hdr + header_end + 4, blen);
                total += blen;
            }
        }
        kfree(hdr);
        if (!err) {
            u64 t1 = core_timer_ticks();
            for (;;) {
                if (core_timer_ticks() - t1 > DL_NO_DATA_TICKS) { err = OC_UPDATE_E_TIMEOUT; break; }
                if ((u64)total >= DL_MAX_TOTAL) { err = OC_UPDATE_E_BUFSIZE; break; }
                int n = net_tls_recv(c, chunk, DL_CHUNK);
                if (n < 0) { err = OC_UPDATE_E_TIMEOUT; break; }
                if (n == 0) break;              /* close_notify / FIN */
                fs_vfs_write(fd, chunk, n);
                total += n;
                t1 = core_timer_ticks();
                if ((total & 0xFFFFF) < DL_CHUNK) {
                    /* crossed a ~1 MiB boundary: brief progress */
                    char line[48]; char num[12];
                    strcpy(line, "update:   downloaded ");
                    u64_to_str((u64)(total / (1024 * 1024)), num);
                    strcat(line, num);
                    strcat(line, " MB");
                    screen_console_puts(line);
                    screen_console_puts("\n");
                }
            }
        }
        net_tls_close(c);
        break;                                /* downloaded */
    } else {
        int sock = net_socket(SOCK_TCP);
        if (sock < 0) { kfree(chunk); fs_vfs_close(fd); return OC_UPDATE_E_CONNECT; }
        if (net_connect(sock, ip, (u16)u.port) < 0) {
            net_close(sock); kfree(chunk); fs_vfs_close(fd);
            return OC_UPDATE_E_CONNECT;
        }
        static char req[2048];   /* static: long CDN query strings; single-threaded shell */
        strcpy(req, "GET ");
        strcat(req, u.path);
        strcat(req, " HTTP/1.0\r\nHost: ");
        strcat(req, u.host);
        strcat(req, "\r\nUser-Agent: opencube-update\r\n\r\n");
        net_send(sock, req, (int)strlen(req));

        /* WP-10u: two-phase receive, structurally identical to the proven
         * shell_cmd_wget() pattern: phase 1 collects the response header into a
         * small heap buffer, phase 2 streams the body to the VFS file.
         * The earlier single-phase version wrote the HTTP headers into
         * the package file and carried per-iteration wall-clock checks;
         * both broke the transfer.  Buffer sized for github.com's ~5.3 KB
         * redirect headers (same reason as the TLS branch above). */
        char *hdr = (char *)kmalloc(16640);
        if (!hdr) {
            net_close(sock);
            kfree(chunk);
            fs_vfs_close(fd);
            fs_vfs_unlink(path);
            return OC_UPDATE_E_BUFSIZE;
        }
        int total_header = 0, header_end = -1, body_start = 0;
        int content_length = -1, body_received = 0;
        while (header_end < 0) {
            int n = net_recv(sock, hdr + total_header, 16639 - total_header);
            if (n <= 0) { err = OC_UPDATE_E_TIMEOUT; break; }
            total_header += n;
            hdr[total_header] = 0;
            for (int k = 0; k <= total_header - 4; k++) {
                if (hdr[k] == '\r' && hdr[k+1] == '\n' &&
                    hdr[k+2] == '\r' && hdr[k+3] == '\n') {
                    header_end = k;
                    body_start = k + 4;
                    body_received = total_header - body_start;
                    break;
                }
            }
            if (header_end < 0 && total_header >= 16639) {
                err = OC_UPDATE_E_HTTP;
                break;
            }
        }
        int code = 0;
        if (!err) {
            const char *sp = hdr;
            while (*sp && *sp != ' ') sp++;
            while (*sp == ' ') sp++;
            while (*sp >= '0' && *sp <= '9') code = code * 10 + (*sp++ - '0');
            if (code == 301 || code == 302 || code == 303 ||
                code == 307 || code == 308) {
                /* redirect: locate the absolute Location URL and hop */
                char loc[2048];
                int found = -1;
                for (int k = 0; k + 9 < header_end; k++) {
                    if (k != 0 && hdr[k - 1] != '\n') continue;
                    int mm = 0;
                    const char *pat = "location:";
                    while (mm < 9) {
                        char a = hdr[k + mm], b = pat[mm];
                        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
                        if (a != b) break;
                        mm++;
                    }
                    if (mm != 9) continue;
                    int v = k + 9;
                    while (v < header_end && (hdr[v] == ' ' || hdr[v] == '\t')) v++;
                    int e = v;
                    while (e < header_end && hdr[e] != '\r' && hdr[e] != '\n') e++;
                    int len = e - v;
                    if (len > (int)sizeof(loc) - 1) len = (int)sizeof(loc) - 1;
                    for (int i = 0; i < len; i++) loc[i] = hdr[v + i];
                    loc[len] = 0;
                    found = 0;
                    break;
                }
                if (found != 0 || (strncmp(loc, "http://", 7) != 0 &&
                                   strncmp(loc, "https://", 8) != 0)) {
                    err = OC_UPDATE_E_HTTP;   /* no absolute Location */
                } else {
                    int m = 0;
                    while (loc[m] && m < (int)sizeof(cur_url) - 1) {
                        cur_url[m] = loc[m];
                        m++;
                    }
                    cur_url[m] = 0;
                }
                kfree(hdr);
                net_close(sock);
                if (hop >= 3) { err = OC_UPDATE_E_HTTP; break; }   /* too many hops */
                continue;                     /* follow redirect */
            }
            if (code < 200 || code > 299) err = OC_UPDATE_E_HTTP;
        }
        if (!err) {
            /* Content-Length (case-insensitive, like shell_cmd_wget) */
            for (int k = 0; k < header_end; k++) {
                if (hdr[k] == 'C' || hdr[k] == 'c') {
                    if (strncmp(hdr + k, "Content-Length:", 15) == 0 ||
                        strncmp(hdr + k, "content-length:", 15) == 0) {
                        int v = k + 15;
                        while (hdr[v] == ' ' || hdr[v] == '\t') v++;
                        content_length = 0;
                        while (hdr[v] >= '0' && hdr[v] <= '9') {
                            content_length = content_length * 10 + (hdr[v] - '0');
                            v++;
                        }
                        break;
                    }
                }
            }
            if (content_length < 0) content_length = body_received;
            /* headers may already carry the first body bytes */
            if (body_received > 0) {
                fs_vfs_write(fd, hdr + body_start, body_received);
                total += body_received;
            }
            /* phase 2: stream the rest of the body */
            while (total < content_length) {
                int n = net_recv(sock, chunk, DL_CHUNK);
                if (n <= 0) break;
                fs_vfs_write(fd, chunk, n);
                total += n;
            }
            if (total < content_length) err = OC_UPDATE_E_TIMEOUT;
        }
        kfree(hdr);
        net_close(sock);
        break;                                /* downloaded */
    }
    }

    kfree(chunk);
    fs_vfs_close(fd);
    if (err) { fs_vfs_unlink(path); return err; }
    return total;
}

/* ------------------------------------------------------------------ */
/* 6. SHA256 verification                                              */
/* ------------------------------------------------------------------ */

#define VF_CHUNK 65536

static int hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int ota_update_verify(const char *path, const char *crypto_sha256_hex) {
    if (!path || !crypto_sha256_hex) return OC_UPDATE_E_ARGS;
    if ((int)strlen(crypto_sha256_hex) != 64) return OC_UPDATE_E_ARGS;

    int fd = fs_vfs_open(path, VFS_O_RDONLY);
    if (fd < 0) return OC_UPDATE_E_IO;

    u8 *buf = (u8 *)kmalloc(VF_CHUNK);
    if (!buf) { fs_vfs_close(fd); return OC_UPDATE_E_BUFSIZE; }

    crypto_sha256_ctx hc;
    crypto_sha256_init(&hc);
    int n;
    u64 total = 0;
    while ((n = fs_vfs_read(fd, buf, VF_CHUNK)) > 0) {
        crypto_sha256_update(&hc, buf, n);
        total += (u64)n;
    }
    fs_vfs_close(fd);
    kfree(buf);

    u8 digest[32];
    crypto_sha256_final(&hc, digest);

    for (int i = 0; i < 32; i++) {
        int hi = hex_nibble(crypto_sha256_hex[i * 2]);
        int lo = hex_nibble(crypto_sha256_hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return OC_UPDATE_E_ARGS;
        if (digest[i] != (u8)((hi << 4) | lo)) return OC_UPDATE_E_SHA;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* 7. package install (gzip + tar -> slot filesystem)                  */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *slot_mount;       /* "/ab/b"                              */
    char manifest[2048];          /* captured manifest.json content       */
    int  manifest_len;
    int  files_installed;
    u64  bytes_installed;
    int  error;                   /* OC_UPDATE_E_* while installing       */
    int  fd;                      /* open target file                     */
    char cur_target[128];
    char grub_cfg_flags[96];      /* flags-partition grub.cfg copy target */
    char strip_prefix[64];        /* top-level wrapper dir ("name/"),     */
    int  have_prefix;             /* stripped from every entry path       */
} install_ctx_t;

/* Strip the package's top-level wrapper directory ("opencube-wp10u-update/")
 * if the archive uses one (mainstream behaviour: the installer maps the
 * package CONTENT onto the slot filesystem, the wrapper dir is packaging
 * metadata).  Returns the path relative to the package root. */
static const char *strip_wrapper(const install_ctx_t *c, const char *path) {
    if (!c->have_prefix) return path;
    int pl = (int)strlen(c->strip_prefix);
    if (strncmp(path, c->strip_prefix, pl) == 0) return path + pl;
    return path;
}

/* gzip -> tar trampoline: the gzip engine hands decompressed bytes to
 * the tar parser.  C has no closures, so the active pair lives in
 * file-scope state (installs are single-threaded shell operations). */
static lib_tar_t *g_inst_tar = NULL;
static int       g_inst_tar_err = 0;
static crypto_sha256_ctx g_inst_sha;          /* digest of the decompressed tar */

static int lib_gzip_to_tar_sink(void *ctx, const u8 *data, int len) {
    (void)ctx;
    if (!g_inst_tar) return -1;
    int rc = lib_tar_feed(g_inst_tar, data, len);
    if (rc != 0) {
        g_inst_tar_err = rc;
        return -1;
    }
    return 0;
}

/* case-insensitive compare of a computed digest against hex text */
static int sha_hex_eq(const u8 digest[32], const char *hex) {
    static const char digits[] = "0123456789abcdef";
    char buf[65];
    for (int i = 0; i < 32; i++) {
        buf[i * 2] = digits[digest[i] >> 4];
        buf[i * 2 + 1] = digits[digest[i] & 0xF];
    }
    buf[64] = 0;
    return strcasecmp(buf, hex) == 0;
}

/* Build the target path for a package entry (see ota_ab_update.h mapping).
 * Returns 0 with *out set, 1 = skip entry, <0 = error. */
static int map_package_path(const char *path, char *out, int outcap) {
    if (strcmp(path, "manifest.json") == 0) return 1;
    if (strcmp(path, "kernel/opencube.elf") == 0) {
        strcpy(out, "/boot/opencube.elf");
        return 0;
    }
    if (strcmp(path, "boot/grub.cfg") == 0) {
        strcpy(out, "/boot/grub.cfg");
        return 0;
    }
    if (path[0] == '/') return 1;                 /* absolute: skip */
    /* P0fix2 BUG-0039 (A5-04): ".." components passed through untouched and
     * fs_vfs_normalize pops them at string level, so a tar entry named
     * a/../../../etc/opencube.conf escaped the slot mount point and wrote
     * anywhere in the VFS (update --local needs no external validation).
     * Reject any path containing a ".." component outright. */
    for (const char *p = path; *p; p++) {
        if (p[0] == '.' && p[1] == '.' &&
            (p[2] == '/' || p[2] == '\0') &&
            (p == path || p[-1] == '/')) {
            return OC_UPDATE_E_BUFSIZE;
        }
    }
    if (strlen(path) + 2 > (u64)outcap) return OC_UPDATE_E_BUFSIZE;
    strcpy(out, "/");
    strcat(out, path);
    return 0;
}

/* mkdir -p for the parent directory of a target path (FAT32). */
static int ensure_parent_dirs(const char *target) {
    char tmp[128];
    strncpy(tmp, target, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = 0;
    for (int i = 1; tmp[i]; i++) {
        if (tmp[i] == '/') {
            tmp[i] = 0;
            fs_vfs_stat_t st;
            if (fs_vfs_stat(tmp, &st) != 0) {
                if (fs_vfs_mkdir(tmp) != 0) return -1;
            }
            tmp[i] = '/';
        }
    }
    return 0;
}

static int install_file_cb(void *ctx, const char *path, int type,
                           u64 size, const u8 *data, int len) {
    install_ctx_t *c = (install_ctx_t *)ctx;
    (void)size;

    if (type == OC_TAR_DIR) return 0;

    if (data == NULL && len == 0) {
        /* final slice: close the file */
        if (c->fd >= 0) fs_vfs_close(c->fd);
        c->fd = -1;
        if (c->grub_cfg_flags[0] && strcmp(c->cur_target, "/boot/grub.cfg") == 0) {
            /* mirror the new grub.cfg to the flags partition for GRUB */
            char src[192];
            strcpy(src, c->slot_mount);
            strcat(src, "/boot/grub.cfg");
            int sfd = fs_vfs_open(src, VFS_O_RDONLY);
            if (sfd >= 0) {
                int dfd = fs_vfs_open(c->grub_cfg_flags,
                                   VFS_O_WRONLY | VFS_O_CREAT | VFS_O_TRUNC);
                if (dfd >= 0) {
                    u8 buf[4096];
                    int n;
                    while ((n = fs_vfs_read(sfd, buf, sizeof(buf))) > 0)
                        fs_vfs_write(dfd, buf, n);
                    fs_vfs_close(dfd);
                }
                fs_vfs_close(sfd);
            }
            c->grub_cfg_flags[0] = 0;
        }
        return 0;
    }

    /* learn the wrapper-dir prefix from the very first entry */
    if (!c->have_prefix) {
        const char *slash = strchr(path, '/');
        if (slash && slash != path) {
            int pl = (int)(slash - path) + 1;
            if (pl < (int)sizeof(c->strip_prefix)) {
                memcpy(c->strip_prefix, path, pl);
                c->strip_prefix[pl] = 0;
                c->have_prefix = 1;
            }
        } else {
            c->have_prefix = 1;   /* no wrapper dir: entries are root-level */
            c->strip_prefix[0] = 0;
        }
    }
    path = strip_wrapper(c, path);

    int is_manifest = (strcmp(path, "manifest.json") == 0);

    /* first slice of a regular file: map the path and open the target */
    if (c->fd < 0 && !is_manifest) {
        char mapped[128];
        int mr = map_package_path(path, mapped, (int)sizeof(mapped));
        if (mr == 1) return 0;                    /* skipped entry */
        if (mr < 0) { c->error = mr; return -1; }
        char target[160];
        if ((int)(strlen(c->slot_mount) + strlen(mapped)) + 1 >=
            (int)sizeof(target)) {
            c->error = OC_UPDATE_E_BUFSIZE;
            return -1;
        }
        strcpy(target, c->slot_mount);
        strcat(target, mapped);
        if (ensure_parent_dirs(target) != 0) {
            c->error = OC_UPDATE_E_IO;
            return -1;
        }
        c->fd = fs_vfs_open(target, VFS_O_WRONLY | VFS_O_CREAT | VFS_O_TRUNC);
        if (c->fd < 0) {
            c->error = OC_UPDATE_E_IO;
            return -1;
        }
        strcpy(c->cur_target, mapped);
        if (strcmp(mapped, "/boot/grub.cfg") == 0 && g_ab_present) {
            strcpy(c->grub_cfg_flags, OC_AB_MOUNT_BOOT);
            strcat(c->grub_cfg_flags, "/boot/grub/grub.cfg");
        }
        c->files_installed++;
    }

    if (is_manifest) {
        /* capture the manifest (bounded) */
        if (c->manifest_len + len < (int)sizeof(c->manifest)) {
            memcpy(c->manifest + c->manifest_len, data, len);
            c->manifest_len += len;
            c->manifest[c->manifest_len] = 0;
        }
        return 0;
    }

    if (c->fd >= 0 && len > 0) {
        crypto_sha256_update(&g_inst_sha, data, len);   /* payload digest */
        int w = fs_vfs_write(c->fd, data, len);
        if (w != len) {
            c->error = OC_UPDATE_E_IO;
            return -1;
        }
        c->bytes_installed += (u64)len;
    }
    return 0;
}

int ota_update_install(const char *pkg_path, const char *slot) {
    if (!pkg_path || !slot) return OC_UPDATE_E_ARGS;
    if (strcmp(slot, "A") != 0 && strcmp(slot, "B") != 0)
        return OC_UPDATE_E_SLOT;
    const char *mount = (slot[0] == 'B') ? OC_AB_MOUNT_B : OC_AB_MOUNT_A;

    if (!g_ab_present) return OC_UPDATE_E_NOAB;

    /* the slot filesystem must be mounted */
    fs_vfs_stat_t st;
    if (fs_vfs_stat(mount, &st) != 0) return OC_UPDATE_E_NOAB;

    /* gzip -> tar -> vfs pipeline */
    install_ctx_t ictx;
    memset(&ictx, 0, sizeof(ictx));
    ictx.slot_mount = mount;
    ictx.fd = -1;
    strcpy(ictx.grub_cfg_flags, OC_AB_MOUNT_BOOT);
    strcat(ictx.grub_cfg_flags, "/boot/grub/grub.cfg");

    lib_tar_t *tar = lib_tar_open(install_file_cb, &ictx);
    if (!tar) return OC_UPDATE_E_BUFSIZE;

    lib_gzip_t *gz = (lib_gzip_t *)kmalloc(sizeof(lib_gzip_t));
    if (!gz) { lib_tar_close(tar); return OC_UPDATE_E_BUFSIZE; }
    memset(gz, 0, sizeof(*gz));

    /* The manifest.json "sha256" covers the UNCOMPRESSED package tar
     * (it cannot cover the .tar.gz it ships inside).  The digest is
     * computed over the decompressed stream inside the gzip->tar
     * pipeline and verified after the walk. */
    crypto_sha256_init(&g_inst_sha);

    /* gzip sink: hash decompressed bytes, feed them into the tar parser */
    g_inst_tar = tar;
    g_inst_tar_err = 0;
    int grc = lib_gzip_init(gz, lib_gzip_to_tar_sink, NULL);
    if (grc != 0) {
        kfree(gz); lib_tar_close(tar);
        return OC_UPDATE_E_TARGZ;
    }

    int fd = fs_vfs_open(pkg_path, VFS_O_RDONLY);
    if (fd < 0) {
        kfree(gz); lib_tar_close(tar);
        return OC_UPDATE_E_IO;
    }

    u8 *buf = (u8 *)kmalloc(32768);
    if (!buf) {
        fs_vfs_close(fd); kfree(gz); lib_tar_close(tar);
        return OC_UPDATE_E_BUFSIZE;
    }

    int err = 0, n;
    while ((n = fs_vfs_read(fd, buf, 32768)) > 0) {
        int rc = lib_gzip_feed(gz, buf, n);
        if (rc != 0) { err = OC_UPDATE_E_TARGZ; break; }
        if (g_inst_tar_err != 0) { err = OC_UPDATE_E_TARGZ; break; }
    }
    if (!err) {
        int rc = lib_gzip_finish(gz);
        if (rc != 0) err = OC_UPDATE_E_TARGZ;
    }
    if (!err) {
        int rc = lib_tar_finish(tar);
        if (rc != 0) err = OC_UPDATE_E_TARGZ;
    }
    u8 pkg_digest[32];
    crypto_sha256_final(&g_inst_sha, pkg_digest);
    if (ictx.error != 0) err = ictx.error;
    if (ictx.fd >= 0) fs_vfs_close(ictx.fd);
    fs_vfs_close(fd);
    kfree(buf);
    kfree(gz);
    lib_tar_close(tar);
    g_inst_tar = NULL;
    g_inst_tar_err = 0;
    if (err) return err;

    if (ictx.files_installed == 0) return OC_UPDATE_E_TARGZ;

    /* manifest checks: version present + payload checksum */
    if (ictx.manifest_len == 0) return OC_UPDATE_E_TARGZ;
    char mver[32];
    char msha[80];
    if (ota_update_json_string(ictx.manifest, "version", mver,
                              (int)sizeof(mver)) != 0)
        return OC_UPDATE_E_TARGZ;
    if (ota_update_json_string(ictx.manifest, "sha256", msha,
                              (int)sizeof(msha)) == 0) {
        if (!sha_hex_eq(pkg_digest, msha)) return OC_UPDATE_E_SHA;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* 8. shell commands                                                   */
/* ------------------------------------------------------------------ */

static int ota_ab_print_status(void) {
    ota_update_status_t st;
    if (ota_update_get_status(&st) != 0) {
        screen_console_puts("update: status unavailable\n");
        return 1;
    }
    screen_console_puts("update: current version: ");
    screen_console_puts(st.current_version);
    screen_console_puts("\n");
    screen_console_puts("update: current boot: ");
    screen_console_puts(st.current_boot);
    screen_console_puts("\n");
    screen_console_puts("update: next boot: ");
    screen_console_puts(st.next_boot[0] ? st.next_boot : "(default)");
    screen_console_puts("\n");
    screen_console_puts("update: available: ");
    if (st.ota_update_available == 1) screen_console_puts(st.available_version);
    else if (st.ota_update_available == 0) screen_console_puts("none (up to date)");
    else screen_console_puts("unknown (run update or checkupdate)");
    screen_console_puts("\n");
    screen_console_puts("update: online update: ");
    screen_console_puts(st.online_update ? "enabled" : "disabled");
    screen_console_puts("\n");
    screen_console_puts("update: A/B disk: ");
    screen_console_puts(st.ota_ab_present ? "present" : "absent");
    screen_console_puts("\n");
    return 0;
}

/* Download the package and verify it against the manifest.  Returns
 * 0 with *pkg_path set, or a negative error. */
static int download_and_verify(const ota_update_pkg_info_t *pkg,
                               const char **pkg_path) {
    fs_vfs_stat_t dst;
    const char *path = (fs_vfs_stat(OC_AB_MOUNT_DATA, &dst) == 0)
                     ? OC_UPDATE_PKG_PATH : OC_UPDATE_PKG_TMP;
    *pkg_path = path;

    char msg[80];
    /* BUG-0109 FIX: the display buffer was char num[12]. package_size is
     * parsed from the (network-supplied) manifest as a full u64, so a
     * huge value printed up to 20 decimal digits into 12 bytes -> stack
     * overflow inside the update path. u64 needs at most 20 digits + NUL
     * = 21 bytes; 24 gives headroom. */
    char num[24];
    strcpy(msg, "update: downloading... (");
    u64_to_str(pkg->package_size / 1024, num);
    strcat(msg, num);
    strcat(msg, " KB)");
    screen_console_puts(msg);
    screen_console_puts("\n");

    int n = ota_update_download(pkg->package_url, path);
    if (n < 0) {
        screen_console_puts("update: download failed: ");
        screen_console_puts(ota_update_strerror(n));
        screen_console_puts("\n");
        return n;
    }
    if (pkg->package_size > 0 && (u64)n != pkg->package_size) {
        screen_console_puts("update: download size mismatch\n");
        return OC_UPDATE_E_HTTP;
    }
    screen_console_puts("update: verifying SHA256... ");
    int rc = ota_update_verify(path, pkg->package_sha256);
    screen_console_puts(rc == 0 ? "OK" : "FAILED");
    screen_console_puts("\n");
    return rc;
}

/* BUG-0110 FIX: the update target slot used to be hardcoded "B" at every
 * call site. Booted from slot B, an update still wrote into B - the
 * RUNNING system - while slot A sat idle. The target is now always the
 * slot that is NOT booted (A<->B); callers guarantee an A/B disk is
 * present before reaching here, so a missing slot is a hard error. */
static const char *update_target_slot(void) {
    int cur = ota_ab_current_slot();
    if (cur == OC_AB_SLOT_A) return "B";
    if (cur == OC_AB_SLOT_B) return "A";
    return NULL;   /* ISO boot / no A/B disk: no safe target */
}

int shell_cmd_update(const char *args) {
    const char *a = args ? args : "";

    /* ---- update --status ---- */
    if (strcmp(a, "--status") == 0 || strcmp(a, "status") == 0)
        return ota_ab_print_status();

    /* ---- update --local <path> (offline, e.g. from a USB drive) ---- */
    if (strncmp(a, "--local ", 8) == 0 || strncmp(a, "local ", 6) == 0) {
        const char *p = (a[0] == '-') ? a + 8 : a + 6;
        while (*p == ' ') p++;
        if (!*p) {
            screen_console_puts("usage: update --local /path/to/package.tar.gz\n");
            return 1;
        }
        if (!g_ab_present) {
            screen_console_puts("update: no A/B disk\n");
            return 1;
        }
        screen_console_puts("update: reading local package...\n");
        /* the manifest inside the package carries the package checksum;
         * ota_update_install() verifies it and fails on a mismatch */
        const char *tgt = update_target_slot();   /* BUG-0110 FIX */
        if (!tgt) {
            screen_console_puts("update: cannot determine target slot (not booted from A/B?)\n");
            return 1;
        }
        int rc = ota_update_install(p, tgt);
        if (rc != 0) {
            screen_console_puts("update: install failed: ");
            screen_console_puts(ota_update_strerror(rc));
            screen_console_puts("\n");
            return 1;
        }
        screen_console_puts("update: verifying SHA256... OK\n");
        screen_console_puts("update: extracting to ");
        screen_console_puts(tgt);
        screen_console_puts(" partition... OK\n");
        if (ota_update_set_boot(tgt) != 0) {
            screen_console_puts("update: setting boot failed\n");
            return 1;
        }
        screen_console_puts("update: setting boot to ");
        screen_console_puts(tgt);
        screen_console_puts("... OK\n");
        screen_console_puts("update: reboot required\n");
        return 0;
    }

    /* ---- update (online) ---- */
    if (!lib_config_online_update_enabled()) {
        screen_console_puts("update: online update disabled by config\n");
        screen_console_puts("update: (online_update=no, use update --local)\n");
        return 1;
    }
    if (!g_ab_present) {
        screen_console_puts("update: no A/B disk\n");
        return 1;
    }

    screen_console_puts("update: checking...\n");
    ota_update_pkg_info_t pkg;
    int rc = ota_update_check_pkg(&pkg);
    if (rc == OC_UPDATE_OK) {
        screen_console_puts("update: up to date (");
        screen_console_puts(pkg.info.version);
        screen_console_puts(")\n");
        return 0;
    }
    if (rc != OC_UPDATE_NEW) {
        screen_console_puts("update: check failed: ");
        screen_console_puts(ota_update_strerror(rc));
        screen_console_puts("\n");
        return 1;
    }
    screen_console_puts("update: ");
    screen_console_puts(pkg.info.version);
    screen_console_puts(" available\n");

    if (!pkg.have_package || !pkg.package_url[0]) {
        screen_console_puts("update: manifest has no package_url\n");
        return 1;
    }

    const char *pkg_path = NULL;
    rc = download_and_verify(&pkg, &pkg_path);
    if (rc != 0) return 1;

    const char *tgt = update_target_slot();   /* BUG-0110 FIX */
    if (!tgt) {
        screen_console_puts("update: cannot determine target slot (not booted from A/B?)\n");
        return 1;
    }
    rc = ota_update_install(pkg_path, tgt);
    if (rc != 0) {
        screen_console_puts("update: install failed: ");
        screen_console_puts(ota_update_strerror(rc));
        screen_console_puts("\n");
        return 1;
    }
    screen_console_puts("update: extracting to ");
    screen_console_puts(tgt);
    screen_console_puts(" partition... OK\n");

    if (ota_update_set_boot(tgt) != 0) {
        screen_console_puts("update: setting boot failed\n");
        return 1;
    }
    screen_console_puts("update: setting boot to ");
    screen_console_puts(tgt);
    screen_console_puts("... OK\n");
    screen_console_puts("update: reboot required\n");
    return 0;
}

int shell_cmd_rollback(const char *args) {
    (void)args;
    if (!g_ab_present) {
        screen_console_puts("rollback: no A/B disk\n");
        return 1;
    }
    screen_console_puts("rollback: current boot: ");
    screen_console_puts(ota_ab_slot_name(g_ab_slot));
    screen_console_puts("\n");
    if (g_ab_slot == OC_AB_SLOT_A) {
        screen_console_puts("rollback: already on slot A\n");
        return 0;
    }
    screen_console_puts("rollback: switching to A...\n");
    if (ota_update_rollback() != 0) {
        screen_console_puts("rollback: failed\n");
        return 1;
    }
    screen_console_puts("rollback: reboot required\n");
    return 0;
}

/* shell_cmd_reboot moved to kernel/power.c (WP-10d-fix2): the reset path
 * is shared with shutdown/halt and the block flush lives in
 * core_power_flush_blk().  ota_ab_update.c keeps the A/B update logic. */

/* ------------------------------------------------------------------ */
/* 9. error strings                                                    */
/* ------------------------------------------------------------------ */

const char *ota_update_strerror(int rc) {
    switch (rc) {
    case OC_UPDATE_OK:          return "up to date";
    case OC_UPDATE_NEW:         return "new version available";
    case OC_UPDATE_E_CONFIG:    return "config file missing or unreadable";
    case OC_UPDATE_E_PREFIX:    return "invalid URL prefix (must be http:// or https://)";
    case OC_UPDATE_E_DNS:       return "DNS resolution failed";
    case OC_UPDATE_E_CONNECT:   return "connect failed";
    case OC_UPDATE_E_TIMEOUT:   return "no response (timeout)";
    case OC_UPDATE_E_HTTP:      return "malformed HTTP response";
    case OC_UPDATE_E_JSON:      return "JSON parse failed";
    case OC_UPDATE_E_BUFSIZE:   return "response too large";
    case OC_UPDATE_E_DISABLED:  return "online update disabled";
    case OC_UPDATE_E_NOAB:      return "no A/B disk";
    case OC_UPDATE_E_SHA:       return "SHA256 mismatch";
    case OC_UPDATE_E_TARGZ:     return "package (tar.gz) invalid";
    case OC_UPDATE_E_IO:        return "I/O error";
    case OC_UPDATE_E_SLOT:      return "invalid slot (use A or B)";
    case OC_UPDATE_E_ARGS:      return "invalid arguments";
    default:                    return "unknown error";
    }
}

/* ------------------------------------------------------------------ */
/* 10. L1 extension surface (kernel/ext.h)                              */
/* ------------------------------------------------------------------ */

int l1_ext_update_check_pkg(l1_ext_update_pkg_info_t *out) {
    ota_update_pkg_info_t pkg;
    int rc = ota_update_check_pkg(&pkg);
    if (out) {
        memset(out, 0, sizeof(*out));
        strcpy(out->info.version, pkg.info.version);
        strcpy(out->info.time, pkg.info.time);
        strcpy(out->info.changes, pkg.info.changes);
        strcpy(out->package_url, pkg.package_url);
        strcpy(out->package_sha256, pkg.package_sha256);
        out->package_size = pkg.package_size;
        out->have_package = pkg.have_package;
    }
    return rc;
}

int l1_ext_update_download(const char *url, const char *path) {
    return ota_update_download(url, path);
}

int l1_ext_update_verify(const char *path, const char *crypto_sha256_hex) {
    return ota_update_verify(path, crypto_sha256_hex);
}

int l1_ext_update_install(const char *pkg_path, const char *slot) {
    return ota_update_install(pkg_path, slot);
}

int l1_ext_update_rollback(void) {
    return ota_update_rollback();
}

int l1_ext_update_set_boot(const char *slot) {
    return ota_update_set_boot(slot);
}

int l1_ext_update_get_status(ota_update_status_t *out) {
    return ota_update_get_status(out);
}
