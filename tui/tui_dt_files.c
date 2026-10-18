/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-10-tui-unified
 * File: tui/tui_dt_files.c
 * Purpose: the desktop file manager - a real VFS browser over
 *          fs_vfs_readdir/stat/mkdir/unlink/rename and fd-based file
 *          copy.  Purely keyboard driven; every action reports OK/FAIL
 *          through the desktop toast.
 */
#include "tui_desktop.h"
#include "../fs/fs_vfs.h"
#include "../kernel/lib/lib_string.h"
#include "../kernel/mem/mem_heap.h"
#include "../drivers/input/driver_input_keyboard.h"

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
    strncpy(out, t == VFS_TYPE_DIR ? "<DIR>" :
                 t == VFS_TYPE_DEVICE ? "<DEV>" : "file", cap - 1);
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
    f[0].editable = 1; f[0].label = "name";
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
/* attribute page (a) and mount page (m)                               */
/* ------------------------------------------------------------------ */

static void fl_attr_page(int idx) {
    char path[FL_PATH_MAX];
    fl_join(path, sizeof(path), g_names + (usize)idx * VFS_NAME_LEN);
    fs_vfs_stat_t st;
    if (fs_vfs_stat(path, &st) != 0) {
        tui_dt_toast(0, "stat %s failed", g_names[idx]);
        return;
    }
    if (tui_dt_page_open() != 0) return;
    tui_print_at(0, 1, "Attributes: ");
    tui_print(st.name);
    char line[112], num[24];
    int row = 2;
    const char *ty = st.type == VFS_TYPE_DIR ? "directory" :
                     st.type == VFS_TYPE_DEVICE ? "device" : "file";
    strncpy(line, "  type : ", sizeof(line) - 1);
    tui_dt_str_append(line, sizeof(line), ty);
    tui_print_at(row++, 1, line);
    u64_to_str(st.size, num);
    strncpy(line, "  size : ", sizeof(line) - 1);
    tui_dt_str_append(line, sizeof(line), num);
    tui_dt_str_append(line, sizeof(line), " bytes");
    tui_print_at(row++, 1, line);
    u64_to_str((u64)st.mode, num);
    strncpy(line, "  mode : 0", sizeof(line) - 1);
    tui_dt_str_append(line, sizeof(line), num);
    tui_print_at(row++, 1, line);
    u64_to_str((u64)st.uid, num);
    strncpy(line, "  uid  : ", sizeof(line) - 1);
    tui_dt_str_append(line, sizeof(line), num);
    tui_print_at(row++, 1, line);
    u64_to_str((u64)st.gid, num);
    strncpy(line, "  gid  : ", sizeof(line) - 1);
    tui_dt_str_append(line, sizeof(line), num);
    tui_print_at(row++, 1, line);
    u64_to_str((u64)st.nlink, num);
    strncpy(line, "  nlink: ", sizeof(line) - 1);
    tui_dt_str_append(line, sizeof(line), num);
    tui_print_at(row++, 1, line);
    tui_dt_page_wait_close(NULL);
}

