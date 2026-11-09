/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP06b-ocsecl
 * File: ocse/ocse_cl.c
 * Purpose: OCSECL - the OCSE command line. A plain interactive shell
 * for installing the system: prompt, tokenizer, the WP-04-fix1 line
 * editor (arrows/Home/End/Del, Ctrl+A/E/U/K/W/C, history, Tab
 * completion) and a fixed command table (the 28 installer commands
 * plus the WP-06a diagnostics). Line input comes from the keyboard
 * queue (PS/2 and the COM1 serial RX path both feed it); every echo
 * and all command output goes through ocse_out (console + serial).
 */
#include "ocse_cl.h"
#include "ocse_gui.h"
#include "ocse_i18n.h"
#include "ocse_detect.h"
#include "ocse_disk.h"
#include "ocse_part.h"
#include "ocse_mkfs.h"
#include "ocse_sha256.h"
#include "ocse_inst.h"
#include "fs_vfs.h"
#include "lib_tar.h"
#include "screen_console.h"
#include "screen_fb.h"
#include "driver_input_keyboard.h"
#include "driver_block_blk.h"
#include "core_sched.h"
#include "core_power.h"
#include "lib_string.h"
#include <stdint.h>

extern int g_ocse_hotkey_req;

/* Provided by ocse_main.c (serial logging path). */
extern void ocse_serial_puts_raw(const char* s);

#define CL_BUF     256
#define CL_HIST    32
#define CL_MAXARGS 20

/* ==================================================================
 * Line editor (ported from the WP-04-fix1 ush editor: prompt-aware
 * redraw that clears to the previous rendered length, cursor-aware
 * insert/delete, history with draft save). Key codes 0x80..0x89 are
 * produced by the same keyboard driver ush reads.
 * ================================================================== */
static const char* g_le_prompt = "";
static int g_le_prevlen = 0;

static void le_write(const char* s) { ocse_out(s); }

static void le_redraw(const char* line, int llen, int cur) {
    char cr = '\r';
    char sp[2] = { ' ', 0 };
    (void)cur;
    int plen = (int)strlen(g_le_prompt);
    char one[2] = { 0, 0 };
    one[0] = cr; le_write(one);
    le_write(g_le_prompt);
    one[0] = ' ';
    for (int i = 0; i < llen; i++) {
        char c[2] = { line[i], 0 };
        le_write(c);
    }
    for (int i = llen; i < g_le_prevlen; i++)
        le_write(sp);                 /* wipe the old render's tail */
    one[0] = cr; le_write(one);
    le_write(g_le_prompt);
    for (int i = 0; i < cur; i++) {
        char c[2] = { line[i], 0 };
        le_write(c);
    }
    g_le_prevlen = llen;
    (void)plen;
}

static int le_insert(char* line, int llen, int cur, char c, int cap) {
    if (llen + 1 >= cap) return cur;
    if (cur == llen) {
        line[llen] = c;
        char one[2] = { c, 0 };
        le_write(one);
    } else {
        for (int i = llen; i > cur; i--) line[i] = line[i - 1];
        line[cur] = c;
        le_redraw(line, llen + 1, cur + 1);
    }
    line[llen + 1] = 0;
    return cur + 1;
}

static int le_backspace(char* line, int llen, int cur) {
    if (cur <= 0) return cur;
    for (int i = cur - 1; i < llen; i++) line[i] = line[i + 1];
    if (cur == llen) le_write("\b \b");
    else le_redraw(line, llen - 1, cur - 1);
    return cur - 1;
}

static int le_delete(char* line, int llen, int cur) {
    if (cur >= llen) return cur;
    for (int i = cur; i < llen; i++) line[i] = line[i + 1];
    le_redraw(line, llen - 1, cur);
    return cur;
}

/* ---- history ---- */
static char g_hist[CL_HIST][CL_BUF];
static int  g_hist_n = 0;

static void hist_add(const char* line) {
    if (!line[0]) return;
    if (g_hist_n > 0 && strcmp(g_hist[g_hist_n - 1], line) == 0)
        return;                                 /* no adjacent dupes */
    if (g_hist_n == CL_HIST) {
        memmove(g_hist[0], g_hist[1], (u64)(CL_HIST - 1) * CL_BUF);
        g_hist_n--;
    }
    strncpy(g_hist[g_hist_n], line, CL_BUF - 1);
    g_hist[g_hist_n][CL_BUF - 1] = 0;
    g_hist_n++;
}

/* ==================================================================
 * Working directory + path resolution (WP-06b batch-4; ported from
 * the v1.0.0 shell: shell_normalize_path / shell_resolve_path_base).
 * The component stack is capped at 32 levels - installer paths are
 * shallow and the ~2 KiB frame stays well clear of the 16 KiB stack
 * even with an IRQ frame landing on top. A component longer than
 * VFS_NAME_LEN-1 splits at the limit (base-shell behavior).
 * ================================================================== */
#define CL_MAXCOMP 32
static char g_cwd[VFS_PATH_LEN] = "/";

static int cl_normalize_path(char* path, int maxlen) {
    if (!path || maxlen < 2) return -1;
    if (path[0] != '/') return -1;
    char comps[CL_MAXCOMP][VFS_NAME_LEN];
    int ncomp = 0;
    int i = 0;
    while (path[i]) {
        while (path[i] == '/') i++;
        if (!path[i]) break;
        char comp[VFS_NAME_LEN];
        int ci = 0;
        while (path[i] && path[i] != '/' && ci < VFS_NAME_LEN - 1)
            comp[ci++] = path[i++];
        comp[ci] = 0;
        if (strcmp(comp, ".") == 0) {
            /* skip */
        } else if (strcmp(comp, "..") == 0) {
            if (ncomp > 0) ncomp--;
        } else {
            if (ncomp >= CL_MAXCOMP) return -1;
            strncpy(comps[ncomp], comp, VFS_NAME_LEN - 1);
            comps[ncomp][VFS_NAME_LEN - 1] = 0;
            ncomp++;
        }
    }
    int p = 0;
    path[p++] = '/';
    for (int c = 0; c < ncomp; c++) {
        int len = (int)strlen(comps[c]);
        if (p + len + 1 >= maxlen) return -1;
        memcpy(path + p, comps[c], (usize)len);
        p += len;
        if (c < ncomp - 1) path[p++] = '/';
    }
    path[p] = 0;
    return 0;
}

/* Resolve `in` against the cwd into `out` (normalized absolute). */
static int cl_resolve(const char* in, char* out, int cap) {
    if (!in || !out || cap < 2) return -1;
    if (in[0] == '/') {
        strncpy(out, in, (usize)(cap - 1));
        out[cap - 1] = 0;
    } else {
        int cl = (int)strlen(g_cwd);
        int il = (int)strlen(in);
        int need_slash = (cl > 0 && g_cwd[cl - 1] != '/');
        if (cl + need_slash + il + 1 > cap) return -1;
        memcpy(out, g_cwd, (usize)cl);
        if (need_slash) { out[cl++] = '/'; out[cl] = 0; }
        memcpy(out + cl, in, (usize)il);
        out[cl + il] = 0;
    }
    return cl_normalize_path(out, cap);
}

/* Shared 64 KiB I/O buffer: the CLI is single-threaded (one operator
 * at a time), so cp/tar/dd/sha256sum take turns instead of each
 * burning their own .bss. */
#define CL_IOBUF_SZ 65536
static u8 g_io_buf[CL_IOBUF_SZ];

/* ==================================================================
 * Command table
 * ================================================================== */
typedef int (*cl_fn)(int argc, char** argv);

typedef struct {
    const char* name;
    const char* help;       /* English - system-visible string */
    cl_fn fn;
} cl_cmd_t;

static const cl_cmd_t* cl_find(const char* name);

static void cl_unknown(const char* cmd) {
    ocse_out("ocsecl: '");
    ocse_out(cmd);
    ocse_outln("' - unknown command (try 'help')");
}

/* ---- WP-06a diagnostics kept as-is ---- */
static void print_checks(void) {
    for (int i = 0; i < OCSE_CHECKS; i++) {
        ocse_out("  ");
        ocse_out(ocse_tr(OCSE_T_CHECK_CPU + i));
        ocse_out(": ");
        ocse_out(ocse_tr(g_ocse_checks[i].pass ? OCSE_T_PASS : OCSE_T_FAIL));
        ocse_out("  [");
        ocse_out(g_ocse_checks[i].detail);
        ocse_outln("]");
    }
}

static void print_disks(void) {
    char n[16];
    if (g_ocse_ndisks == 0) {
        ocse_outln(ocse_tr(OCSE_T_NO_DISK));
        return;
    }
    for (int d = 0; d < g_ocse_ndisks; d++) {
        ocse_disk_t* dk = &g_ocse_disks[d];
        ocse_out("  ");
        ocse_out(dk->name);
        ocse_out(dk->table_kind == 2 ? " [GPT] " :
                 dk->table_kind == 1 ? " [MBR] " : " [no table] ");
        u64_to_str(dk->mb, n); ocse_out(n); ocse_out(" MiB, ");
        u64_to_str((u64)dk->nparts, n); ocse_out(n); ocse_out(" part(s):");
        ocse_outln("");
        for (int p = 0; p < dk->nparts; p++) {
            ocse_part_t* pt = &dk->parts[p];
            ocse_out("    #");
            u64_to_str((u64)p, n); ocse_out(n);
            ocse_out(" ");
            ocse_out(ocse_fs_name(pt->fs));
            ocse_out(" lba=");
            u64_to_str(pt->start_lba, n); ocse_out(n);
            ocse_out(" size=");
            u64_to_str(pt->mb, n); ocse_out(n); ocse_out("MiB");
            ocse_outln("");
        }
        if (dk->space_low) {
            ocse_out("    (!) ");
            ocse_outln(ocse_tr(OCSE_T_SPACE_LOW));
        }
    }
}

