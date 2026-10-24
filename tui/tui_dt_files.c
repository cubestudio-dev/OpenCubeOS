/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-10-tui-native
 * File: tui/tui_dt_files.c
 * Purpose: the desktop file manager - a real VFS browser over
 *          fs_vfs_readdir/stat/mkdir/unlink/rename and fd-based file
 *          copy.  Purely keyboard driven; every action reports OK/FAIL
 *          through the desktop toast.
 *
 * WP-10-tui-native upgrades (all native, no command bridge):
 *   - delete asks for confirmation (tui_dt_confirm dialog)
 *   - a native text viewer (V) with scrolling, UTF-8/CJK rendering
 *   - the mount page lists mounts AND block devices and performs
 *     real umount / mount actions through fs_vfs_umount/mount
 *   - copy is recursive for directories (mkdir + per-entry walk)
 *   - every prompt, toast and page is bilingual (tui_tr)
 */
#include "tui_desktop.h"
#include "../services/fs/fs_vfs.h"
#include "../core/lib/lib_string.h"
#include "../core/mem/mem_heap.h"
#include "../drivers/input/driver_input_keyboard.h"
#include "../drivers/block/driver_block_blk.h"

#define FL_MAX      128                  /* entries per directory      */

/* the kernel string lib has no strrchr */
static char *fl_strrchr(char *s, char c) {
    char *last = (char *)0;
    for (; *s; s++) if (*s == c) last = s;
    return last;
}
#define FL_PATH_MAX 256
#define FL_CELLS    4                    /* 4KB copy chunk (heap)      */

static char     g_cwd[FL_PATH_MAX] = "/";
/* per-directory listing buffers live on the heap (identity-window .bss
 * is reserved for small state) */
static char    *g_names = NULL;
static int     *g_types = NULL;
static u64     *g_sizes = NULL;
static int      g_count = 0;

static int fl_buf_alloc(void) {
    if (g_names) return 0;
    g_names = kmalloc((u64)FL_MAX * VFS_NAME_LEN);
    g_types = kmalloc((u64)FL_MAX * sizeof(int));
    g_sizes = kmalloc((u64)FL_MAX * sizeof(u64));
    if (!g_names || !g_types || !g_sizes) return -1;
    return 0;
}
static int      g_sel = 0;
static int      g_clip_copy = 0;      /* 1 = copy, 2 = cut, 0 = none */
static char     g_clip_path[FL_PATH_MAX];

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

static void fl_scan(void) {
    if (fl_buf_alloc() != 0) { g_count = 0; return; }
    g_count = 0;
    for (int i = 0; i < FL_MAX; i++) {
        fs_vfs_dirent_t de;
        if (fs_vfs_readdir(g_cwd, i, &de) != 0) break;
        char *slot = g_names + (usize)g_count * VFS_NAME_LEN;
        strncpy(slot, de.name, VFS_NAME_LEN - 1);
        slot[VFS_NAME_LEN - 1] = 0;
        g_types[g_count] = de.type;
        fs_vfs_stat_t st;
        g_sizes[g_count] = 0;
        /* stat through "<cwd>/<name>" for the size column */
        char path[FL_PATH_MAX];
        strncpy(path, g_cwd, sizeof(path) - 1);
        path[sizeof(path) - 1] = 0;
        int L = (int)strlen(path);
        if (L && path[L-1] != '/' && L + 1 < (int)sizeof(path)) path[L++] = '/';
        strncpy(path + L, de.name, sizeof(path) - (usize)L - 1);
        path[sizeof(path) - 1] = 0;
        if (fs_vfs_stat(path, &st) == 0) g_sizes[g_count] = st.size;
        g_count++;
    }
    if (g_sel >= g_count) g_sel = g_count ? g_count - 1 : 0;
    if (g_sel < 0) g_sel = 0;
}

static void fl_join(char *out, int cap, const char *name) {
    (void)cap;
    strncpy(out, g_cwd, (usize)cap - 1);
    out[cap - 1] = 0;
    int L = (int)strlen(out);
    if (L && out[L-1] != '/' && L + 1 < cap) out[L++] = '/';
    strncpy(out + L, name, (usize)(cap - L - 1));
    out[cap - 1] = 0;
}

static void fl_type_str(int t, char *out, int cap) {
    strncpy(out, t == VFS_TYPE_DIR ? tui_tr("<DIR>", "<目录>") :
                 t == VFS_TYPE_DEVICE ? tui_tr("<DEV>", "<设备>") :
                                        tui_tr("file", "文件"),
            (usize)(cap - 1));
    out[cap - 1] = 0;
}

