/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS 3.0.0-WP03 G5
 * File: services/klog/klog_file.c
 * Purpose: the ON-DISK channel for the kernel log - the /var/log/
 *          kernel.log sink, verbatim from the pre-G5 lib_klog.c file
 *          channel.  The ring stays in core/lib (the mechanism); this
 *          module registers itself as the ring's emit hook (task
 *          context, after each publish) and syncs its own tail.
 */
#include "types.h"
#include "lib_klog.h"
#include "klog_file.h"
#include "fs_vfs.h"
#include "lib_string.h"
#include "lib_log.h"

#define KLOG_PATH          "/var/log/kernel.log"
/* KLOG_ROTATE_BYTES / KLOG_ROTATE_KEEP come from lib_klog.h (single
 * source of truth - the same defaults as the pre-G5 channel). */

static int  g_fd = -1;                 /* open O_APPEND fd, -1 = down */
static u64  g_bytes = 0;               /* bytes in the ACTIVE file    */
static u64  g_synced = 0;              /* absolute seq already appended */
static u32  g_dirty = 0;               /* ring tail not appended yet  */
static u64  g_rotate_bytes = KLOG_ROTATE_BYTES;
static u32  g_rotate_keep  = KLOG_ROTATE_KEEP;

static void kf_close(void) {
    if (g_fd >= 0) { fs_vfs_close(g_fd); g_fd = -1; }
}

/* shift generations: kernel.log -> .1 -> .2 -> ... (keep N) */
static int kf_shift(void) {
    char from[48], to[48];
    static const char *base = KLOG_PATH;

    if (g_rotate_keep >= 1) {
        strcpy(from, base); strcat(from, ".");
        u64_to_str((u64)g_rotate_keep, from + strlen(from));
        fs_vfs_unlink(from);
        for (u32 gen = g_rotate_keep; gen >= 2; gen--) {
            strcpy(from, base); strcat(from, ".");
            u64_to_str((u64)(gen - 1), from + strlen(from));
            strcpy(to, base); strcat(to, ".");
            u64_to_str((u64)gen, to + strlen(to));
            fs_vfs_rename(from, to);
            if (gen == 2) break;   /* guard: u32 would wrap below 2 */
        }
        strcpy(from, base);
        strcpy(to, base); strcat(to, ".1");
        fs_vfs_rename(from, to);
    }
    g_bytes = 0;
    return 0;
}

/* ensure /var/log + an open append fd */
static int kf_open(void) {
    if (g_fd >= 0) return g_fd;
    fs_vfs_mkdir("/var");
    fs_vfs_mkdir("/var/log");
    g_fd = fs_vfs_open(KLOG_PATH, VFS_O_WRONLY | VFS_O_CREAT | VFS_O_APPEND);
    if (g_fd >= 0) {
        fs_vfs_stat_t st;
        g_bytes = (fs_vfs_stat(KLOG_PATH, &st) == 0) ? st.size : 0;
    }
    return g_fd;
}

/* append every not-yet-synced ring entry.  TASK CONTEXT ONLY. */
static void kf_sync(void) {
    if (kf_open() < 0) return;

    u64 total = klog_total_seq();
    u64 from = g_synced;
    u64 oldest = klog_oldest_seq();
    if (from < oldest) from = oldest;   /* skip wrap-eaten entries */
    if (from >= total) { g_synced = total; g_dirty = 0; return; }

    char line[200];
    for (u64 seq = from; seq < total; seq++) {
        int len = klog_entry_render(&seq, line, sizeof(line));
        if (len < 0) break;
        int w = fs_vfs_write(g_fd, line, len);
        if (w < 0) break;               /* fs hiccup: retry next time */
        g_bytes += (u64)w;
        g_synced = seq + 1;
    }
    g_dirty = (g_synced < klog_total_seq()) ? 1 : 0;

    /* automatic rotation */
    if (g_bytes >= g_rotate_bytes) {
        kf_close();
        kf_shift();
        kf_open();
    }
}

/* the ring's emit hook (task context, after each publish) */
static void klog_file_hook(void) {
    kf_sync();
}

/* called by kmain after the first filesystem is up: dump the boot
 * log to /var/log/kernel.log (verbatim pre-G5 behaviour) */
void klog_on_mount(void) {
    klog_set_emit_hook(klog_file_hook);
    if (kf_open() >= 0) {
        kf_sync();
    }
}

/* file-channel reporting + rotation tuning (the API surface kept
 * from the command era; the L0 commands died with the user plane,
 * the accessors stay for the service surface) */
u64  klog_file_bytes(void) { return g_bytes; }
int  klog_file_open(void)  { return g_fd >= 0; }
u64  klog_set_rotate_bytes(u64 bytes) {
    u64 old = g_rotate_bytes;
    if (bytes >= 4096) g_rotate_bytes = bytes;
    return old;
}
u32  klog_set_rotate_keep(u32 keep) {
    u32 old = g_rotate_keep;
    if (keep >= 1 && keep <= 9) g_rotate_keep = keep;
    return old;
}
u64  klog_get_rotate_bytes(void) { return g_rotate_bytes; }
u32  klog_get_rotate_keep(void)  { return g_rotate_keep; }

/* explicit flush (task context only; the pre-G5 API, moved here) */
int klog_flush(void) {
    u64 flags;
    __asm__ volatile("pushfq; popq %0" : "=r"(flags) :: "memory");
    if (!(flags & 0x200)) return -1;     /* IRQ ctx: the timer/task will */
    kf_sync();
    return 0;
}

/* Panic-path variant: interrupts are OFF and this is the last chance -
 * sync unconditionally (crashlog_flush runs the same VFS writes from
 * the same depth, empirically reliable). */
int klog_flush_panic(void) {
    kf_sync();
    return 0;
}

int klog_rotate(void) {
    u64 flags;
    __asm__ volatile("pushfq; popq %0" : "=r"(flags) :: "memory");
    if (!(flags & 0x200)) return -1;
    kf_sync();                           /* never lose the pending tail */
    kf_close();
    if (kf_open() < 0 && g_fd < 0) return -1;   /* no fs */
    kf_close();
    kf_shift();
    kf_open();
    return 0;
}