static int cmd_hwcheck(int argc, char** argv) {
    (void)argc; (void)argv;
    ocse_detect_run();
    print_checks();
    return 0;
}
static int cmd_disks(int argc, char** argv) {
    (void)argc; (void)argv;
    ocse_disk_scan();
    print_disks();
    return 0;
}
static int cmd_lang(int argc, char** argv) {
    if (argc == 1) {
        ocse_outln(ocse_tr(OCSE_T_LANG_NOW));
        return 0;
    }
    if (argc == 2 && (strcmp(argv[1], "en") == 0 ||
                      strcmp(argv[1], "zh") == 0)) {
        ocse_i18n_set(argv[1][0] == 'e' ? OCSE_LANG_EN : OCSE_LANG_ZH);
        ocse_outln(ocse_tr(OCSE_T_LANG_NOW));
        return 0;
    }
    ocse_outln("usage: lang [en|zh]");
    return 1;
}
/* WP06g3-4.2: serial line output encoding - the same setting the
 * main kernel exposes (serial_encoding utf8|gbk).  System
 * strings stay English; the encoding names are the terminology. */
static int cmd_serial_encoding(int argc, char** argv) {
    if (argc == 1) {
        ocse_out("serial encoding: ");
        ocse_outln(ocse_serial_encoding_name());
        return 0;
    }
    if (argc == 2 && (strcmp(argv[1], "utf8") == 0 ||
                      strcmp(argv[1], "gbk") == 0)) {
        ocse_serial_encoding_set_str(argv[1]);
        ocse_out("serial encoding: ");
        ocse_outln(ocse_serial_encoding_name());
        return 0;
    }
    ocse_outln("usage: serial_encoding [utf8|gbk]");
    return 1;
}

static int cmd_clear(int argc, char** argv) {
    (void)argc; (void)argv;
    screen_console_reset(screen_fb_rgb(0xE0, 0xE0, 0xE0),
                         screen_fb_rgb(0x10, 0x10, 0x14));
    return 0;
}

/* click <x> <y>: probe the GUI button geometry through the same
 * hit_test() the pointer path uses (T4's F-step matrix; the QEMU
 * monitor cannot inject pointer events into the vendored USB
 * stack). Pure geometry - no state, no side effects. */
static int cmd_click(int argc, char** argv) {
    if (argc != 3) {
        ocse_outln("usage: click <x> <y>");
        return 1;
    }
    u64 v[2] = { 0, 0 };
    for (int i = 0; i < 2; i++) {
        int j = 0;
        while (argv[i + 1][j] >= '0' && argv[i + 1][j] <= '9' && j < 9) {
            v[i] = v[i] * 10 + (u64)(argv[i + 1][j] - '0');
            j++;
        }
        if (argv[i + 1][j]) {
            ocse_outln("click: bad coordinate");
            return 1;
        }
    }
    return ocse_gui_click_probe((int)v[0], (int)v[1]);
}
static int cmd_reboot(int argc, char** argv) {
    (void)argc; (void)argv;
    ocse_outln(ocse_tr(OCSE_T_REBOOTING));
    ocse_serial_puts_raw("[ocsecl] reboot requested\r\n");
    core_power_reboot();                        /* flushes blk, never returns */
    return 0;
}
static int cmd_shutdown(int argc, char** argv) {
    (void)argc; (void)argv;
    ocse_outln(ocse_tr(OCSE_T_POWEROFF));
    ocse_serial_puts_raw("[ocsecl] shutdown requested\r\n");
    if (core_power_shutdown() != 0) {
        ocse_outln(ocse_tr(OCSE_T_POWEROFF_NOACPI));
        core_power_halt();
    }
    return 0;
}

/* ---- WP-06b partition commands ---- */
static int cmd_lsdisk(int argc, char** argv) {
    (void)argc; (void)argv;
    return ocse_part_lsdisk();
}

static int cmd_lspart(int argc, char** argv) {
    (void)argc;
    if (!argv[1]) { ocse_outln("usage: lspart <disk>"); return 1; }
    return ocse_part_lspart(argv[1]);
}
static int cmd_parted(int argc, char** argv) {
    (void)argc;
    if (!argv[1]) { ocse_outln("usage: parted <disk>"); return 1; }
    return ocse_part_parted(argv[1]);
}
static int cmd_mkpart(int argc, char** argv) {
    (void)argc;
    if (!argv[1] || !argv[2] || !argv[3]) {
        ocse_outln("usage: mkpart <disk> <startMiB> <endMiB>");
        return 1;
    }
    u64 s = 0, e = 0;
    int i = 0;
    while (argv[2][i] >= '0' && argv[2][i] <= '9' && i < 12) {
        s = s * 10 + (u64)(argv[2][i] - '0'); i++;
    }
    if (argv[2][i] || i == 0) { ocse_outln("mkpart: bad start"); return 1; }
    i = 0;
    while (argv[3][i] >= '0' && argv[3][i] <= '9' && i < 12) {
        e = e * 10 + (u64)(argv[3][i] - '0'); i++;
    }
    if (argv[3][i] || i == 0) { ocse_outln("mkpart: bad end"); return 1; }
    return ocse_part_mkpart(argv[1], s, e);
}
static int cmd_rmpart(int argc, char** argv) {
    (void)argc;
    if (!argv[1] || !argv[2]) {
        ocse_outln("usage: rmpart <disk> <n>");
        return 1;
    }
    int n = 0, i = 0;
    while (argv[2][i] >= '0' && argv[2][i] <= '9' && i < 4) {
        n = n * 10 + (argv[2][i] - '0'); i++;
    }
    if (argv[2][i] || i == 0 || n < 1) {
        ocse_outln("rmpart: bad partition number");
        return 1;
    }
    return ocse_part_rmpart(argv[1], n);
}
static int cmd_setflag(int argc, char** argv) {
    (void)argc;
    if (!argv[1] || !argv[2] || !argv[3]) {
        ocse_outln("usage: setflag <disk> <n> boot [on|off]");
        return 1;
    }
    int n = 0, i = 0;
    while (argv[2][i] >= '0' && argv[2][i] <= '9' && i < 4) {
        n = n * 10 + (argv[2][i] - '0'); i++;
    }
    if (argv[2][i] || i == 0 || n < 1) {
        ocse_outln("setflag: bad partition number");
        return 1;
    }
    int on = 1;
    if (argc >= 5) {
        if (strcmp(argv[4], "on") == 0 || strcmp(argv[4], "1") == 0)
            on = 1;
        else if (strcmp(argv[4], "off") == 0 || strcmp(argv[4], "0") == 0)
            on = 0;
        else {
            ocse_outln("setflag: value must be on|off");
            return 1;
        }
    }
    return ocse_part_setflag(argv[1], n, argv[3], on);
}

/* ---- WP-06b format + mount commands ----
 * mkfs.* keeps the BUG-0239 data-loss gate: nothing is written until
 * the literal token `confirm` arrives as the last argument, and a
 * mounted device is refused outright. */
static int mkfs_dev_mounted(const char* dev) {
    fs_vfs_mount_info_t m[VFS_MAX_MOUNTS];
    int n = fs_vfs_get_mounts(m, VFS_MAX_MOUNTS);
    for (int i = 0; i < n; i++)
        if (m[i].in_use && strcmp(m[i].device, dev) == 0) return 1;
    return 0;
}

static int cmd_mkfs_common(const char* cmd, int argc, char** argv, int fmt) {
    if (argc < 2) {
        ocse_out(cmd);
        ocse_outln(": WARNING: formatting DESTROYS ALL DATA on the target device!");
        ocse_out("  usage: ");
        ocse_out(cmd);
        ocse_outln(" <device> confirm");
        return 1;
    }
    /* rebuild "<dev> [confirm]" for the shared gate */
    char tail[CL_BUF];
    strncpy(tail, argv[1], sizeof(tail) - 1);
    tail[sizeof(tail) - 1] = 0;
    for (int i = 2; i < argc; i++) {
        u64 l = strlen(tail);
        u64 al = strlen(argv[i]);
        if (l + al + 2 > sizeof(tail)) break;
        tail[l] = ' ';
        tail[l + 1] = 0;
        strcat(tail, argv[i]);
    }
    char dev[32];
    if (ocse_mkfs_gate(cmd, tail, dev, sizeof(dev)) != 0) return 1;
    if (mkfs_dev_mounted(dev)) {
        ocse_out(cmd);
        ocse_out(": '");
        ocse_out(dev);
        ocse_out("'");
        ocse_outln(ocse_tr(OCSE_T_M_MOUNTED));
        return 1;
    }
    int idx = driver_block_find_device(dev);
    if (idx < 0) {
        ocse_out(cmd);
        ocse_out(": no such device: ");
        ocse_outln(dev);
        return 1;
    }
    int rc;
    if (fmt == OCSE_FMT_FAT32) rc = ocse_mkfs_fat32(idx);
    else if (fmt == OCSE_FMT_EXFAT) rc = ocse_mkfs_exfat(idx);
    else rc = ocse_mkfs_ext4(idx);
    if (rc != 0) {
        ocse_out(cmd);
        ocse_out(": '");
        ocse_out(dev);
        ocse_out("' ");
        ocse_outln(ocse_tr(OCSE_T_M_FMTFAIL));
        return 1;
    }
    ocse_out(cmd);
    ocse_out(": '");
    ocse_out(dev);
    ocse_out("' ");
    ocse_outln(ocse_tr(OCSE_T_M_FORMATTED));
    ocse_mkfs_sync_mbr_type(dev, fmt);
    ocse_disk_scan();
    return 0;
}