/* read src fully, write it to dst (returns 0 ok) */
static int fl_copy_file(const char *src, const char *dst) {
    int sfd = fs_vfs_open(src, VFS_O_RDONLY);
    if (sfd < 0) return -1;
    int dfd = fs_vfs_open(dst, VFS_O_WRONLY | VFS_O_CREAT | VFS_O_TRUNC);
    if (dfd < 0) { fs_vfs_close(sfd); return -2; }
    char *buf = kmalloc(FL_CELLS * 1024);
    if (!buf) { fs_vfs_close(sfd); fs_vfs_close(dfd); return -3; }
    int rc = 0;
    for (;;) {
        int n = fs_vfs_read(sfd, buf, FL_CELLS * 1024);
        if (n < 0) { rc = -4; break; }
        if (n == 0) break;
        int w = fs_vfs_write(dfd, buf, n);
        if (w != n) { rc = -5; break; }
    }
    kfree(buf);
    fs_vfs_close(sfd);
    fs_vfs_close(dfd);
    return rc;
}

/* copy any entry (recursive for directories).  Depth-limited to keep
 * the kernel stack safe.  Returns >=0 = number of entries copied (0 for
 * a plain file), <0 error. */
static int fl_copy_depth = 0;
static int fl_copy_any(const char *src, const char *dst) {
    fs_vfs_stat_t st;
    if (fs_vfs_stat(src, &st) != 0) return -1;
    if (st.type != VFS_TYPE_DIR) return fl_copy_file(src, dst) == 0 ? 0 : -1;
    if (fl_copy_depth >= 8) return -2;      /* recursion guard        */
    int rc = fs_vfs_mkdir(dst);
    if (rc != 0) {
        fs_vfs_stat_t ds;
        if (fs_vfs_stat(dst, &ds) != 0) return rc;  /* real failure */
    }
    fl_copy_depth++;
    int copied = 0;
    for (int i = 0; i < FL_MAX; i++) {
        fs_vfs_dirent_t de;
        if (fs_vfs_readdir(src, i, &de) != 0) break;
        char sp[FL_PATH_MAX], dp[FL_PATH_MAX];
        strncpy(sp, src, sizeof(sp) - 1); sp[sizeof(sp) - 1] = 0;
        int L = (int)strlen(sp);
        if (L && sp[L-1] != '/' && L + 1 < (int)sizeof(sp)) sp[L++] = '/';
        strncpy(sp + L, de.name, sizeof(sp) - (usize)L - 1);
        sp[sizeof(sp) - 1] = 0;
        strncpy(dp, dst, sizeof(dp) - 1); dp[sizeof(dp) - 1] = 0;
        L = (int)strlen(dp);
        if (L && dp[L-1] != '/' && L + 1 < (int)sizeof(dp)) dp[L++] = '/';
        strncpy(dp + L, de.name, sizeof(dp) - (usize)L - 1);
        dp[sizeof(dp) - 1] = 0;
        int r = fl_copy_any(sp, dp);
        if (r < 0) { fl_copy_depth--; return r; }
        copied += (r > 0 ? r : 1);
    }
    fl_copy_depth--;
    return copied;
}

/* one-line form prompt: returns 0 confirmed (buf filled), -1 cancelled */
static int fl_prompt(const char *title, char *buf, int cap) {
    int rows = tui_dt_page_rows(), cols = tui_dt_page_cols();
    tui_window_t *fwin = tui_window_new(rows / 2 - 2, (cols - 56) / 2, 4, 56);
    if (!fwin) return -1;
    tui_window_focus_set(fwin);
    tui_window_title(fwin, title);
    tui_field_t f[1];
    memset(f, 0, sizeof(f));
    f[0].y = 2; f[0].x = 8; f[0].width = 44;
    f[0].editable = 1; f[0].label = tui_tr("name", "名称");
    tui_form_t *form = tui_form_new(fwin, f, 1);
    /* draw the prompt title by hand: tui_window_title() only stores
     * the string, it is not painted by tui_window_refresh */
    tui_set_attr(TUI_MAKE_ATTR(TUI_COLOR_WHITE, TUI_COLOR_TEAL) | TUI_ATTR_BOLD);
    tui_print_at(0, 1, title);
    tui_set_attr(tui_default_attr());
    tui_form_post(form);
    tui_window_refresh(fwin);
    int rc = -1;
    for (;;) {
        int k = tui_getch();
        if (k == TUI_KEY_ESC) break;
        int d = tui_form_driver(form, k);
        if (d == 1) {
            const char *v = tui_form_field_buffer(form, 0);
            strncpy(buf, v ? v : "", cap - 1);
            buf[cap - 1] = 0;
            rc = 0;
            break;
        }
        if (d == -1) break;
        tui_window_refresh(fwin);
    }
    tui_form_unpost(form);
    tui_window_free(fwin);
    /* CRITICAL: tui_form_post moved the input focus onto the form
     * window; once it is freed every tui_print would target the
     * released window.  Hand the focus back to the live page. */
    tui_window_t *pg = tui_dt_page_window();
    if (pg) tui_window_focus_set(pg);
    return rc;
}

