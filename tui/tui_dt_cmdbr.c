/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-10-tui-unified
 * File: tui/tui_dt_cmdbr.c
 * Purpose: the Command Bridge (live shell parity inside the TUI) and
 *          the dynamic per-disk subtree (the 6-level-depth proof).
 *          Everything renders real data from the real subsystems.
 */
#include "tui_desktop.h"
#include "../kernel/lib/lib_string.h"
#include "../kernel/mem/mem_heap.h"
#include "../shell/shell.h"
#include "../drivers/block/driver_block_blk.h"

/* bounded append (the kernel string lib has no strncat) */
static void dt_append(char *dst, usize cap, const char *src) {
    usize len = strlen(dst);
    if (len + 1 >= cap) return;
    usize k = 0;
    while (src[k] && len + 1 < cap) dst[len++] = src[k++];
    dst[len] = 0;
}

/* ------------------------------------------------------------------ */
/* full-screen page helpers                                            */
/* ------------------------------------------------------------------ */

static tui_window_t *g_page_win = NULL;

int tui_dt_page_open(void) {
    int c, r;
    tui_term_size(&c, &r);
    tui_window_t *win = tui_window_new(0, 0, r, c);
    if (!win) return -1;
    tui_window_focus_set(win);
    tui_window_title(win, "Open Cube OS Desktop");
    tui_window_clear(win);
    tui_window_refresh(win);
    /* NOTE: this framework paints windows through explicit
     * tui_window_refresh(); tui_update_panels() first black-fills the
     * screen and repaints only panel-wrapped windows, which is why a
     * plain page window must never rely on it. */
    g_page_win = win;
    return 0;
}

int tui_dt_page_rows(void) {
    int c, r;
    tui_term_size(&c, &r);
    return r;
}

int tui_dt_page_cols(void) {
    int c, r;
    tui_term_size(&c, &r);
    return c;
}

void tui_dt_page_flush(void) {
    if (g_page_win) tui_window_refresh(g_page_win);
}

void tui_dt_page_wait_close(const char *hint) {
    tui_dt_page_flush();   /* the page body must actually hit the screen */
    tui_set_attr(TUI_MAKE_ATTR(TUI_COLOR_YELLOW, TUI_COLOR_BLACK));
    tui_print_at(tui_dt_page_rows() - 1, 1,
                 hint ? hint : tui_tr("Esc/F10 back", "Esc/F10 返回"));
    tui_set_attr(tui_default_attr());
    tui_dt_page_flush();
    for (;;) {
        int k = tui_getch();
        if (k == TUI_KEY_ESC || k == TUI_KEY_F10 || k == 'q' ||
            k == TUI_KEY_BACKSPACE)
            break;
    }
    /* the page window dies with the page (no panel-stack leakage) */
    if (g_page_win) { tui_window_free(g_page_win); g_page_win = NULL; }
    tui_update_panels();   /* black-fill + repaint panel windows       */
}

/* ------------------------------------------------------------------ */
/* command bridge: every shell command, runnable, live output          */
/* ------------------------------------------------------------------ */

#define DT_CMD_MAX   240
#define DT_OUT_MAX   16384

/* the command table and the output buffer live on the kernel heap
 * (linker identity window is precious - large buffers never in .bss);
 * they are allocated on page entry and freed on page exit */
static shell_cmd_info_t *dt_cmds = NULL;
static int              dt_cmd_cap = 0;
static int              dt_cmd_count = 0;
static char            *dt_out = NULL;

static void dt_cmd_sort(void) {
    for (int i = 1; i < dt_cmd_count; i++) {
        shell_cmd_info_t key = dt_cmds[i];
        int j = i - 1;
        while (j >= 0 && strcasecmp(dt_cmds[j].name, key.name) > 0) {
            dt_cmds[j + 1] = dt_cmds[j];
            j--;
        }
        dt_cmds[j + 1] = key;
    }
}

static void dt_cmd_reload(void) {
    if (!dt_cmds) { dt_cmd_count = 0; return; }
    dt_cmd_count = shell_cmd_enum(dt_cmds, dt_cmd_cap ? dt_cmd_cap : DT_CMD_MAX);
    dt_cmd_sort();
}

static int dt_cmd_alloc(void) {
    if (dt_cmds && dt_out) return 0;   /* already allocated */
    if (!dt_cmds) {
        dt_cmds = kmalloc(sizeof(shell_cmd_info_t) * DT_CMD_MAX);
        if (!dt_cmds) return -1;
    }
    if (!dt_out) {
        dt_out = kmalloc(DT_OUT_MAX);
        if (!dt_out) { kfree(dt_cmds); dt_cmds = NULL; return -1; }
    }
    dt_cmd_cap = DT_CMD_MAX;
    return 0;
}