static int cmd_mkfs_fat32(int argc, char** argv) {
    return cmd_mkfs_common("mkfs.fat32", argc, argv, OCSE_FMT_FAT32);
}
static int cmd_mkfs_exfat(int argc, char** argv) {
    return cmd_mkfs_common("mkfs.exfat", argc, argv, OCSE_FMT_EXFAT);
}
static int cmd_mkfs_ext4(int argc, char** argv) {
    return cmd_mkfs_common("mkfs.ext4", argc, argv, OCSE_FMT_EXT4);
}

static int cmd_mount(int argc, char** argv) {
    (void)argc;
    if (!argv[1] || !argv[2]) {
        ocse_outln("usage: mount <device> <path>");
        return 1;
    }
    const char* dev = argv[1];
    const char* path = argv[2];
    if (path[0] != '/') {
        ocse_outln("mount: path must be absolute (start with '/')");
        return 1;
    }
    /* fs auto-detection: the CD drive is the ISO medium; everything
     * else is classified by its on-disk signature */
    const char* fs = NULL;
    if (dev[0] == 'c' && dev[1] == 'd' && dev[2] >= '0' && dev[2] <= '9') {
        fs = "iso9660";
    } else {
        ocse_fs_t k = ocse_disk_probe_fs(dev);
        if (k == OCSE_FS_FAT32) fs = "fat32";
        else if (k == OCSE_FS_EXFAT) fs = "exfat";
        else if (k == OCSE_FS_EXT) fs = "ext4";
    }
    if (!fs) {
        ocse_out("mount: '");
        ocse_out(dev);
        ocse_out("'");
        ocse_outln(ocse_tr(OCSE_T_M_NOFS));
        return 1;
    }
    if (fs_vfs_mount(fs, path, dev) != 0) {
        ocse_out("mount: ");
        ocse_out(dev);
        ocse_out(" -> ");
        ocse_out(path);
        ocse_outln(" failed");
        return 1;
    }
    ocse_out(ocse_tr(OCSE_T_M_MOUNTEDW));
    ocse_out(dev);
    ocse_out(" (");
    ocse_out(fs);
    ocse_out(ocse_tr(OCSE_T_M_MNTAT));
    ocse_outln(path);
    return 0;
}

static int cmd_umount(int argc, char** argv) {
    (void)argc;
    if (!argv[1]) {
        ocse_outln("usage: umount <path>");
        return 1;
    }
    if (fs_vfs_umount(argv[1]) != 0) {
        ocse_out("umount: ");
        ocse_out(argv[1]);
        ocse_outln(ocse_tr(OCSE_T_M_UMNTFAIL));
        return 1;
    }
    ocse_out(ocse_tr(OCSE_T_M_UNMOUNTEDW));
    ocse_outln(argv[1]);
    return 0;
}

/* ---- WP-06b batch-4: file commands ----
 * Everything below operates through the VFS: paths resolve against
 * the shell cwd, cross mount points transparently, and the ext4
 * write path (batch-3) is what actually lands the bytes when the
 * target volume is ext4. */
static void cl_print_dirent(const char* name, int type, u64 size) {
    char num[24];
    if (type == VFS_TYPE_DIR) {
        ocse_out("  [D] ");
        ocse_outln(name);
    } else if (type == VFS_TYPE_DEVICE) {
        ocse_out("  [C] ");
        ocse_outln(name);
    } else {
        u64_to_str(size, num);
        int nl = (int)strlen(num);
        ocse_out("  ");
        for (int i = nl; i < 10; i++) ocse_out(" ");
        ocse_out(num);
        ocse_out(" ");
        ocse_outln(name);
    }
}

static int cmd_ls(int argc, char** argv) {
    const char* arg = (argc >= 2) ? argv[1] : g_cwd;
    char path[VFS_PATH_LEN];
    if (cl_resolve(arg, path, sizeof(path)) != 0) {
        ocse_outln("ls: bad path");
        return 1;
    }
    fs_vfs_stat_t st;
    if (fs_vfs_stat(path, &st) != 0) {
        ocse_out("ls: no such path: ");
        ocse_outln(path);
        return 1;
    }
    if (st.type != VFS_TYPE_DIR) {
        cl_print_dirent(st.name, st.type, st.size);
        return 0;
    }
    int pl = (int)strlen(path);
    int need_slash = (pl > 0 && path[pl - 1] != '/');
    for (int i = 0; ; i++) {
        fs_vfs_dirent_t e;
        if (fs_vfs_readdir(path, i, &e) != 0) break;
        u64 sz = 0;
        if (e.type == VFS_TYPE_FILE) {
            char child[VFS_PATH_LEN];
            int el = (int)strlen(e.name);
            if (pl + need_slash + el + 1 >= (int)sizeof(child)) {
                cl_print_dirent(e.name, e.type, 0);
                continue;
            }
            memcpy(child, path, (usize)pl);
            int cl = pl;
            if (need_slash) child[cl++] = '/';
            memcpy(child + cl, e.name, (usize)(el + 1));
            fs_vfs_stat_t es;
            if (fs_vfs_stat(child, &es) == 0) sz = es.size;
        }
        cl_print_dirent(e.name, e.type, sz);
    }
    return 0;
}

static int cmd_cd(int argc, char** argv) {
    const char* arg = (argc >= 2) ? argv[1] : "/";
    char path[VFS_PATH_LEN];
    if (cl_resolve(arg, path, sizeof(path)) != 0) {
        ocse_outln("cd: bad path");
        return 1;
    }
    fs_vfs_stat_t st;
    if (fs_vfs_stat(path, &st) != 0) {
        ocse_out("cd: no such directory: ");
        ocse_outln(arg);
        return 1;
    }
    if (st.type != VFS_TYPE_DIR) {
        ocse_out("cd: not a directory: ");
        ocse_outln(arg);
        return 1;
    }
    strncpy(g_cwd, path, sizeof(g_cwd) - 1);
    g_cwd[sizeof(g_cwd) - 1] = 0;
    return 0;
}

static int cmd_pwd(int argc, char** argv) {
    (void)argc; (void)argv;
    ocse_outln(g_cwd);
    return 0;
}

static int cmd_cat(int argc, char** argv) {
    if (argc < 2) { ocse_outln("usage: cat <file>"); return 1; }
    char path[VFS_PATH_LEN];
    if (cl_resolve(argv[1], path, sizeof(path)) != 0) {
        ocse_outln("cat: bad path");
        return 1;
    }
    int fd = fs_vfs_open(path, VFS_O_RDONLY);
    if (fd < 0) {
        ocse_out("cat: cannot open ");
        ocse_outln(path);
        return 1;
    }
    u64 shown = 0;
    int truncated = 0;
    for (;;) {
        int n = fs_vfs_read(fd, g_io_buf, 4096);
        if (n <= 0) break;
        for (int i = 0; i < n; i++) {
            if (shown >= 8192) { truncated = 1; break; }
            u8 ch = g_io_buf[i];
            char one[2] = { 0, 0 };
            if (ch >= 0x20 && ch < 0x7F) one[0] = (char)ch;
            else if (ch == '\n') one[0] = '\n';
            else if (ch == '\t') one[0] = '\t';
            else one[0] = '.';
            ocse_out(one);
            shown++;
        }
        if (truncated || n < 4096) break;
    }
    fs_vfs_close(fd);
    if (truncated) ocse_outln("\ncat: (output truncated at 8192 bytes)");
    return 0;
}

static int cmd_write(int argc, char** argv) {
    if (argc < 2) { ocse_outln("usage: write <file> [text ...]"); return 1; }
    char path[VFS_PATH_LEN];
    if (cl_resolve(argv[1], path, sizeof(path)) != 0) {
        ocse_outln("write: bad path");
        return 1;
    }
    /* join argv[2..] with single spaces; one trailing newline */
    char text[CL_BUF];
    int tl = 0;
    text[0] = 0;
    for (int i = 2; i < argc; i++) {
        int al = (int)strlen(argv[i]);
        int sp = (i > 2) ? 1 : 0;
        if (tl + sp + al + 2 > (int)sizeof(text)) break;
        if (sp) text[tl++] = ' ';
        memcpy(text + tl, argv[i], (usize)al);
        tl += al;
        text[tl] = 0;
    }
    if (tl > 0) { text[tl++] = '\n'; text[tl] = 0; }
    int fd = fs_vfs_open(path, VFS_O_WRONLY | VFS_O_CREAT | VFS_O_TRUNC);
    if (fd < 0) {
        ocse_out("write: cannot create ");
        ocse_outln(path);
        return 1;
    }
    int w = (tl > 0) ? fs_vfs_write(fd, text, tl) : 0;
    fs_vfs_close(fd);
    if (w != tl) { ocse_outln("write: short write"); return 1; }
    char n[24];
    ocse_out("wrote ");
    u64_to_str((u64)tl, n); ocse_out(n);
    ocse_out(" bytes to ");
    ocse_outln(path);
    return 0;
}