static void fl_mount_page(void) {
    if (tui_dt_page_open() != 0) return;
    tui_print_at(0, 1, tui_tr("Mounted file systems", "已挂载文件系统"));
    fs_vfs_mount_info_t mi[8];
    int n = fs_vfs_get_mounts(mi, 8);
    int row = 2;
    if (n == 0)
        tui_print_at(row++, 1, tui_tr("  (no mounts)", "  （无挂载）"));
    for (int i = 0; i < n && row < tui_dt_page_rows() - 2; i++) {
        char line[128];
        int m = 0;
        for (const char *p = mi[i].mount_point; *p && m < 30; p++) line[m++] = *p;
        while (m < 16) line[m++] = ' ';
        line[m++] = ' ';
        for (const char *p = mi[i].fs_type; *p && m < 30; p++) line[m++] = *p;
        while (m < 24) line[m++] = ' ';
        line[m++] = ' ';
        for (const char *p = mi[i].device; *p && m < 60; p++) line[m++] = *p;
        line[m] = 0;
        tui_print_at(row++, 1, line);
    }
    tui_dt_page_wait_close(NULL);
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
        tui_print_at(0, cols - 20, g_clip_copy == 1 ? " [copy] " : " [cut]  ");
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
        char ty[12];
        fl_type_str(g_types[i], ty, (int)sizeof(ty));
        for (const char *p = ty; *p && m < 40; p++) line[m++] = *p;
        while (m < 36) line[m++] = ' ';
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
        " Enter:Open Back:Up c:Copy x:Cut p:Paste r:Ren d:Del n:MkDir f:New a:Attr m:Mnt F5:Ref Esc:Exit ",
        " Enter:打开 Back:上级 c:复制 x:剪切 p:粘贴 r:重命名 d:删除 n:建目录 f:新建 a:属性 m:挂载 F5:刷新 Esc:退出 "));
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
        else if (k == TUI_KEY_F5)   { fl_scan(); tui_dt_toast(1, "refreshed"); }
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
                fl_attr_page(g_sel);
            }
        }
        else if (k == 'a' && g_count > 0) {
            fl_attr_page(g_sel);        /* sub-page frees its window */
            tui_dt_page_open();         /* reopen the browser page   */
        }
        else if (k == 'm') {
            fl_mount_page();
            tui_dt_page_open();
        }
        else if (k == 'n') {
            if (fl_prompt(tui_tr("new directory name", "新目录名"),
                          buf, (int)sizeof(buf)) == 0) {
                char path[FL_PATH_MAX];
                fl_join(path, sizeof(path), buf);
                int rc = fs_vfs_mkdir(path);
                tui_dt_toast(rc == 0, "mkdir %s rc=%d", buf, rc);
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
                tui_dt_toast(rc == 0, "create %s rc=%d", buf, rc);
                fl_scan();
            }
        }
        else if (k == 'c' && g_count > 0) {
            fl_join(g_clip_path, sizeof(g_clip_path), g_names + (usize)g_sel * VFS_NAME_LEN);
            g_clip_copy = 1;
            tui_dt_toast(1, "marked for copy: %s", g_names[g_sel]);
        }
        else if (k == 'x' && g_count > 0) {
            fl_join(g_clip_path, sizeof(g_clip_path), g_names + (usize)g_sel * VFS_NAME_LEN);
            g_clip_copy = 2;
            tui_dt_toast(1, "marked for move: %s", g_names[g_sel]);
        }
        else if (k == 'p' && g_clip_copy) {
            /* destination = cwd + basename of clip */
            char *slash = fl_strrchr(g_clip_path, '/');
            const char *base = slash ? slash + 1 : g_clip_path;
            char dst[FL_PATH_MAX];
            fl_join(dst, sizeof(dst), base);
            int rc;
            if (g_clip_copy == 1) {
                rc = fl_copy_file(g_clip_path, dst);
                tui_dt_toast(rc == 0, "copy rc=%d", rc);
            } else {
                rc = fs_vfs_rename(g_clip_path, dst);
                tui_dt_toast(rc == 0, "move rc=%d", rc);
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
                tui_dt_toast(rc == 0, "rename rc=%d", rc);
                fl_scan();
            }
        }
        else if (k == 'd' && g_count > 0) {
            char path[FL_PATH_MAX];
            fl_join(path, sizeof(path), g_names + (usize)g_sel * VFS_NAME_LEN);
            int rc = (g_types[g_sel] == VFS_TYPE_DIR)
                        ? fs_vfs_rmdir(path) : fs_vfs_unlink(path);
            tui_dt_toast(rc == 0, "delete %s rc=%d", g_names + (usize)g_sel * VFS_NAME_LEN, rc);
            fl_scan();
        }
    }
    return 0;
}