/* ------------------------------------------------------------------ */
/* native text viewer (V / Enter on a file)                            */
/* ------------------------------------------------------------------ */

#define FL_VIEW_MAX (96 * 1024)

static void fl_view_page(int idx) {
    char path[FL_PATH_MAX];
    fl_join(path, sizeof(path), g_names + (usize)idx * VFS_NAME_LEN);
    fs_vfs_stat_t st;
    if (fs_vfs_stat(path, &st) != 0) {
        tui_dt_toast(0, tui_tr("stat %s failed", "stat %s 失败"),
                     g_names + (usize)idx * VFS_NAME_LEN);
        return;
    }
    if (st.type == VFS_TYPE_DIR) return;
    int fd = fs_vfs_open(path, VFS_O_RDONLY);
    if (fd < 0) {
        tui_dt_toast(0, tui_tr("open %s failed", "打开 %s 失败"), path);
        return;
    }
    char *buf = kmalloc(FL_VIEW_MAX + 1);
    if (!buf) { fs_vfs_close(fd); tui_dt_toast(0, tui_tr("out of memory", "内存不足")); return; }
    int total = 0;
    for (;;) {
        int n = fs_vfs_read(fd, buf + total, FL_VIEW_MAX - total);
        if (n <= 0) break;
        total += n;
        if (total >= FL_VIEW_MAX) break;
    }
    fs_vfs_close(fd);
    buf[total] = 0;

    if (tui_dt_page_open() != 0) { kfree(buf); return; }
    int rows = tui_dt_page_rows(), cols = tui_dt_page_cols();
    /* count lines (soft-wrap at cols-2 is NOT applied; long lines are
     * clipped to the viewport like the editor - honest and simple) */
    int nlines = 1;
    for (int i = 0; i < total; i++) if (buf[i] == '\n') nlines++;
    int top = 0;
    int partial = total >= FL_VIEW_MAX;
    for (;;) {
        tui_attr_t hdr = TUI_MAKE_ATTR(TUI_COLOR_WHITE, TUI_COLOR_TEAL) | TUI_ATTR_BOLD;
        tui_set_attr(hdr);
        for (int x = 0; x < cols; x++) tui_print_at(0, x, " ");
        {
            char t[112];
            strncpy(t, tui_tr("View: ", "查看："), sizeof(t) - 1);
            tui_dt_str_append(t, (int)sizeof(t), path);
            tui_dt_str_append(t, (int)sizeof(t), "  ");
            char num[16];
            u64_to_str((u64)total, num);
            tui_dt_str_append(t, (int)sizeof(t), num);
            tui_dt_str_append(t, (int)sizeof(t), tui_tr(" B", " 字节"));
            if (partial)
                tui_dt_str_append(t, (int)sizeof(t),
                                  tui_tr(" [truncated]", " [已截断]"));
            tui_print_at(0, 1, t);
        }
        tui_set_attr(tui_default_attr());
        for (int r = 1; r < rows - 2; r++) {
            tui_set_attr(tui_default_attr());
            for (int x = 0; x < cols; x++) tui_print_at(r, x, " ");
        }
        tui_set_attr(TUI_MAKE_ATTR(TUI_COLOR_SILVER, TUI_COLOR_BLACK));
        /* walk `top` newlines first */
        int pos = 0, line = 0;
        while (line < top && pos < total) {
            if (buf[pos] == '\n') line++;
            pos++;
        }
        int row = 1;
        while (row < rows - 2 && pos <= total) {
            if (pos >= total) break;
            int eol = pos;
            while (eol < total && buf[eol] != '\n') eol++;
            int len = eol - pos;
            if (len > cols - 3) len = cols - 3;
            char linebuf[256];
            if (len > 255) len = 255;
            memcpy(linebuf, buf + pos, (usize)len);
            linebuf[len] = 0;
            tui_print_at(row, 1, linebuf);
            row++;
            pos = eol + 1;
        }
        /* footer */
        tui_attr_t bar = TUI_MAKE_ATTR(TUI_COLOR_BLACK, TUI_COLOR_AQUA);
        tui_set_attr(bar);
        for (int x = 0; x < cols; x++) tui_print_at(rows - 1, x, " ");
        {
            char f[128];
            strncpy(f, tui_tr(" Up/Dn/PgUp/PgDn:Scroll  line ", " 上/下/PgUp/PgDn:滚动  行 "), sizeof(f) - 1);
            char num[16];
            u64_to_str((u64)(top + 1), num);
            tui_dt_str_append(f, (int)sizeof(f), num);
            tui_dt_str_append(f, (int)sizeof(f), "/");
            u64_to_str((u64)nlines, num);
            tui_dt_str_append(f, (int)sizeof(f), num);
            tui_dt_str_append(f, (int)sizeof(f), tui_tr("  Esc:Back ", "  Esc:返回 "));
            tui_print_at(rows - 1, 0, f);
        }
        tui_set_attr(tui_default_attr());
        tui_dt_page_flush();
        int k = tui_getch();
        int page = rows - 4; if (page < 1) page = 1;
        if (k == TUI_KEY_ESC || k == TUI_KEY_F10 || k == 'q') break;
        else if (k == TUI_KEY_UP) { if (top > 0) top--; }
        else if (k == TUI_KEY_DOWN) { if (top < nlines - 1) top++; }
        else if (k == TUI_KEY_PGUP) { top -= page; if (top < 0) top = 0; }
        else if (k == TUI_KEY_PGDN) { top += page; if (top > nlines - 1) top = nlines - 1; }
        else if (k == TUI_KEY_HOME) top = 0;
        else if (k == TUI_KEY_END) { top = nlines - 1; if (top < 0) top = 0; }
    }
    kfree(buf);
    /* the viewer page window dies here (same contract as
     * tui_dt_page_wait_close); the caller reopens the browser page */
    tui_dt_page_close();
}