static int cmd_touch(int argc, char** argv) {
    if (argc < 2) { ocse_outln("usage: touch <file>"); return 1; }
    char path[VFS_PATH_LEN];
    if (cl_resolve(argv[1], path, sizeof(path)) != 0) {
        ocse_outln("touch: bad path");
        return 1;
    }
    fs_vfs_stat_t st;
    if (fs_vfs_stat(path, &st) == 0) return 0;   /* exists: unix touch */
    int fd = fs_vfs_open(path, VFS_O_WRONLY | VFS_O_CREAT);
    if (fd < 0) {
        ocse_out("touch: cannot create ");
        ocse_outln(path);
        return 1;
    }
    fs_vfs_close(fd);
    return 0;
}

static int cmd_mkdir(int argc, char** argv) {
    if (argc < 2) { ocse_outln("usage: mkdir <path>"); return 1; }
    char path[VFS_PATH_LEN];
    if (cl_resolve(argv[1], path, sizeof(path)) != 0) {
        ocse_outln("mkdir: bad path");
        return 1;
    }
    if (fs_vfs_mkdir(path) != 0) {
        ocse_out("mkdir: cannot create ");
        ocse_out(path);
        ocse_outln(" (exists, or parent missing)");
        return 1;
    }
    return 0;
}

static int cmd_rmdir(int argc, char** argv) {
    if (argc < 2) { ocse_outln("usage: rmdir <path>"); return 1; }
    char path[VFS_PATH_LEN];
    if (cl_resolve(argv[1], path, sizeof(path)) != 0) {
        ocse_outln("rmdir: bad path");
        return 1;
    }
    if (fs_vfs_rmdir(path) != 0) {
        ocse_out("rmdir: cannot remove ");
        ocse_out(path);
        ocse_outln(" (not found, not a directory, or not empty)");
        return 1;
    }
    return 0;
}

static int cmd_rm(int argc, char** argv) {
    if (argc < 2) { ocse_outln("usage: rm <file>"); return 1; }
    char path[VFS_PATH_LEN];
    if (cl_resolve(argv[1], path, sizeof(path)) != 0) {
        ocse_outln("rm: bad path");
        return 1;
    }
    fs_vfs_stat_t st;
    if (fs_vfs_stat(path, &st) != 0) {
        ocse_out("rm: no such file: ");
        ocse_outln(path);
        return 1;
    }
    if (st.type == VFS_TYPE_DIR) {
        ocse_out("rm: '");
        ocse_out(path);
        ocse_outln("' is a directory (use rmdir)");
        return 1;
    }
    if (fs_vfs_unlink(path) != 0) {
        ocse_out("rm: cannot remove ");
        ocse_outln(path);
        return 1;
    }
    return 0;
}

/* basename of a resolved path (trailing slashes stripped) */
static int cl_basename_of(const char* path, char* out, int cap) {
    int l = (int)strlen(path);
    while (l > 1 && path[l - 1] == '/') l--;
    int e = l;
    while (e > 0 && path[e - 1] != '/') e--;
    int len = l - e;
    if (len <= 0 || len >= cap) return -1;
    memcpy(out, path + e, (usize)len);
    out[len] = 0;
    return 0;
}

/* Full-path copy through the VFS (64 KiB turns). 0 on success with
 * *total set; negative on failure. */
static int cl_copy_file(const char* src, const char* dst, u64* total) {
    int in = fs_vfs_open(src, VFS_O_RDONLY);
    if (in < 0) return -1;
    int out = fs_vfs_open(dst, VFS_O_WRONLY | VFS_O_CREAT | VFS_O_TRUNC);
    if (out < 0) { fs_vfs_close(in); return -2; }
    *total = 0;
    for (;;) {
        int n = fs_vfs_read(in, g_io_buf, CL_IOBUF_SZ);
        if (n < 0) { fs_vfs_close(in); fs_vfs_close(out); return -3; }
        if (n == 0) break;
        int w = fs_vfs_write(out, g_io_buf, n);
        if (w != n) { fs_vfs_close(in); fs_vfs_close(out); return -4; }
        *total += (u64)n;
    }
    fs_vfs_close(in);
    fs_vfs_close(out);
    return 0;
}

static int cmd_cp(int argc, char** argv) {
    if (argc < 3) { ocse_outln("usage: cp <src> <dst>"); return 1; }
    char src[VFS_PATH_LEN], dst[VFS_PATH_LEN];
    if (cl_resolve(argv[1], src, sizeof(src)) != 0 ||
        cl_resolve(argv[2], dst, sizeof(dst)) != 0) {
        ocse_outln("cp: bad path");
        return 1;
    }
    fs_vfs_stat_t ss;
    if (fs_vfs_stat(src, &ss) != 0) {
        ocse_out("cp: no such file: ");
        ocse_outln(argv[1]);
        return 1;
    }
    if (ss.type != VFS_TYPE_FILE) {
        ocse_outln("cp: source is not a regular file");
        return 1;
    }
    /* unix cp semantics: dst directory -> dst/<basename> */
    fs_vfs_stat_t ds;
    if (fs_vfs_stat(dst, &ds) == 0 && ds.type == VFS_TYPE_DIR) {
        char base[VFS_NAME_LEN];
        if (cl_basename_of(src, base, sizeof(base)) != 0) {
            ocse_outln("cp: bad source name");
            return 1;
        }
        int dl = (int)strlen(dst);
        int need_slash = (dl > 0 && dst[dl - 1] != '/');
        if (dl + need_slash + (int)strlen(base) + 1 >= (int)sizeof(dst)) {
            ocse_outln("cp: destination path too long");
            return 1;
        }
        if (need_slash) { dst[dl++] = '/'; dst[dl] = 0; }
        memcpy(dst + dl, base, strlen(base) + 1);
    }
    if (strcmp(src, dst) == 0) {
        ocse_outln("cp: source and destination are the same file");
        return 1;
    }
    u64 total = 0;
    int rc = cl_copy_file(src, dst, &total);
    if (rc != 0) {
        char n[24];
        ocse_out("cp: copy failed (err ");
        u64_to_str((u64)(-rc), n); ocse_out(n);
        ocse_outln(")");
        return 1;
    }
    return 0;
}

static int cmd_mv(int argc, char** argv) {
    if (argc < 3) { ocse_outln("usage: mv <src> <dst>"); return 1; }
    char src[VFS_PATH_LEN], dst[VFS_PATH_LEN];
    if (cl_resolve(argv[1], src, sizeof(src)) != 0 ||
        cl_resolve(argv[2], dst, sizeof(dst)) != 0) {
        ocse_outln("mv: bad path");
        return 1;
    }
    fs_vfs_stat_t ss;
    if (fs_vfs_stat(src, &ss) != 0) {
        ocse_out("mv: no such file: ");
        ocse_outln(argv[1]);
        return 1;
    }
    fs_vfs_stat_t ds;
    if (fs_vfs_stat(dst, &ds) == 0 && ds.type == VFS_TYPE_DIR) {
        char base[VFS_NAME_LEN];
        if (cl_basename_of(src, base, sizeof(base)) != 0) {
            ocse_outln("mv: bad source name");
            return 1;
        }
        int dl = (int)strlen(dst);
        int need_slash = (dl > 0 && dst[dl - 1] != '/');
        if (dl + need_slash + (int)strlen(base) + 1 >= (int)sizeof(dst)) {
            ocse_outln("mv: destination path too long");
            return 1;
        }
        if (need_slash) { dst[dl++] = '/'; dst[dl] = 0; }
        memcpy(dst + dl, base, strlen(base) + 1);
    }
    if (strcmp(src, dst) == 0) {
        ocse_outln("mv: source and destination are the same file");
        return 1;
    }
    /* same parent -> real rename; different parents -> copy+unlink
     * for files (fs_vfs_rename hands the OLD parent to the fs rename
     * op, so a cross-parent call would rename inside the old dir) */
    int sl = (int)strlen(src), dl2 = (int)strlen(dst);
    int sp = 0, dp = 0;
    for (int i = sl - 1; i >= 0; i--) if (src[i] == '/') { sp = i; break; }
    for (int i = dl2 - 1; i >= 0; i--) if (dst[i] == '/') { dp = i; break; }
    if (sp == dp && strncmp(src, dst, (usize)sp) == 0) {
        if (fs_vfs_rename(src, dst) == 0) return 0;
        /* fall through: copy + unlink for regular files */
    }
    if (ss.type != VFS_TYPE_FILE) {
        ocse_outln("mv: moving a directory across parents is not supported");
        return 1;
    }
    u64 total = 0;
    if (cl_copy_file(src, dst, &total) != 0) {
        ocse_outln("mv: copy phase failed");
        return 1;
    }
    if (fs_vfs_unlink(src) != 0) {
        ocse_outln("mv: copied, but could not remove the source");
        return 1;
    }
    return 0;
}