static void dt_cmd_free(void) {
    if (dt_cmds) { kfree(dt_cmds); dt_cmds = NULL; }
    if (dt_out)  { kfree(dt_out);  dt_out = NULL;  }
    dt_cmd_cap = 0;
    dt_cmd_count = 0;
}

/* draw the captured-output area (bottom half), newest lines visible */
static void dt_draw_output(int list_rows, int rows, int cols) {
    int out_row = list_rows + 2;
    tui_set_attr(TUI_MAKE_ATTR(TUI_COLOR_AQUA, TUI_COLOR_BLACK) | TUI_ATTR_BOLD);
    tui_print_at(out_row, 1, tui_tr("-- output --", "-- 输出 --"));
    tui_set_attr(tui_default_attr());
    int avail = rows - out_row - 2;
    if (avail < 0) avail = 0;
    /* count lines */
    int total = 0;
    for (char *p = dt_out; *p; ) {
        char *nl = strchr(p, '\n');
        total++;
        if (!nl) break;
        p = nl + 1;
    }
    int show_from = total - avail;
    if (show_from < 0) show_from = 0;
    int r = out_row + 1;
    int idx = 0;
    for (char *p = dt_out; *p && idx <= show_from + avail; ) {
        char *nl = strchr(p, '\n');
        int len = nl ? (int)(nl - p) : (int)strlen(p);
        if (idx >= show_from && r < rows - 1 && len > 0) {
            int m = len;
            if (m > cols - 3) m = cols - 3;
            if (m > 120) m = 120;
            char line[128];
            memcpy(line, p, m);
            line[m] = 0;
            tui_print_at(r++, 1, line);
        }
        idx++;
        if (!nl) break;
        p = nl + 1;
    }
}

int tui_dt_cmdbridge_run(tui_dt_node_t *self) {
    (void)self;
    if (dt_cmd_alloc() != 0) { tui_dt_toast(0, "out of memory"); return -1; }
    if (tui_dt_page_open() != 0) { dt_cmd_free(); return -1; }
    dt_cmd_reload();
    int sel = 0;
    int rows = tui_dt_page_rows();
    int cols = tui_dt_page_cols();
    int list_rows = rows - 6;
    if (list_rows < 1) list_rows = 1;

    for (;;) {
        tui_set_attr(tui_default_attr());
        char hdr[120];
        int n = 0;
        const char *t1 = tui_tr("Command Bridge - ", "命令桥 - ");
        for (const char *p = t1; *p && n < 40; p++) hdr[n++] = *p;
        char num[12];
        u64_to_str((u64)dt_cmd_count, num);
        for (int i = 0; num[i] && n < 40; i++) hdr[n++] = num[i];
        const char *t2 = tui_tr(" commands (Enter run, F5 reload, Esc back)",
                                " 个命令（Enter 运行，F5 重载，Esc 返回）");
        for (const char *p = t2; *p && n < 100; p++) hdr[n++] = *p;
        hdr[n] = 0;
        tui_print_at(0, 1, hdr);

        int top = sel - (list_rows - 1);
        if (top < 0) top = 0;
        for (int i = 0; i < dt_cmd_count; i++) {
            int row = 1 + i - top;
            if (row < 1 || row > list_rows) continue;
            tui_set_attr(i == sel
                ? (TUI_MAKE_ATTR(TUI_COLOR_BLACK, TUI_COLOR_SILVER) | TUI_ATTR_BOLD)
                : TUI_MAKE_ATTR(TUI_COLOR_SILVER, TUI_COLOR_BLACK));
            char line[128];
            int m = 0;
            for (const char *p = dt_cmds[i].name; *p && m < 26; p++) line[m++] = *p;
            while (m < 18) line[m++] = ' ';
            line[m++] = '-'; line[m++] = ' ';
            for (const char *p = dt_cmds[i].help; *p && m < 120; p++) line[m++] = *p;
            line[m] = 0;
            tui_print_at(row, 1, line);
        }

        dt_draw_output(list_rows, rows, cols);
        tui_dt_page_flush();

        int k = tui_getch();
        if (k == TUI_KEY_UP)         { if (sel > 0) sel--; }
        else if (k == TUI_KEY_DOWN)  { if (sel < dt_cmd_count - 1) sel++; }
        else if (k == TUI_KEY_PGUP)  { sel -= list_rows; if (sel < 0) sel = 0; }
        else if (k == TUI_KEY_PGDN)  { sel += list_rows;
                                       if (sel >= dt_cmd_count) sel = dt_cmd_count - 1; }
        else if (k == TUI_KEY_F5)    { dt_cmd_reload(); }
        else if (k == TUI_KEY_ESC || k == TUI_KEY_F10) { dt_cmd_free(); return 0; }
        else if (k == TUI_KEY_ENTER && dt_cmd_count > 0) {
            tui_window_t *fwin = tui_window_new(rows / 2 - 2, (cols - 56) / 2, 4, 56);
            if (!fwin) continue;
            tui_window_title(fwin, tui_tr("arguments (empty = none)",
                                          "参数（空 = 无）"));
            tui_field_t f[1];
            memset(f, 0, sizeof(f));
            f[0].y = 1; f[0].x = 1;
            f[0].width = 50;
            f[0].editable = 1;
            f[0].label = "args";
            tui_form_t *form = tui_form_new(fwin, f, 1);
            tui_form_post(form);
            tui_window_refresh(fwin);
            int cancelled = 0;
            for (;;) {
                int k2 = tui_getch();
                if (k2 == TUI_KEY_ESC) { cancelled = 1; break; }
                int d = tui_form_driver(form, k2);
                if (d == 1) break;
                if (d == -1) { cancelled = 1; break; }
            }
            const char *args = tui_form_field_buffer(form, 0);
            char line[256];
            int m = 0;
            for (const char *p = dt_cmds[sel].name; *p && m < 250; p++) line[m++] = *p;
            if (args && args[0]) {
                line[m++] = ' ';
                for (const char *p = args; *p && m < 250; p++) line[m++] = *p;
            }
            line[m] = 0;
            tui_form_unpost(form);
            tui_window_free(fwin);
            tui_dt_page_flush();   /* back to the bridge page */
            if (cancelled) continue;

            tui_dt_loading_begin(line);
            dt_out[0] = 0;
            int rc = shell_execute_captured(line, dt_out, DT_OUT_MAX);
            tui_dt_loading_end(rc == 0, "command finished", "command failed");
            tui_dt_toast(rc == 0, "%s rc=%d", dt_cmds[sel].name, rc);
        }
    }
}