/* ------------------------------------------------------------------ */
/* attribute page (a)                                                  */
/* ------------------------------------------------------------------ */

static void fl_attr_page(int idx) {
    char path[FL_PATH_MAX];
    fl_join(path, sizeof(path), g_names + (usize)idx * VFS_NAME_LEN);
    fs_vfs_stat_t st;
    if (fs_vfs_stat(path, &st) != 0) {
        tui_dt_toast(0, tui_tr("stat %s failed", "stat %s 失败"),
                     g_names + (usize)idx * VFS_NAME_LEN);
        return;
    }
    if (tui_dt_page_open() != 0) return;
    tui_print_at(0, 1, tui_tr("Attributes: ", "属性："));
    tui_print(st.name);
    char line[112], num[24];
    int row = 2;
    const char *ty = st.type == VFS_TYPE_DIR ? tui_tr("directory", "目录") :
                     st.type == VFS_TYPE_DEVICE ? tui_tr("device", "设备") :
                                                  tui_tr("file", "文件");
    strncpy(line, tui_tr("  type : ", "  类型："), sizeof(line) - 1);
    tui_dt_str_append(line, (int)sizeof(line), ty);
    tui_print_at(row++, 1, line);
    u64_to_str(st.size, num);
    strncpy(line, tui_tr("  size : ", "  大小："), sizeof(line) - 1);
    tui_dt_str_append(line, (int)sizeof(line), num);
    tui_dt_str_append(line, (int)sizeof(line), tui_tr(" bytes", " 字节"));
    tui_print_at(row++, 1, line);
    u64_to_str((u64)st.mode, num);
    strncpy(line, tui_tr("  mode : 0", "  模式：0"), sizeof(line) - 1);
    tui_dt_str_append(line, (int)sizeof(line), num);
    tui_print_at(row++, 1, line);
    u64_to_str((u64)st.uid, num);
    strncpy(line, tui_tr("  uid  : ", "  用户："), sizeof(line) - 1);
    tui_dt_str_append(line, (int)sizeof(line), num);
    tui_print_at(row++, 1, line);
    u64_to_str((u64)st.gid, num);
    strncpy(line, tui_tr("  gid  : ", "  组　："), sizeof(line) - 1);
    tui_dt_str_append(line, (int)sizeof(line), num);
    tui_print_at(row++, 1, line);
    u64_to_str((u64)st.nlink, num);
    strncpy(line, tui_tr("  nlink: ", "  链接："), sizeof(line) - 1);
    tui_dt_str_append(line, (int)sizeof(line), num);
    tui_print_at(row++, 1, line);
    tui_print_at(row++, 1, tui_tr("  full : ", "  路径："));
    tui_print(path);
    tui_dt_page_wait_close(NULL);
}

