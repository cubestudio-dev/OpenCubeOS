/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP-l1apps-merge - L1 app "disk-tool" (the block
 * + filesystem mega-app).
 *
 * Merge surface (the WP-l1apps-merge contract):
 *   disk-tool                -> the menu wizard below
 *   disk-tool direct <op> .. -> explicit verbatim passthrough of one
 *                               op from the table below
 *   disk-tool <op> [args]    -> classic verbatim dispatch: the SAME
 *                               table, the historical op spellings
 *                               (identical output to running the old
 *                               single-command app)
 *
 * The table carries EVERY block-service op (ahci .. abcfg) and every
 * fs-service op (mount .. tree) - zero functionality loss; the
 * single-command apps themselves stay registered (interface freeze).
 *
 * The wizard adds the safety layer the raw ops never had:
 *   - mkfs.* and abdisk ask for the device name, make the user
 *     RETYPE it, then a bare y/N - three deliberate steps;
 *   - the mount manager collects type/dev/path fields one by one so
 *     the argument order can never be mixed up;
 *   - everything else (lsblk/parted/fsck/dskstat/abcfg, the A/B
 *     status) is read-only and runs immediately. */
#include "l1_lib.h"
#include "l1svc_ops.h"
#include "wizard_lib.h"

/* ---- the verbatim op table ------------------------------------------
 * name = the historical command spelling (what the single-command
 * apps were called), cat/op = the L1SVC call under it. */
struct wiz_op {
    const char *name;
    long cat;
    long op;
};

static const struct wiz_op g_ops[] = {
    /* block service */
    {"ahci",           L1SVC_CAT_BLOCK, L1SVC_BLK_AHCI},
    {"nvme",           L1SVC_CAT_BLOCK, L1SVC_BLK_NVME},
    {"ata",            L1SVC_CAT_BLOCK, L1SVC_BLK_ATA},
    {"dskstat",        L1SVC_CAT_BLOCK, L1SVC_BLK_DSKSTAT},
    {"lsblk",          L1SVC_CAT_BLOCK, L1SVC_BLK_LSBLK},
    {"parted",         L1SVC_CAT_BLOCK, L1SVC_BLK_PARTED},
    {"mkfs.fat32",     L1SVC_CAT_BLOCK, L1SVC_BLK_MKFS_FAT32},
    {"mkfs.exfat",     L1SVC_CAT_BLOCK, L1SVC_BLK_MKFS_EXFAT},
    {"mkfs.ext4",      L1SVC_CAT_BLOCK, L1SVC_BLK_MKFS_EXT4},
    {"fsck",           L1SVC_CAT_BLOCK, L1SVC_BLK_FSCK},
    {"sync",           L1SVC_CAT_BLOCK, L1SVC_BLK_SYNC},
    {"ahci_test",      L1SVC_CAT_BLOCK, L1SVC_BLK_AHCI_TEST},
    {"nvme_test",      L1SVC_CAT_BLOCK, L1SVC_BLK_NVME_TEST},
    {"ata_dma_test",   L1SVC_CAT_BLOCK, L1SVC_BLK_ATA_DMA_TEST},
    {"virtio_blk_test",L1SVC_CAT_BLOCK, L1SVC_BLK_VIRTIO_TEST},
    {"disk_rw_test",   L1SVC_CAT_BLOCK, L1SVC_BLK_RW_TEST},
    {"partition_test", L1SVC_CAT_BLOCK, L1SVC_BLK_PART_TEST},
    {"fs_mount_test",  L1SVC_CAT_BLOCK, L1SVC_BLK_FS_MOUNT_TEST},
    {"real_hw_test",   L1SVC_CAT_BLOCK, L1SVC_BLK_REAL_HW_TEST},
    {"abdisk",         L1SVC_CAT_BLOCK, L1SVC_BLK_ABDISK},
    {"install",        L1SVC_CAT_BLOCK, L1SVC_BLK_INSTALL},
    {"grub-install",   L1SVC_CAT_BLOCK, L1SVC_BLK_GRUB_INSTALL},
    {"abcfg",          L1SVC_CAT_BLOCK, L1SVC_BLK_ABCFG},
    /* fs service */
    {"mount",          L1SVC_CAT_FS, L1SVC_FS_MOUNT},
    {"mounts",         L1SVC_CAT_FS, L1SVC_FS_MOUNTS},
    {"umount",         L1SVC_CAT_FS, L1SVC_FS_UMOUNT},
    {"fatmount",       L1SVC_CAT_FS, L1SVC_FS_FATMOUNT},
    {"fatstat",        L1SVC_CAT_FS, L1SVC_FS_FATSTAT},
    {"tree",           L1SVC_CAT_FS, L1SVC_FS_TREE},
};
#define G_OPS_N ((int)(sizeof(g_ops) / sizeof(g_ops[0])))