/* mkdir -p semantics over a resolved absolute path */
static int cl_mkdir_p(const char* dir) {
    char tmp[VFS_PATH_LEN];
    int l = (int)strlen(dir);
    if (l <= 0 || l >= (int)sizeof(tmp)) return -1;
    memcpy(tmp, dir, (usize)(l + 1));
    if (l > 1 && tmp[l - 1] == '/') tmp[--l] = 0;
    for (int i = 1; i <= l; i++) {
        if (tmp[i] == '/' || tmp[i] == 0) {
            char save = tmp[i];
            tmp[i] = 0;
            if (tmp[0]) fs_vfs_mkdir(tmp);   /* EEXIST is fine */
            tmp[i] = save;
        }
    }
    fs_vfs_stat_t st;
    if (fs_vfs_stat(dir, &st) != 0 || st.type != VFS_TYPE_DIR) return -1;
    return 0;
}

/* ---- tar (ustar list/extract via the kernel's streaming reader) ---- */
typedef struct {
    char base[VFS_PATH_LEN];    /* destdir prefix; empty = entry paths */
    int  list;                  /* -t */
    int  fail;
    int  fd;                    /* current output file, -1 = none */
    u64  off;                   /* bytes written to the current file */
    u64  size;                  /* announced size of the current file */
    char cur[VFS_PATH_LEN];     /* current file's full target path */
} cl_tar_ctx;

static int cl_tar_cb(void* vctx, const char* epath, int type, u64 size,
                     const u8* data, int slice_len) {
    cl_tar_ctx* c = (cl_tar_ctx*)vctx;
    char path[VFS_PATH_LEN];
    if (c->base[0]) {
        int bl = (int)strlen(c->base);
        int el = (int)strlen(epath);
        if (bl + 1 + el + 1 > (int)sizeof(path)) { c->fail = 1; return 1; }
        memcpy(path, c->base, (usize)bl);
        path[bl++] = '/';
        memcpy(path + bl, epath, (usize)(el + 1));
    } else {
        int el = (int)strlen(epath);
        if (el + 1 > (int)sizeof(path)) { c->fail = 1; return 1; }
        memcpy(path, epath, (usize)(el + 1));
    }
    if (c->list) {
        char num[24];
        if (type == OC_TAR_DIR) {
            ocse_out("  [D] ");
        } else {
            u64_to_str(size, num);
            int nl = (int)strlen(num);
            ocse_out("  ");
            for (int i = nl; i < 10; i++) ocse_out(" ");
            ocse_out(num);
            ocse_out(" ");
        }
        ocse_outln(path);
        return 0;
    }
    if (type == OC_TAR_DIR) {
        if (cl_mkdir_p(path) != 0) { c->fail = 2; return 1; }
        return 0;
    }
    if (data && slice_len > 0) {
        if (c->fd < 0) {
            /* first slice: ensure the parent chain, then create */
            char parent[VFS_PATH_LEN];
            int pl = (int)strlen(path);
            int cut = pl;
            while (cut > 0 && path[cut - 1] != '/') cut--;
            if (cut <= 0) { c->fail = 3; return 1; }
            if (cut == 1) { parent[0] = '/'; parent[1] = 0; }
            else { memcpy(parent, path, (usize)(cut - 1)); parent[cut - 1] = 0; }
            if (cl_mkdir_p(parent) != 0) { c->fail = 3; return 1; }
            c->fd = fs_vfs_open(path, VFS_O_WRONLY | VFS_O_CREAT | VFS_O_TRUNC);
            if (c->fd < 0) { c->fail = 4; return 1; }
            c->off = 0;
            c->size = size;
            strncpy(c->cur, path, sizeof(c->cur) - 1);
            c->cur[sizeof(c->cur) - 1] = 0;
        }
        int w = fs_vfs_write(c->fd, data, slice_len);
        if (w != slice_len) { c->fail = 5; return 1; }
        c->off += (u64)slice_len;
        return 0;
    }
    /* slice_len == 0: end-of-file marker */
    if (c->fd >= 0) {
        fs_vfs_close(c->fd);
        c->fd = -1;
        char n[24];
        ocse_out("  extracted ");
        ocse_out(c->cur);
        ocse_out(" (");
        u64_to_str(c->off, n); ocse_out(n);
        ocse_outln(" bytes)");
        c->off = 0;
    } else if (c->size == 0 && size == 0) {
        /* empty file: only the marker arrived */
        char parent[VFS_PATH_LEN];
        int pl = (int)strlen(path);
        int cut = pl;
        while (cut > 0 && path[cut - 1] != '/') cut--;
        if (cut > 0) {
            if (cut == 1) { parent[0] = '/'; parent[1] = 0; }
            else { memcpy(parent, path, (usize)(cut - 1)); parent[cut - 1] = 0; }
            cl_mkdir_p(parent);
        }
        int fd = fs_vfs_open(path, VFS_O_WRONLY | VFS_O_CREAT | VFS_O_TRUNC);
        if (fd < 0) { c->fail = 4; return 1; }
        fs_vfs_close(fd);
        ocse_out("  extracted ");
        ocse_out(path);
        ocse_outln(" (0 bytes)");
    }
    return 0;
}

/* Shared engine (the `install` state machine calls the extract side
 * too). apath/base are resolved ABSOLUTE paths; base empty = entry
 * paths as-is, "/" normalized to empty. */
static int tar_run(const char* apath, const char* base, int list) {
    cl_tar_ctx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.list = list;
    ctx.fd = -1;
    if (base && base[0]) {
        strncpy(ctx.base, base, sizeof(ctx.base) - 1);
        ctx.base[sizeof(ctx.base) - 1] = 0;
    }
    int fd = fs_vfs_open(apath, VFS_O_RDONLY);
    if (fd < 0) {
        ocse_out("tar: cannot open ");
        ocse_outln(apath);
        return 1;
    }
    lib_tar_t* t = lib_tar_open(cl_tar_cb, &ctx);
    if (!t) { fs_vfs_close(fd); ocse_outln("tar: out of memory"); return 1; }
    int rc = 0;
    for (;;) {
        int n = fs_vfs_read(fd, g_io_buf, CL_IOBUF_SZ);
        if (n < 0) { rc = -100; break; }
        if (n == 0) break;
        int f = lib_tar_feed(t, g_io_buf, n);
        if (f != 0) { rc = f; break; }
    }
    if (rc == 0) rc = lib_tar_finish(t);
    lib_tar_close(t);
    fs_vfs_close(fd);
    if (ctx.fd >= 0) fs_vfs_close(ctx.fd);
    if (rc != 0 || ctx.fail) {
        char n[24];
        ocse_out("tar: archive error (");
        u64_to_str((u64)(-(rc ? rc : ctx.fail)), n); ocse_out(n);
        ocse_outln(")");
        return 1;
    }
    return 0;
}

/* Public extract entry for the installer (ocse_inst.c). */
int ocse_cl_tar_extract(const char* archive, const char* destdir) {
    char apath[VFS_PATH_LEN];
    char base[VFS_PATH_LEN];
    if (cl_resolve(archive, apath, sizeof(apath)) != 0) {
        ocse_outln("tar: bad archive path");
        return 1;
    }
    if (!destdir || cl_resolve(destdir, base, sizeof(base)) != 0) {
        ocse_outln("tar: bad destdir");
        return 1;
    }
    fs_vfs_stat_t st;
    if (fs_vfs_stat(base, &st) != 0 || st.type != VFS_TYPE_DIR) {
        ocse_outln("tar: destdir is not a directory");
        return 1;
    }
    if (strcmp(base, "/") == 0) base[0] = 0;
    return tar_run(apath, base, 0);
}

static int cmd_tar(int argc, char** argv) {
    if (argc < 3) {
        ocse_outln("usage: tar -t <archive>             list entries");
        ocse_outln("       tar -x <archive> [destdir]   extract");
        return 1;
    }
    int list;
    if (strcmp(argv[1], "-t") == 0) list = 1;
    else if (strcmp(argv[1], "-x") == 0) list = 0;
    else { ocse_outln("tar: mode must be -t (list) or -x (extract)"); return 1; }
    const char* dest = (argc >= 4) ? argv[3] : NULL;
    char apath[VFS_PATH_LEN];
    if (cl_resolve(argv[2], apath, sizeof(apath)) != 0) {
        ocse_outln("tar: bad archive path");
        return 1;
    }
    char base[VFS_PATH_LEN];
    base[0] = 0;
    if (!list) {
        if (!dest) {
            strncpy(base, g_cwd, sizeof(base) - 1);
            base[sizeof(base) - 1] = 0;
        } else {
            if (cl_resolve(dest, base, sizeof(base)) != 0) {
                ocse_outln("tar: bad destdir");
                return 1;
            }
            fs_vfs_stat_t st;
            if (fs_vfs_stat(base, &st) != 0 || st.type != VFS_TYPE_DIR) {
                ocse_outln("tar: destdir is not a directory");
                return 1;
            }
            if (strcmp(base, "/") == 0) base[0] = 0;
        }
    }
    return tar_run(apath, base, list);
}