/* ------------------------------------------------------------------ */
/* mount manager (m): list mounts + block devices, umount/mount        */
/* ------------------------------------------------------------------ */

static const char *const FL_FS_TYPES[] = { "ramfs", "fat32", "exfat", "ext4" };
#define FL_FS_TYPES_N (int)(sizeof(FL_FS_TYPES) / sizeof(FL_FS_TYPES[0]))

static void fl_mount_page(void) {
    if (tui_dt_page_open() != 0) return;
    int rows = tui_dt_page_rows(), cols = tui_dt_page_cols();
    int msel = 0;
    fs_vfs_mount_info_t mi[8];
    for (;;) {
        int n = fs_vfs_get_mounts(mi, 8);
        tui_attr_t hdr = TUI_MAKE_ATTR(TUI_COLOR_WHITE, TUI_COLOR_TEAL) | TUI_ATTR_BOLD;
        tui_set_attr(hdr);
        for (int x = 0; x < cols; x++) tui_print_at(0, x, " ");
        tui_print_at(0, 1, tui_tr("Mount manager", "挂载管理"));
        tui_set_attr(tui_default_attr());
        int row = 2;
        tui_set_attr(TUI_MAKE_ATTR(TUI_COLOR_AQUA, TUI_COLOR_BLACK) | TUI_ATTR_BOLD);
        tui_print_at(row++, 1, tui_tr(
            "  MOUNT POINT         TYPE       DEVICE",
            "  挂载点              类型       设备"));
        tui_set_attr(tui_default_attr());
        for (int i = 0; i < n && row < rows - 8; i++) {
            char line[128];
            int m = 0;
            for (const char *p = mi[i].mount_point; *p && m < 30; p++) line[m++] = *p;
            while (m < 22) line[m++] = ' ';
            for (const char *p = mi[i].fs_type; *p && m < 34; p++) line[m++] = *p;
            while (m < 32) line[m++] = ' ';
            for (const char *p = mi[i].device; *p && m < 60; p++) line[m++] = *p;
            line[m] = 0;
            tui_set_attr(i == msel
                ? (tui_attr_t)(TUI_MAKE_ATTR(TUI_COLOR_BLACK, TUI_COLOR_SILVER) | TUI_ATTR_BOLD)
                : TUI_MAKE_ATTR(TUI_COLOR_SILVER, TUI_COLOR_BLACK));
            tui_print_at(row++, 1, line);
        }
        /* block devices (real driver_block registry) */
        row++;
        tui_set_attr(TUI_MAKE_ATTR(TUI_COLOR_AQUA, TUI_COLOR_BLACK) | TUI_ATTR_BOLD);
        tui_print_at(row++, 1, tui_tr(
            "  BLOCK DEVICE        SIZE (MiB)",
            "  块设备              容量 (MiB)"));
        tui_set_attr(TUI_MAKE_ATTR(TUI_COLOR_SILVER, TUI_COLOR_BLACK));
        int ndev = 0;
        for (int d = 0; d < 16; d++) {
            driver_block_device_t *dev = driver_block_get_device(d);
            if (!dev) break;
            ndev++;
            char line[96], num[24];
            int m = 0;
            for (const char *p = dev->name; *p && m < 22; p++) line[m++] = *p;
            while (m < 22) line[m++] = ' ';
            u64_to_str(dev->sectors >> 11, num);
            for (const char *p = num; *p && m < 40; p++) line[m++] = *p;
            line[m] = 0;
            if (row < rows - 4) tui_print_at(row++, 1, line);
        }
        if (ndev == 0 && row < rows - 4)
            tui_print_at(row++, 1, tui_tr("  (no block devices)",
                                          "  （无块设备）"));
        /* key bar */
        tui_attr_t bar = TUI_MAKE_ATTR(TUI_COLOR_BLACK, TUI_COLOR_AQUA);
        tui_set_attr(bar);
        for (int x = 0; x < cols; x++) tui_print_at(rows - 1, x, " ");
        tui_print_at(rows - 1, 0, tui_tr(
            " Up/Dn:Select u:Umount n:Mount Esc:Back ",
            " 上/下:选择 u:卸载 n:挂载 Esc:返回 "));
        tui_set_attr(tui_default_attr());
        tui_dt_page_flush();
        int k = tui_getch();
        if (k == TUI_KEY_ESC || k == TUI_KEY_F10 || k == 'q') break;
        else if (k == TUI_KEY_UP) { if (msel > 0) msel--; }
        else if (k == TUI_KEY_DOWN) { if (msel < n - 1) msel++; }
        else if (k == 'u') {
            if (n > 0 && msel < n) {
                char msg[180];
                strncpy(msg, tui_tr("umount ", "卸载 "), sizeof(msg) - 1);
                tui_dt_str_append(msg, (int)sizeof(msg), mi[msel].mount_point);
                tui_dt_str_append(msg, (int)sizeof(msg), tui_tr(" ?", " ？"));
                if (tui_dt_confirm(msg, msg) == 1) {
                    int rc = fs_vfs_umount(mi[msel].mount_point);
                    tui_dt_toast(rc == 0,
                        tui_tr("umount %s rc=%d", "卸载 %s rc=%d"),
                        mi[msel].mount_point, rc);
                    if (rc == 0 && msel > 0) msel--;
                }
            } else {
                tui_dt_toast(0, tui_tr("no mount selected", "未选择挂载点"));
            }
        }
        else if (k == 'n') {
            char dev[64], mnt[128];
            if (fl_prompt(tui_tr("device to mount (e.g. ata0)",
                                 "要挂载的设备（如 ata0）"),
                          dev, (int)sizeof(dev)) != 0 || !dev[0])
                continue;
            char fstype[24];
            if (fl_prompt(tui_tr("fs type (ramfs/fat32/exfat/ext4)",
                                 "文件系统类型（ramfs/fat32/exfat/ext4）"),
                          fstype, (int)sizeof(fstype)) != 0 || !fstype[0])
                continue;
            /* validate against the known fs_type registry names */
            int known = 0;
            for (int t = 0; t < FL_FS_TYPES_N; t++)
                if (strcmp(fstype, FL_FS_TYPES[t]) == 0) { known = 1; break; }
            if (!known) {
                tui_dt_toast(0,
                    tui_tr("unknown fs type: %s (ramfs/fat32/exfat/ext4)",
                           "未知文件系统类型：%s（ramfs/fat32/exfat/ext4）"),
                    fstype);
                continue;
            }
            if (fl_prompt(tui_tr("mount point (e.g. /mnt)",
                                 "挂载点（如 /mnt）"),
                          mnt, (int)sizeof(mnt)) != 0 || !mnt[0])
                continue;
            int rc = fs_vfs_mount(fstype, mnt, dev);
            tui_dt_toast(rc == 0,
                tui_tr("mount %s %s %s rc=%d", "挂载 %s %s %s rc=%d"),
                fstype, dev, mnt, rc);
        }
    }
}