static char g_out[WIZ_OUT_CAP];

/* ---- dispatch helpers ------------------------------------------------ */
static const struct wiz_op *find_op(const char *name) {
    for (int i = 0; i < G_OPS_N; i++)
        if (streq_(g_ops[i].name, name)) return &g_ops[i];
    return (const struct wiz_op *)0;
}

/* Run one op with the historical single-argument-line semantics:
 * argv[start..] joined by single spaces (the l1svc_main body). */
static int run_op(const struct wiz_op *o, int argc, char **argv, int start) {
    char arg[256];
    int len = 0;
    arg[0] = 0;
    for (int j = start; j < argc && len < (int)sizeof(arg) - 1; j++) {
        if (j > start && len < (int)sizeof(arg) - 2) arg[len++] = ' ';
        for (const char *p = argv[j]; *p && len < (int)sizeof(arg) - 1; p++)
            arg[len++] = *p;
        arg[len] = 0;
    }
    long rc = sys_l1svc(o->cat, o->op, arg[0] ? (long)arg : 0,
                        (long)g_out, (long)sizeof(g_out));
    if (rc != 0) {
        puts_(o->name);
        puts_(tr_("svc_call_failed"));
        return 1;
    }
    puts_(g_out);
    if (g_out[0] && g_out[strlen_(g_out) - 1] != '\n') putc_('\n');
    return 0;
}

static void usage(void) {
    puts_("usage: disk-tool            (interactive menu)\n");
    puts_("       disk-tool direct <op> [args]   (explicit passthrough)\n");
    puts_("       disk-tool <op> [args]          (classic op dispatch)\n");
    puts_("ops:");
    for (int i = 0; i < G_OPS_N; i++) {
        puts_("  ");
        puts_(g_ops[i].name);
        putc_('\n');
    }
}

/* ---- destructive-path guard -----------------------------------------
 * mkfs.* and abdisk: the device is asked for, RETYPED, then a bare
 * y/N - and only then does the op run. */
static int guarded_device_op(const struct wiz_op *o) {
    static char dev[64];
    static char dev2[64];
    wiz_ask(tr_("dt_ask_dev"), dev, (int)sizeof(dev));
    if (!dev[0]) return 1;
    wiz_ask(tr_("dt_retype"), dev2, (int)sizeof(dev2));
    if (!streq_(dev, dev2)) {
        wiz_fail(tr_("dt_mismatch"));
        return 1;
    }
    puts_(tr_("dt_warn"));
    putc_('\n');
    if (!wiz_confirm(tr_("dt_confirm"))) return 1;
    char arg[128];
    strcpy_(arg, dev);
    long rc = sys_l1svc(o->cat, o->op, (long)arg, (long)g_out,
                        (long)sizeof(g_out));
    if (rc != 0) {
        puts_(o->name);
        puts_(tr_("svc_call_failed"));
        return 1;
    }
    puts_(g_out);
    if (g_out[0] && g_out[strlen_(g_out) - 1] != '\n') putc_('\n');
    return 0;
}

/* ---- submenus -------------------------------------------------------- */
static void submenu_mkfs(void) {
    static const char *const items[] = {
        "dt_mkfs_fat32", "dt_mkfs_exfat", "dt_mkfs_ext4"};
    for (;;) {
        int c = wiz_menu(tr_("dt_mkfs_title"), items, 3);
        if (c == 0) return;
        (void)guarded_device_op(find_op(c == 1 ? "mkfs.fat32" :
                                        c == 2 ? "mkfs.exfat" :
                                                 "mkfs.ext4"));
        wiz_pause();
    }
}