/* ---- dd: block copy between devices and files ---- */
typedef struct {
    int        is_dev;
    int        dev;        /* dev_idx when is_dev */
    int        fd;         /* VFS fd when !is_dev */
    u64        sects;      /* device capacity (sectors) */
    const char* name;
} cl_dd_end;

static int cl_dd_parse_end(const char* name, cl_dd_end* e, int for_write) {
    e->name = name;
    e->fd = -1;
    e->dev = -1;
    e->sects = 0;
    int d = driver_block_find_device(name);
    if (d >= 0) {
        e->is_dev = 1;
        e->dev = d;
        driver_block_device_t* dv = driver_block_get_device(d);
        e->sects = dv ? dv->sectors : 0;
        return 0;
    }
    e->is_dev = 0;
    char path[VFS_PATH_LEN];
    if (cl_resolve(name, path, sizeof(path)) != 0) return -1;
    int fd = fs_vfs_open(path, for_write
                         ? (VFS_O_WRONLY | VFS_O_CREAT | VFS_O_TRUNC)
                         : VFS_O_RDONLY);
    if (fd < 0) return -2;
    e->fd = fd;
    return 0;
}

static u64 cl_parse_num(const char* s) {
    u64 v = 0;
    int i = 0;
    while (s[i] >= '0' && s[i] <= '9' && i < 16) v = v * 10 + (u64)(s[i++] - '0');
    return (s[i] || i == 0) ? 0 : v;
}

static int cmd_dd(int argc, char** argv) {
    const char* ifs = NULL;
    const char* ofs = NULL;
    u64 bs = 512, count = 0, skip = 0, seek = 0;
    int confirm = 0;
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "if=", 3) == 0) ifs = argv[i] + 3;
        else if (strncmp(argv[i], "of=", 3) == 0) ofs = argv[i] + 3;
        else if (strncmp(argv[i], "bs=", 3) == 0) bs = cl_parse_num(argv[i] + 3);
        else if (strncmp(argv[i], "count=", 6) == 0) count = cl_parse_num(argv[i] + 6);
        else if (strncmp(argv[i], "skip=", 5) == 0) skip = cl_parse_num(argv[i] + 5);
        else if (strncmp(argv[i], "seek=", 5) == 0) seek = cl_parse_num(argv[i] + 5);
        else if (strcmp(argv[i], "confirm") == 0) confirm = 1;
        else {
            ocse_out("dd: unknown argument: ");
            ocse_outln(argv[i]);
            return 1;
        }
    }
    if (!ifs || !ofs) {
        ocse_outln("usage: dd if=<dev|file> of=<dev|file> [bs=N] [count=N] [skip=N] [seek=N] [confirm]");
        return 1;
    }
    if (bs < 512 || bs > CL_IOBUF_SZ || bs == 0) {
        ocse_outln("dd: bs must be 512..65536");
        return 1;
    }
    cl_dd_end in, out;
    if (cl_dd_parse_end(ifs, &in, 0) != 0) {
        ocse_out("dd: cannot open input: ");
        ocse_outln(ifs);
        return 1;
    }
    if (cl_dd_parse_end(ofs, &out, 1) != 0) {
        if (in.fd >= 0) fs_vfs_close(in.fd);
        ocse_out("dd: cannot open output: ");
        ocse_outln(ofs);
        return 1;
    }
    /* Safety gates: a mounted device is never a dd target; a raw
     * device target needs the BUG-0239-style confirm token. */
    if (out.is_dev) {
        if (mkfs_dev_mounted(out.name)) {
            if (in.fd >= 0) fs_vfs_close(in.fd);
            ocse_out("dd: '");
            ocse_out(out.name);
            ocse_outln("' is mounted - umount it first");
            return 1;
        }
        if (!confirm) {
            if (in.fd >= 0) fs_vfs_close(in.fd);
            ocse_outln("dd: writing a raw device DESTROYS DATA - append 'confirm' to proceed");
            ocse_out("dd: ");
            ocse_outln(ocse_tr(OCSE_T_M_NOWRITE));
            return 1;
        }
    }
    if ((in.is_dev || out.is_dev) && bs % 512 != 0) {
        if (in.fd >= 0) fs_vfs_close(in.fd);
        if (out.fd >= 0) fs_vfs_close(out.fd);
        ocse_outln("dd: bs must be a multiple of 512 when a device is involved");
        return 1;
    }
    if (in.is_dev && count == 0) {
        if (in.fd >= 0) fs_vfs_close(in.fd);
        if (out.fd >= 0) fs_vfs_close(out.fd);
        ocse_outln("dd: count= is required when reading from a device");
        return 1;
    }
    if (in.is_dev && skip * (bs / 512) + count * (bs / 512) > in.sects) {
        if (in.fd >= 0) fs_vfs_close(in.fd);
        if (out.fd >= 0) fs_vfs_close(out.fd);
        ocse_outln("dd: read range beyond the end of the device");
        return 1;
    }
    if (!in.is_dev) fs_vfs_seek(in.fd, (i64)(skip * bs), VFS_SEEK_SET);
    if (!out.is_dev) fs_vfs_seek(out.fd, (i64)(seek * bs), VFS_SEEK_SET);
    u64 in_lba = skip * (bs / 512);
    u64 out_lba = seek * (bs / 512);
    u64 done = 0, total = 0;
    int rc = 0;
    while (count == 0 || done < count) {
        int n;
        if (in.is_dev) {
            u32 sects = (u32)(bs / 512);
            if (in_lba + sects > in.sects) break;
            if (driver_block_read_sectors(in.dev, in_lba, sects, g_io_buf) != 0) {
                rc = -10; break;
            }
            n = (int)bs;
        } else {
            n = fs_vfs_read(in.fd, g_io_buf, (int)bs);
            if (n < 0) { rc = -11; break; }
        }
        if (n == 0) break;
        if (out.is_dev) {
            u32 sects = (u32)((n + 511) / 512);
            if (out_lba + sects > out.sects) { rc = -12; break; }
            if (n % 512 != 0) {
                for (int i = n; i < (int)(sects * 512); i++) g_io_buf[i] = 0;
            }
            if (driver_block_write_sectors(out.dev, out_lba, sects, g_io_buf) != 0) {
                rc = -13; break;
            }
            out_lba += sects;
        } else {
            int w = fs_vfs_write(out.fd, g_io_buf, n);
            if (w != n) { rc = -14; break; }
        }
        if (in.is_dev) in_lba += bs / 512;
        done++;
        total += (u64)n;
        if (!in.is_dev && n < (int)bs) break;   /* short read = EOF */
    }
    if (in.fd >= 0) fs_vfs_close(in.fd);
    if (out.fd >= 0) fs_vfs_close(out.fd);
    if (rc != 0) {
        char n[24];
        ocse_out("dd: I/O error (");
        u64_to_str((u64)(-rc), n); ocse_out(n);
        ocse_outln(")");
        return 1;
    }
    char n1[24], n2[24];
    ocse_out("dd: copied ");
    u64_to_str(done, n1); ocse_out(n1);
    ocse_out(" block(s), ");
    u64_to_str(total, n2); ocse_out(n2);
    ocse_out(" byte(s), if=");
    ocse_out(ifs);
    ocse_out(" of=");
    ocse_outln(ofs);
    return 0;
}

/* ---- sha256sum: VFS files or the first N sectors of a device ---- */
static int cmd_sha256sum(int argc, char** argv) {
    if (argc < 2) {
        ocse_outln("usage: sha256sum <file>");
        ocse_outln("       sha256sum <device> <sectors>   (first N sectors)");
        return 1;
    }
    u8 h[32];
    char hex[65];
    int d = driver_block_find_device(argv[1]);
    if (d >= 0) {
        if (argc < 3) {
            ocse_outln("sha256sum: device form needs a sector count");
            return 1;
        }
        u64 cnt = cl_parse_num(argv[2]);
        if (cnt == 0) {
            ocse_outln("sha256sum: bad sector count");
            return 1;
        }
        driver_block_device_t* dv = driver_block_get_device(d);
        if (!dv || cnt > dv->sectors) {
            ocse_outln("sha256sum: range beyond the end of the device");
            return 1;
        }
        ocse_sha256_ctx c;
        ocse_sha256_init(&c);
        u64 done = 0;
        while (done < cnt) {
            u32 chunk = 128;
            if (done + chunk > cnt) chunk = (u32)(cnt - done);
            if (driver_block_read_sectors(d, done, chunk, g_io_buf) != 0) {
                ocse_outln("sha256sum: read error");
                return 1;
            }
            ocse_sha256_update(&c, g_io_buf, (int)(chunk * 512));
            done += chunk;
        }
        ocse_sha256_final(&c, h);
        ocse_sha256_hex(h, hex);
        ocse_out(hex);
        ocse_out("  ");
        ocse_outln(argv[1]);
        return 0;
    }
    char path[VFS_PATH_LEN];
    if (cl_resolve(argv[1], path, sizeof(path)) != 0) {
        ocse_outln("sha256sum: bad path");
        return 1;
    }
    int fd = fs_vfs_open(path, VFS_O_RDONLY);
    if (fd < 0) {
        ocse_out("sha256sum: cannot open ");
        ocse_outln(path);
        return 1;
    }
    ocse_sha256_ctx c;
    ocse_sha256_init(&c);
    for (;;) {
        int n = fs_vfs_read(fd, g_io_buf, CL_IOBUF_SZ);
        if (n < 0) { fs_vfs_close(fd); ocse_outln("sha256sum: read error"); return 1; }
        if (n == 0) break;
        ocse_sha256_update(&c, g_io_buf, n);
    }
    fs_vfs_close(fd);
    ocse_sha256_final(&c, h);
    ocse_sha256_hex(h, hex);
    ocse_out(hex);
    ocse_out("  ");
    ocse_outln(path);
    return 0;
}