/* ------------------------------------------------------------------ */
/* render + main loop                                                  */
/* ------------------------------------------------------------------ */

static void fl_render(void) {
    int rows = tui_dt_page_rows(), cols = tui_dt_page_cols();
    /* header: current path + clip state */
    tui_attr_t hdr = TUI_MAKE_ATTR(TUI_COLOR_WHITE, TUI_COLOR_TEAL) | TUI_ATTR_BOLD;
    tui_set_attr(hdr);
    for (int x = 0; x < cols; x++) tui_print_at(0, x, " ");
    tui_print_at(0, 1, g_cwd);
    if (g_clip_copy) {
        tui_set_attr(TUI_MAKE_ATTR(TUI_COLOR_YELLOW, TUI_COLOR_TEAL));
        tui_print_at(0, cols - 20,
                     g_clip_copy == 1 ? tui_tr(" [copy] ", " [复制] ")
                                      : tui_tr(" [cut]  ", " [剪切] "));
        tui_print(g_clip_path);
    }
    tui_set_attr(tui_default_attr());

    int first_row = 1;
    int max_items = rows - first_row - 2;
    if (max_items < 1) max_items = 1;
    int top = g_sel - (max_items - 1);
    if (top < 0) top = 0;
    for (int r = first_row; r < first_row + max_items && r < rows - 1; r++) {
        tui_set_attr(tui_default_attr());
        for (int x = 0; x < cols; x++) tui_print_at(r, x, " ");
    }
    for (int i = 0; i < g_count; i++) {
        int row = first_row + i - top;
        if (row < first_row || row >= first_row + max_items) continue;
        tui_set_attr(i == g_sel
            ? (TUI_MAKE_ATTR(TUI_COLOR_BLACK, TUI_COLOR_SILVER) | TUI_ATTR_BOLD)
            : TUI_MAKE_ATTR(TUI_COLOR_SILVER, TUI_COLOR_BLACK));
        char line[128], num[24];
        int m = 0;
        const char *nm = g_names + (usize)i * VFS_NAME_LEN;
        for (const char *p = nm; *p && m < 40; p++) line[m++] = *p;
        while (m < 28) line[m++] = ' ';
        char ty[16];
        fl_type_str(g_types[i], ty, (int)sizeof(ty));
        for (const char *p = ty; *p && m < 44; p++) line[m++] = *p;
        while (m < 44) line[m++] = ' ';
        u64_to_str(g_sizes[i], num);
        for (const char *p = num; *p && m < 60; p++) line[m++] = *p;
        line[m] = 0;
        tui_print_at(row, 1, line);
    }
    /* result toast (row rows-2) - visible INSIDE the files page */
    tui_set_attr(tui_default_attr());
    for (int x = 0; x < cols; x++) tui_print_at(rows - 2, x, " ");
    int tok = 1;
    const char *txt = tui_dt_toast_text(&tok);
    if (txt[0]) {
        tui_set_attr(tok
            ? (tui_attr_t)(TUI_MAKE_ATTR(TUI_COLOR_LIME, TUI_COLOR_BLACK) | TUI_ATTR_BOLD)
            : (tui_attr_t)(TUI_MAKE_ATTR(TUI_COLOR_WHITE, TUI_COLOR_MAROON) | TUI_ATTR_REVERSE));
        tui_print_at(rows - 2, 1, txt);
    }
    /* resident key bar (row rows-1) */
    tui_attr_t bar = TUI_MAKE_ATTR(TUI_COLOR_BLACK, TUI_COLOR_AQUA);
    tui_set_attr(bar);
    for (int x = 0; x < cols; x++) tui_print_at(rows - 1, x, " ");
    tui_set_attr(TUI_MAKE_ATTR(TUI_COLOR_BLACK, TUI_COLOR_AQUA));
    tui_print_at(rows - 1, 0, tui_tr(
        " Enter:Open/View Back:Up c:Copy x:Cut p:Paste r:Ren d:Del n:MkDir f:New a:Attr v:View m:Mnt F5:Ref Esc:Exit ",
        " Enter:打开/查看 Back:上级 c:复制 x:剪切 p:粘贴 r:重命名 d:删除 n:建目录 f:新建 a:属性 v:查看 m:挂载 F5:刷新 Esc:退出 "));
    tui_set_attr(tui_default_attr());
    tui_dt_page_flush();
}