/* ------------------------------------------------------------------ */
/* dynamic disk subtree (levels 4-6 of the menu tree, real devices)    */
/* ------------------------------------------------------------------ */

/* per-device persisted command strings for SHELL nodes (the registry
 * keeps the pointer, so the storage must outlive the entries) */
static char g_disk_rw_cmd[8][48];

/* level-6 page: real device detail from the block layer */
int tui_dt_disk_detail_run(tui_dt_node_t *self) {
    driver_block_device_t *d =
        self ? (driver_block_device_t *)self->userptr : NULL;
    if (!d) return -1;
    if (tui_dt_page_open() != 0) return -1;
    tui_print_at(0, 1, tui_tr("Disk detail: ", "磁盘详情："));
    tui_print(d->name);
    int row = 2;
    char line[112];
    char num[24];

    strncpy(line, "  device     : ", sizeof(line) - 1);
    dt_append(line, sizeof(line), d->name);
    tui_print_at(row++, 1, line);

    u64_to_str((u64)d->type, num);
    strncpy(line, "  type       : ", sizeof(line) - 1);
    dt_append(line, sizeof(line), num);
    tui_print_at(row++, 1, line);

    u64_to_str(d->sectors, num);
    strncpy(line, "  sectors    : ", sizeof(line) - 1);
    dt_append(line, sizeof(line), num);
    tui_print_at(row++, 1, line);

    u64_to_str((u64)d->sector_size, num);
    strncpy(line, "  sector size: ", sizeof(line) - 1);
    dt_append(line, sizeof(line), num);
    tui_print_at(row++, 1, line);

    u64 mib = (d->sectors * (u64)d->sector_size) >> 20;
    u64_to_str(mib, num);
    strncpy(line, "  capacity   : ", sizeof(line) - 1);
    dt_append(line, sizeof(line), num);
    dt_append(line, sizeof(line), " MiB");
    tui_print_at(row++, 1, line);

    if (d->bus || d->dev || d->func) {
        static const char hx[] = "0123456789abcdef";
        int m = 0;
        line[m++] = ' '; line[m++] = ' '; line[m++] = 'p'; line[m++] = 'c';
        line[m++] = 'i'; line[m++] = ' '; line[m++] = ' '; line[m++] = ':';
        line[m++] = ' '; line[m++] = '0'; line[m++] = '0'; line[m++] = ':';
        line[m++] = hx[(d->dev >> 4) & 0xF]; line[m++] = hx[d->dev & 0xF];
        line[m++] = '.'; line[m++] = hx[d->func & 0xF];
        line[m] = 0;
        tui_print_at(row++, 1, line);
    }

    row++;
    driver_block_stats_t st;
    memset(&st, 0, sizeof(st));
    driver_block_get_stats(d->index, &st);
    u64_to_str(st.reads, num);
    strncpy(line, "  reads      : ", sizeof(line) - 1);
    dt_append(line, sizeof(line), num);
    tui_print_at(row++, 1, line);
    u64_to_str(st.writes, num);
    strncpy(line, "  writes     : ", sizeof(line) - 1);
    dt_append(line, sizeof(line), num);
    tui_print_at(row++, 1, line);
    u64_to_str(st.read_sectors, num);
    strncpy(line, "  rd sectors : ", sizeof(line) - 1);
    dt_append(line, sizeof(line), num);
    tui_print_at(row++, 1, line);
    u64_to_str(st.write_sectors, num);
    strncpy(line, "  wr sectors : ", sizeof(line) - 1);
    dt_append(line, sizeof(line), num);
    tui_print_at(row++, 1, line);

    tui_dt_page_wait_close(NULL);
    return 0;
}