/* ---- WP-06b batch-5: the installer commands ---- */
static int cmd_ocbl_install(int argc, char** argv) {
    if (argc < 2) {
        ocse_outln("ocbl-install: writes the OCBL boot code - append 'confirm'");
        ocse_outln("usage: ocbl-install <disk> confirm");
        return 1;
    }
    int confirm = (argc >= 3 && strcmp(argv[2], "confirm") == 0);
    if (!confirm) {
        ocse_outln("ocbl-install: OVERWRITES the boot area - append 'confirm'");
        ocse_outln("ocbl-install: Nothing was written.");
        return 1;
    }
    int idx = ocse_part_disk_idx(argv[1]);
    if (idx < 0) {
        ocse_out("ocbl-install: no such disk: ");
        ocse_outln(argv[1]);
        return 1;
    }
    return (ocse_inst_ocbl(idx, "/install/OCBLMBR.BIN",
                           g_io_buf, CL_IOBUF_SZ) == 0) ? 0 : 1;
}

static int cmd_install(int argc, char** argv) {
    if (argc < 3) {
        ocse_outln("usage: install <disk> <n> confirm");
        ocse_outln("       (target partition must be FAT32 and mounted)");
        return 1;
    }
    int n = 0, i = 0;
    while (argv[2][i] >= '0' && argv[2][i] <= '9' && i < 4) {
        n = n * 10 + (argv[2][i] - '0'); i++;
    }
    if (argv[2][i] || i == 0 || n < 1) {
        ocse_outln("install: bad partition number");
        return 1;
    }
    int confirm = (argc >= 4 && strcmp(argv[3], "confirm") == 0);
    return ocse_inst_install(argv[1], n, confirm, g_io_buf, CL_IOBUF_SZ);
}

static int cmd_config(int argc, char** argv) {
    if (argc >= 2 && strcmp(argv[1], "set") == 0) {
        if (argc != 4) {
            ocse_outln("usage: config set <key> <value>");
            ocse_outln("       keys: hostname <name> | lang en|zh | boot.slot A|B");
            return 1;
        }
        return ocse_inst_config_set(argv[2], argv[3]);
    }
    if (argc >= 2) {
        ocse_outln("usage: config                show the installed config");
        ocse_outln("       config set <key> <value>");
        return 1;
    }
    return ocse_inst_config_show();
}

/* ---- sync: flush the whole block cache to the media ---- */
static int cmd_sync(int argc, char** argv) {
    (void)argc; (void)argv;
    core_power_flush_blk();
    ocse_outln(ocse_tr(OCSE_T_S_FLUSHED));
    return 0;
}

/* ---- help ---- */
static int cmd_help(int argc, char** argv);

/* ---- the table (order = help listing order) ----
 * halt/gui have dedicated handlers; both switch the world (halt
 * never returns, gui leaves the loop), so they are real entries
 * with their own fn slots. */
static int cmd_halt(int argc, char** argv) {
    (void)argc; (void)argv;
    ocse_outln(ocse_tr(OCSE_T_HALT));
    core_power_halt();                        /* never returns */
    return 0;
}
static int cmd_gui(int argc, char** argv) {
    (void)argc; (void)argv;
    return 0x100;                               /* "go to GUI" */
}

static const cl_cmd_t g_cmds[] = {
    { "help",     "show all commands or one command's help", cmd_help },
    { "lsdisk",   "list install-target disks",              cmd_lsdisk },
    { "lspart",   "list partitions of <disk>",              cmd_lspart },
    { "parted",   "show the raw partition table of <disk>", cmd_parted },
    { "mkpart",   "create a partition: mkpart <disk> <startMiB> <endMiB>", cmd_mkpart },
    { "rmpart",   "delete partition #n: rmpart <disk> <n>",  cmd_rmpart },
    { "setflag",  "set a flag: setflag <disk> <n> boot [on|off]", cmd_setflag },
    { "mkfs.fat32", "format <device> FAT32 (DESTROYS DATA; needs 'confirm')", cmd_mkfs_fat32 },
    { "mkfs.exfat", "format <device> exFAT (DESTROYS DATA; needs 'confirm')", cmd_mkfs_exfat },
    { "mkfs.ext4",  "format <device> ext4 (DESTROYS DATA; needs 'confirm')", cmd_mkfs_ext4 },
    { "mount",    "mount <device> <path> (filesystem auto-detected)", cmd_mount },
    { "umount",   "unmount the filesystem at <path>", cmd_umount },
    { "ls",       "list a directory: ls [path]",                   cmd_ls },
    { "cd",       "change directory: cd [path] (default /)",       cmd_cd },
    { "pwd",      "print the working directory",                   cmd_pwd },
    { "cat",      "print a text file: cat <file>",                 cmd_cat },
    { "write",    "write one line: write <file> [text ...]",       cmd_write },
    { "touch",    "create an empty file: touch <file>",            cmd_touch },
    { "cp",       "copy a file: cp <src> <dst>",                   cmd_cp },
    { "mv",       "move or rename a file: mv <src> <dst>",         cmd_mv },
    { "rm",       "delete a file: rm <file>",                      cmd_rm },
    { "mkdir",    "create a directory: mkdir <path>",              cmd_mkdir },
    { "rmdir",    "delete an empty directory: rmdir <path>",       cmd_rmdir },
    { "tar",      "ustar tool: tar -t <arc> | tar -x <arc> [dir]", cmd_tar },
    { "dd",       "block copy: dd if= of= [bs= count= skip= seek=] [confirm]", cmd_dd },
    { "sha256sum", "SHA-256 digest: sha256sum <file> | <dev> <sectors>", cmd_sha256sum },
    { "install",  "install OCOS onto a partition: install <disk> <n> confirm", cmd_install },
    { "ocbl-install", "write the OCBL boot code: ocbl-install <disk> confirm", cmd_ocbl_install },
    { "config",   "show/edit the installed config: config [set <k> <v>]", cmd_config },
    { "sync",     "flush the block cache to the disks",         cmd_sync },
    { "reboot",   "flush disks and reboot the machine",     cmd_reboot },
    { "shutdown", "flush disks and power off",              cmd_shutdown },
    { "hwcheck",  "re-run the four hardware checks",         cmd_hwcheck },
    { "disks",    "raw disk inventory (WP-06a view)",        cmd_disks },
    { "lang",     "show or set the language: lang [en|zh]", cmd_lang },
    { "serial_encoding", "show/set the serial line encoding: serial_encoding [utf8|gbk]", cmd_serial_encoding },
    { "clear",    "clear the screen",                        cmd_clear },
    { "click",    "probe the GUI button geometry: click <x> <y>", cmd_click },
    { "gui",      "switch back to the graphical panel",      cmd_gui },
    { "halt",     "stop the CPU (no ACPI path)",             cmd_halt },
    { NULL, NULL, NULL }
};

static const cl_cmd_t* cl_find(const char* name) {
    for (const cl_cmd_t* c = &g_cmds[0]; c->name; c++)
        if (strcmp(c->name, name) == 0) return c;
    return NULL;
}

static int cmd_help(int argc, char** argv) {
    if (argc >= 2) {
        const cl_cmd_t* c = cl_find(argv[1]);
        if (!c) { cl_unknown(argv[1]); return 1; }
        ocse_out(c->name);
        ocse_out(" - ");
        ocse_outln(c->help);
        return 0;
    }
    ocse_outln(ocse_tr(OCSE_T_CLI_BANNER));
    for (const cl_cmd_t* c = &g_cmds[0]; c->name; c++) {
        ocse_out("  ");
        ocse_out(c->name);
        int pad = 14 - (int)strlen(c->name);
        while (pad-- > 0) ocse_out(" ");
        ocse_out(" ");
        ocse_outln(c->help);
    }
    return 0;
}

/* ==================================================================
 * Tokenizer + dispatch
 * ================================================================== */
static int split_args(char* line, char** argv, int max) {
    int argc = 0;
    char* p = line;
    while (*p && argc < max) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) *p++ = 0;
    }
    return argc;
}

/* Returns 0 normally; 0x100 = switch to GUI. */
static int run_line(char* line) {
    char* argv[CL_MAXARGS];
    int argc = split_args(line, argv, CL_MAXARGS);
    if (argc == 0) return 0;
    const cl_cmd_t* c = cl_find(argv[0]);
    if (!c) { cl_unknown(argv[0]); return 1; }
    return c->fn(argc, argv);
}

/* ==================================================================
 * WP-06c: the wizard's command channel. One line, same tokenizer/
 * table/handlers, output captured (console silent, serial mirrors).
 * The line is copied first: handlers mutate their argv in place and
 * the wizard's buffers must not be aliased.
 * ================================================================== */