int tui_dt_files_run(tui_dt_node_t *self) {
    (void)self;
    if (tui_dt_page_open() != 0) return -1;
    g_clip_copy = 0;
    fl_scan();
    char buf[FL_PATH_MAX];
    for (;;) {
        fl_render();
        int k = tui_getch();
        if (k == TUI_KEY_UP)    { if (g_sel > 0) g_sel--; }
        else if (k == TUI_KEY_DOWN) { if (g_sel < g_count - 1) g_sel++; }
        else if (k == TUI_KEY_F5) {
            fl_scan();
            tui_dt_toast(1, tui_tr("refreshed", "已刷新"));
        }
        else if (k == TUI_KEY_ESC || k == TUI_KEY_F10 || k == 'q') break;
        else if (k == TUI_KEY_BACKSPACE) {
            if (strcmp(g_cwd, "/") != 0) {
                char *slash = fl_strrchr(g_cwd, '/');
                if (slash && slash != g_cwd) *slash = 0;
                else strcpy(g_cwd, "/");
                g_sel = 0;
                fl_scan();
            }
        }
        else if (k == TUI_KEY_ENTER && g_count > 0) {
            if (g_types[g_sel] == VFS_TYPE_DIR) {
                char path[FL_PATH_MAX];
                fl_join(path, sizeof(path), g_names + (usize)g_sel * VFS_NAME_LEN);
                strncpy(g_cwd, path, sizeof(g_cwd) - 1);
                g_cwd[sizeof(g_cwd) - 1] = 0;
                g_sel = 0;
                fl_scan();
            } else {
                fl_view_page(g_sel);        /* native viewer         */
                tui_dt_page_open();         /* reopen the browser    */
            }
        }
        else if (k == 'v' && g_count > 0 && g_types[g_sel] != VFS_TYPE_DIR) {
            fl_view_page(g_sel);
            tui_dt_page_open();
        }
        else if (k == 'a' && g_count > 0) {
            fl_attr_page(g_sel);        /* sub-page frees its window */
            tui_dt_page_open();         /* reopen the browser page   */
        }
        else if (k == 'm') {
            fl_mount_page();
            tui_dt_page_open();
            fl_scan();
        }
        else if (k == 'n') {
            if (fl_prompt(tui_tr("new directory name", "新目录名"),
                          buf, (int)sizeof(buf)) == 0) {
                char path[FL_PATH_MAX];
                fl_join(path, sizeof(path), buf);
                int rc = fs_vfs_mkdir(path);
                tui_dt_toast(rc == 0, tui_tr("mkdir %s rc=%d", "新建目录 %s rc=%d"),
                             buf, rc);
                fl_scan();
            }
        }
        else if (k == 'f') {
            if (fl_prompt(tui_tr("new file name", "新文件名"),
                          buf, (int)sizeof(buf)) == 0) {
                char path[FL_PATH_MAX];
                fl_join(path, sizeof(path), buf);
                int fd = fs_vfs_open(path, VFS_O_WRONLY | VFS_O_CREAT);
                int rc = fd >= 0 ? (fs_vfs_close(fd), 0) : -1;
                tui_dt_toast(rc == 0, tui_tr("create %s rc=%d", "新建 %s rc=%d"),
                             buf, rc);
                fl_scan();
            }
        }
        else if (k == 'c' && g_count > 0) {
            fl_join(g_clip_path, sizeof(g_clip_path), g_names + (usize)g_sel * VFS_NAME_LEN);
            g_clip_copy = 1;
            tui_dt_toast(1, tui_tr("marked for copy: %s", "已标记复制：%s"),
                         g_names + (usize)g_sel * VFS_NAME_LEN);
        }
        else if (k == 'x' && g_count > 0) {
            fl_join(g_clip_path, sizeof(g_clip_path), g_names + (usize)g_sel * VFS_NAME_LEN);
            g_clip_copy = 2;
            tui_dt_toast(1, tui_tr("marked for move: %s", "已标记移动：%s"),
                         g_names + (usize)g_sel * VFS_NAME_LEN);
        }
        else if (k == 'p' && g_clip_copy) {
            /* destination = cwd + basename of clip */
            char *slash = fl_strrchr(g_clip_path, '/');
            const char *base = slash ? slash + 1 : g_clip_path;
            char dst[FL_PATH_MAX];
            fl_join(dst, sizeof(dst), base);
            int rc;
            if (g_clip_copy == 1) {
                fl_copy_depth = 0;
                rc = fl_copy_any(g_clip_path, dst);
                tui_dt_toast(rc >= 0, tui_tr("copy rc=%d", "复制 rc=%d"), rc);
            } else {
                rc = fs_vfs_rename(g_clip_path, dst);
                tui_dt_toast(rc == 0, tui_tr("move rc=%d", "移动 rc=%d"), rc);
                if (rc == 0) g_clip_copy = 0;
            }
            fl_scan();
        }
        else if (k == 'r' && g_count > 0) {
            if (fl_prompt(tui_tr("rename to", "重命名为"),
                          buf, (int)sizeof(buf)) == 0) {
                char srcp[FL_PATH_MAX], dstp[FL_PATH_MAX];
                fl_join(srcp, sizeof(srcp), g_names + (usize)g_sel * VFS_NAME_LEN);
                fl_join(dstp, sizeof(dstp), buf);
                int rc = fs_vfs_rename(srcp, dstp);
                tui_dt_toast(rc == 0, tui_tr("rename rc=%d", "重命名 rc=%d"), rc);
                fl_scan();
            }
        }
        else if (k == 'd' && g_count > 0) {
            char path[FL_PATH_MAX];
            fl_join(path, sizeof(path), g_names + (usize)g_sel * VFS_NAME_LEN);
            /* WP-10-tui-native: destructive actions confirm first */
            char msg[180];
            strncpy(msg, tui_tr("delete ", "删除 "), sizeof(msg) - 1);
            tui_dt_str_append(msg, (int)sizeof(msg),
                              g_names + (usize)g_sel * VFS_NAME_LEN);
            tui_dt_str_append(msg, (int)sizeof(msg), tui_tr(" ?", " ？"));
            if (tui_dt_confirm(msg, msg) != 1) {
                /* WP-10-tui-native fix: cancelling the delete stays in
                 * the browser (break left the whole application - the
                 * task-manager 'k' path uses continue for the same
                 * interaction; parity restored here) */
                tui_dt_toast(1, tui_tr("cancelled", "已取消"));
                continue;
            }
            int rc = (g_types[g_sel] == VFS_TYPE_DIR)
                        ? fs_vfs_rmdir(path) : fs_vfs_unlink(path);
            tui_dt_toast(rc == 0, tui_tr("delete %s rc=%d", "删除 %s rc=%d"),
                         g_names + (usize)g_sel * VFS_NAME_LEN, rc);
            fl_scan();
        }
    }
    return 0;
}