static void submenu_mount(void) {
    static const char *const items[] = {
        "dt_mounts", "dt_mount", "dt_umount", "dt_fatmount", "dt_fatstat"};
    static char a[96], b[96], c[96];
    for (;;) {
        int m = wiz_menu(tr_("dt_mount_title"), items, 5);
        if (m == 0) return;
        if (m == 1) { (void)wiz_svc(L1SVC_CAT_FS, L1SVC_FS_MOUNTS, 0, g_out); }
        else if (m == 2) {
            /* mount <type> <dev> <path> - one field at a time */
            wiz_ask(tr_("dt_mount_type"), a, (int)sizeof(a));
            if (!a[0]) continue;
            wiz_ask(tr_("dt_mount_dev"), b, (int)sizeof(b));
            if (!b[0]) continue;
            wiz_ask(tr_("dt_mount_path"), c, (int)sizeof(c));
            if (!c[0]) continue;
            char arg[256];
            strcpy_(arg, a); strcat_(arg, " ");
            strcat_(arg, b); strcat_(arg, " "); strcat_(arg, c);
            (void)wiz_svc(L1SVC_CAT_FS, L1SVC_FS_MOUNT, arg, g_out);
        }
        else if (m == 3) {
            wiz_ask(tr_("dt_umount_path"), a, (int)sizeof(a));
            if (!a[0]) continue;
            (void)wiz_svc(L1SVC_CAT_FS, L1SVC_FS_UMOUNT, a, g_out);
        }
        else if (m == 4) {
            /* fatmount [dev path] */
            wiz_ask(tr_("dt_mount_dev"), b, (int)sizeof(b));
            if (!b[0]) continue;
            wiz_ask(tr_("dt_mount_path"), c, (int)sizeof(c));
            char arg[192];
            strcpy_(arg, b);
            if (c[0]) { strcat_(arg, " "); strcat_(arg, c); }
            (void)wiz_svc(L1SVC_CAT_FS, L1SVC_FS_FATMOUNT, arg, g_out);
        }
        else {
            (void)wiz_svc(L1SVC_CAT_FS, L1SVC_FS_FATSTAT, 0, g_out);
        }
        wiz_pause();
    }
}

static void submenu_ab(void) {
    static const char *const items[] = {
        "dt_ab_status", "dt_ab_create", "dt_ab_cfg"};
    for (;;) {
        int c = wiz_menu(tr_("dt_ab_title"), items, 3);
        if (c == 0) return;
        if (c == 1) {
            /* the A/B status lives in the OTA service (cross-domain
             * by design: same machine state, richer text) */
            (void)wiz_svc(L1SVC_CAT_OTA, L1SVC_OTA_UPDATE, "status", g_out);
        } else if (c == 2) {
            (void)guarded_device_op(find_op("abdisk"));
        } else {
            (void)wiz_svc(L1SVC_CAT_BLOCK, L1SVC_BLK_ABCFG, 0, g_out);
        }
        wiz_pause();
    }
}

/* ---- the main menu --------------------------------------------------- */
static int disk_wizard(void) {
    static const char *const items[] = {
        "dt_m_lsblk", "dt_m_parted", "dt_m_mkfs", "dt_m_mount",
        "dt_m_fsck", "dt_m_dskstat", "dt_m_ab"};
    static char dev[96];
    for (;;) {
        int c = wiz_menu(tr_("wiz_title"), items, 7);
        if (c == 0) return 0;
        if (c == 1) {
            (void)wiz_svc(L1SVC_CAT_BLOCK, L1SVC_BLK_LSBLK, 0, g_out);
            wiz_pause();
        } else if (c == 2) {
            wiz_ask(tr_("dt_ask_dev_opt"), dev, (int)sizeof(dev));
            (void)wiz_svc(L1SVC_CAT_BLOCK, L1SVC_BLK_PARTED, dev, g_out);
            wiz_pause();
        } else if (c == 3) {
            submenu_mkfs();
        } else if (c == 4) {
            submenu_mount();
        } else if (c == 5) {
            wiz_ask(tr_("dt_ask_dev_opt"), dev, (int)sizeof(dev));
            (void)wiz_svc(L1SVC_CAT_BLOCK, L1SVC_BLK_FSCK, dev, g_out);
            wiz_pause();
        } else if (c == 6) {
            (void)wiz_svc(L1SVC_CAT_BLOCK, L1SVC_BLK_DSKSTAT, 0, g_out);
            wiz_pause();
        } else {
            submenu_ab();
        }
    }
}

int app_main(int argc, char **argv) {
    if (argc < 2) return disk_wizard();

    int start = 1;
    if (streq_(argv[1], "direct")) {
        if (argc < 3) { usage(); return 1; }
        start = 2;
    }
    const struct wiz_op *o = find_op(argv[start]);
    if (!o) {
        puts_("disk-tool: unknown op: ");
        puts_(argv[start]);
        putc_('\n');
        usage();
        return 1;
    }
    return run_op(o, argc, argv, start + 1);
}