int ocse_cl_exec(const char* line, char* out, int outsz) {
    char buf[CL_BUF];
    int l = (int)strlen(line);
    if (l >= CL_BUF) return 1;
    memcpy(buf, line, (usize)l + 1);
    ocse_out_capture(out, outsz);
    int rc = run_line(buf);
    ocse_out_capture_off();
    return rc;
}

/* ==================================================================
 * Tab completion: commands (first word) or cwd entries (later words)
 * ================================================================== */
static void complete_word(char* line, int* llen, int* cur) {
    int wstart = *cur;
    while (wstart > 0 && line[wstart - 1] != ' ') wstart--;
    char* word = line + wstart;
    int wlen = *cur - wstart;
    if (wlen <= 0) return;
    int found = 0;
    char match[CL_BUF];
    match[0] = 0;
    if (wstart == 0) {
        /* command names */
        for (const cl_cmd_t* c = &g_cmds[0]; c->name; c++) {
            if (strncmp(c->name, word, (u64)wlen) == 0) {
                if (found == 0) { strncpy(match, c->name, CL_BUF - 1); }
                else {
                    int j = 0;
                    while (j < (int)strlen(match) && match[j] == c->name[j]) j++;
                    match[j] = 0;
                }
                found++;
            }
        }
    }
    /* file-name completion (batch-4): resolve the directory part of
     * the word against the cwd and match the entries of that dir. */
    else {
        int wsl = 0;                     /* chars of dir part in word */
        for (int i = wlen - 1; i >= 0; i--)
            if (word[i] == '/') { wsl = i + 1; break; }
        char dirp[VFS_PATH_LEN];
        if (wsl == 0) {
            strncpy(dirp, g_cwd, sizeof(dirp) - 1);
            dirp[sizeof(dirp) - 1] = 0;
        } else {
            if (wsl >= (int)sizeof(dirp)) wsl = (int)sizeof(dirp) - 1;
            memcpy(dirp, word, (usize)wsl);
            dirp[wsl] = 0;
        }
        char rdir[VFS_PATH_LEN];
        if (cl_resolve(dirp, rdir, sizeof(rdir)) != 0) { line[*llen] = 0; return; }
        fs_vfs_stat_t dst;
        if (fs_vfs_stat(rdir, &dst) != 0 || dst.type != VFS_TYPE_DIR) {
            line[*llen] = 0; return;
        }
        const char* prefix = word + wsl;
        int plen = wlen - wsl;
        char match[VFS_NAME_LEN];
        int found = 0, last_type = 0;
        match[0] = 0;
        for (int i = 0; ; i++) {
            fs_vfs_dirent_t e;
            if (fs_vfs_readdir(rdir, i, &e) != 0) break;
            if (plen > 0 && strncmp(e.name, prefix, (u64)plen) != 0) continue;
            if (found == 0) {
                strncpy(match, e.name, VFS_NAME_LEN - 1);
                match[VFS_NAME_LEN - 1] = 0;
            } else {
                int j = 0;
                while (j < (int)strlen(match) && match[j] == e.name[j]) j++;
                match[j] = 0;
            }
            last_type = e.type;
            found++;
        }
        if (found >= 1 && match[0] && (int)strlen(match) > plen) {
            int ml = (int)strlen(match);
            for (int i = plen; i < ml && *llen < CL_BUF - 2; i++) {
                line[(*llen)++] = match[i];
                char one[2] = { match[i], 0 };
                le_write(one);
            }
            *cur = *llen;
            if (found == 1 && *llen < CL_BUF - 2) {
                char ap = (last_type == VFS_TYPE_DIR) ? '/' : ' ';
                line[(*llen)++] = ap;
                char one[2] = { ap, 0 };
                le_write(one);
                *cur = *llen;
            }
        } else if (found > 1 && plen == 0) {
            /* no common prefix: list the candidates */
            le_write("\n");
            for (int i = 0; ; i++) {
                fs_vfs_dirent_t e;
                if (fs_vfs_readdir(rdir, i, &e) != 0) break;
                ocse_out("  ");
                ocse_outln(e.name);
            }
            le_write(g_le_prompt);
            le_write(line);
            *cur = *llen;
        }
        line[*llen] = 0;
        return;                 /* path branch is self-contained */
    }
    if (found == 1 || (found > 1 && match[0])) {
        int ml = (int)strlen(match);
        if (ml > wlen) {
            for (int i = wlen; i < ml && *llen < CL_BUF - 2; i++) {
                line[(*llen)++] = match[i];
                char one[2] = { match[i], 0 };
                le_write(one);
            }
            *cur = *llen;
            if (found == 1 && *llen < CL_BUF - 2) {
                line[(*llen)++] = ' ';
                le_write(" ");
                *cur = *llen;
            }
        } else if (found > 1) {
            le_write("\n");
            for (const cl_cmd_t* c = &g_cmds[0]; c->name; c++)
                if (strncmp(c->name, word, (u64)wlen) == 0) {
                    ocse_out("  ");
                    ocse_outln(c->name);
                }
            le_write(g_le_prompt);
            le_write(line);
            *cur = *llen;
        }
    }
    line[*llen] = 0;
}

/* ==================================================================
 * The loop
 * ================================================================== */
static int read_line(char* line) {
    /* prompt-aware editor loop; returns length */
    int llen = 0, cur = 0;
    int hist_nav = -1;
    char draft[CL_BUF];
    line[0] = 0;
    for (;;) {
        if (g_ocse_hotkey_req)
            return -1;                          /* caller switches */
        if (!driver_input_keyboard_has_key()) {
            core_sched_yield();
            continue;
        }
        int k = driver_input_keyboard_getch();
        if (k == '\r' || k == '\n') {
            le_write("\n");
            return llen;
        }
        if (k == 0x03) {                        /* Ctrl+C */
            le_write("^C\n");
            return -2;
        }
        if (k == 0x80) {                        /* Up */
            if (g_hist_n > 0) {
                if (hist_nav == -1) {
                    strncpy(draft, line, CL_BUF - 1);
                    hist_nav = g_hist_n - 1;
                } else if (hist_nav > 0) hist_nav--;
                else continue;
                strncpy(line, g_hist[hist_nav], CL_BUF - 1);
                line[CL_BUF - 1] = 0;
                llen = cur = (int)strlen(line);
                le_redraw(line, llen, cur);
            }
            continue;
        }
        if (k == 0x81) {                        /* Down */
            if (hist_nav != -1) {
                if (hist_nav < g_hist_n - 1) {
                    hist_nav++;
                    strncpy(line, g_hist[hist_nav], CL_BUF - 1);
                } else {
                    hist_nav = -1;
                    strncpy(line, draft, CL_BUF - 1);
                }
                line[CL_BUF - 1] = 0;
                llen = cur = (int)strlen(line);
                le_redraw(line, llen, cur);
            }
            continue;
        }
        if (k == 0x82) { if (cur > 0) { cur--; le_redraw(line, llen, cur); } continue; }
        if (k == 0x83) { if (cur < llen) { cur++; le_redraw(line, llen, cur); } continue; }
        if (k == 0x84 || k == 0x01) { cur = 0; le_redraw(line, llen, cur); continue; }
        if (k == 0x85 || k == 0x05) { cur = llen; le_redraw(line, llen, cur); continue; }
        if (k == 0x89) { cur = le_delete(line, llen, cur); llen = (int)strlen(line); continue; }
        if (k == 0x15) {                        /* Ctrl+U */
            if (cur > 0) {
                int rest = llen - cur;
                for (int i = 0; i < rest; i++) line[i] = line[cur + i];
                llen = rest; line[llen] = 0; cur = 0;
                le_redraw(line, llen, cur);
            }
            continue;
        }
        if (k == 0x0B) {                        /* Ctrl+K */
            if (cur < llen) {
                llen = cur; line[llen] = 0;
                le_redraw(line, llen, cur);
            }
            continue;
        }
        if (k == 0x17) {                        /* Ctrl+W */
            if (cur > 0) {
                int p2 = cur;
                while (p2 > 0 && line[p2 - 1] == ' ') p2--;
                while (p2 > 0 && line[p2 - 1] != ' ') p2--;
                int removed = cur - p2;
                int rest = llen - cur;
                for (int i = 0; i < rest; i++) line[p2 + i] = line[cur + i];
                llen -= removed; line[llen] = 0; cur = p2;
                le_redraw(line, llen, cur);
            }
            continue;
        }
        if (k == 0x08 || k == 0x7F) {           /* Backspace */
            cur = le_backspace(line, llen, cur);
            llen = (int)strlen(line);
            continue;
        }
        if (k == '\t') {                        /* Tab */
            if (cur == llen) complete_word(line, &llen, &cur);
            continue;
        }
        if (k >= 0x20 && k < 0x7F) {
            cur = le_insert(line, llen, cur, (char)k, CL_BUF);
            llen = (int)strlen(line);
        }
    }
}

int ocse_cl_loop(void) {
    char line[CL_BUF];
    ocse_out("ocsecl> ");
    for (;;) {
        g_le_prompt = "ocsecl> ";
        g_le_prevlen = 0;
        int rc = read_line(line);
        if (rc == -1)
            return OCSE_ACT_GOTO_GUI;           /* hotkey F1 */
        if (rc >= 0 && line[0]) {
            hist_add(line);
            int act = run_line(line);
            if (act == 0x100)
                return OCSE_ACT_GOTO_GUI;
        }
        ocse_out("ocsecl> ");
    }
}