/* level-5 MENU populate: attach Detail + rw_test to one disk */
static int dt_disk_children_populate(tui_dt_node_t *self) {
    driver_block_device_t *d =
        self ? (driver_block_device_t *)self->userptr : NULL;
    if (!d) return -1;
    while (self->child_count > 0) {
        tui_dt_node_t *c = self->children[0];
        tui_dt_node_remove(c->owner, c->id);
    }
    tui_dt_node_t s;
    memset(&s, 0, sizeof(s));
    strncpy(s.id, self->id, sizeof(s.id) - 1);
    dt_append(s.id, sizeof(s.id), ".detail");
    strncpy(s.en, "Detail", sizeof(s.en) - 1);
    strncpy(s.zh, "详情", sizeof(s.zh) - 1);
    strncpy(s.desc_en, "device detail (live)", sizeof(s.desc_en) - 1);
    strncpy(s.desc_zh, "设备详情（实时）", sizeof(s.desc_zh) - 1);
    s.kind = TUI_DT_PAGE;
    s.run = tui_dt_disk_detail_run;
    s.userptr = d;
    strncpy(s.owner, "builtin", sizeof(s.owner) - 1);
    tui_dt_node_add(self->id, &s);

    memset(&s, 0, sizeof(s));
    strncpy(s.id, self->id, sizeof(s.id) - 1);
    dt_append(s.id, sizeof(s.id), ".rw");
    strncpy(s.en, "rw_test", sizeof(s.en) - 1);
    strncpy(s.zh, "读写测试", sizeof(s.zh) - 1);
    strncpy(s.desc_en, "runs: disk_rw_test <dev>", sizeof(s.desc_en) - 1);
    strncpy(s.desc_zh, "运行 disk_rw_test", sizeof(s.desc_zh) - 1);
    s.kind = TUI_DT_SHELL;
    if (d->index >= 0 && d->index < 8) {
        strncpy(g_disk_rw_cmd[d->index], "disk_rw_test ",
                sizeof(g_disk_rw_cmd[d->index]) - 1);
        dt_append(g_disk_rw_cmd[d->index], sizeof(g_disk_rw_cmd[d->index]), d->name);
        s.shell_cmd = g_disk_rw_cmd[d->index];
    }
    s.userptr = d;
    strncpy(s.owner, "builtin", sizeof(s.owner) - 1);
    tui_dt_node_add(self->id, &s);
    return 0;
}

/* level-4 MENU populate: enumerate real block devices */
int tui_dt_disks_populate(tui_dt_node_t *self) {
    if (!self) return -1;
    while (self->child_count > 0) {
        tui_dt_node_t *c = self->children[0];
        tui_dt_node_remove(c->owner, c->id);
    }
    int added = 0;
    for (int i = 0; i < 16; i++) {
        driver_block_device_t *d = driver_block_get_device(i);
        if (!d || !d->present) continue;
        tui_dt_node_t n;
        memset(&n, 0, sizeof(n));
        strncpy(n.id, "disk.", sizeof(n.id) - 1);
        dt_append(n.id, sizeof(n.id), d->name);
        strncpy(n.en, d->name, sizeof(n.en) - 1);
        strncpy(n.zh, d->name, sizeof(n.zh) - 1);
        strncpy(n.desc_en, "device actions", sizeof(n.desc_en) - 1);
        strncpy(n.desc_zh, "设备操作", sizeof(n.desc_zh) - 1);
        strncpy(n.icon, "[d]", sizeof(n.icon) - 1);
        n.kind = TUI_DT_MENU;
        n.priority = 10 + i;
        n.userptr = d;
        n.populate = dt_disk_children_populate;
        strncpy(n.owner, "builtin", sizeof(n.owner) - 1);
        if (tui_dt_node_add("panel.st.disks", &n) == 0) added++;
    }
    return 0;
}
