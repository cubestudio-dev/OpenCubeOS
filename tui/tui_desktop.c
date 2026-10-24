/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-10-tui-unified
 * File: tui/tui_desktop.c
 * Purpose: the unified TUI desktop (single `tui` entry).  See
 *          tui/tui_desktop.h for the contract.
 *
 * Layout of the main screen:
 *
 *   row 0          title bar (reverse video)
 *   row 1          breadcrumb: root > Control Panel > Storage > Disks
 *   row 2          separator
 *   row 3..h-3     menu items: "icon  label  -  description"
 *                  the selected row is drawn REVERSE (strong focus)
 *   row h-2        toast line (last result: green OK / red FAIL:...)
 *   row h-1        resident status bar (hotkey map, per language)
 *
 * Every submenu is a registry node; nothing in the renderer knows any
 * application by name.  Applications are mounted by
 * tui_dt_bootstrap() (builtin, idempotent) and by any L1 module via
 * tui_dt_node_add() - the same call, the same rules.
 *
 * Shell parity: the Command Bridge page enumerates the live shell
 * command table (shell_cmd_enum) and runs any command with its real
 * output captured (shell_execute_captured) inside the TUI, so every
 * shell capability is reachable without leaving the desktop.
 */
#include "tui_desktop.h"
#include "../core/lib/lib_string.h"
#include "../core/lib/lib_config.h"
#include "../core/mem/mem_heap.h"
#include "../core/mem/mem_pmm.h"
#include "../recovery/shell.h"
#include "../drivers/block/driver_block_blk.h"
#include "../drivers/input/driver_input_keyboard.h"
#include "../drivers/display/screen_console.h"
#include "../core/core_usermode.h"
#include "../core/mem/mem_pmm.h"

/* application entry points mounted by bootstrap (all of them assume
 * the desktop's tui session is already initialized) */
int  tui_sysinfo_main(void);
int  tui_net_main(void);
int  tui_hw_main(void);
int  tui_hw_run_page(const char *page);
int  tui_time_main(void);
int  tui_sys_main(void);
int  tui_cn_input_app(void);
int  tui_chinese_init(void);
int  tui_ext_page_main(const char *name);

/* ------------------------------------------------------------------ */
/* language                                                            */
/* ------------------------------------------------------------------ */

int tui_lang_current(void) {
    /* single source of truth: shell.lang (set by `lang`, /etc/opencube.conf
     * or the F2 switch below).  No separate desktop copy to drift. */
    return shell_lang_get();
}

int tui_lang_set(int lang) {
    int l = (lang == SHELL_LANG_ZH) ? SHELL_LANG_ZH : SHELL_LANG_EN;
    shell_lang_set(l);
    /* persist exactly like `lang zh` does; report the config write rc
     * honestly (0 = written, negative = config backend refused) */
    int wr = lib_config_write("shell.lang", shell_lang_name());
    tui_dt_toast(1, tui_tr("language: %s (config write rc=%d)", "语言：%s（配置写入 rc=%d）"),
                 shell_lang_name(), wr);
    return 0;
}

const char *tui_tr(const char *en, const char *zh) {
    return (shell_lang_get() == SHELL_LANG_ZH) ? zh : en;
}

/* ------------------------------------------------------------------ */
/* crash guard (exception isolation)                                   */
/* ------------------------------------------------------------------ */

/* WP-10-tui-qa: the guard buffer is now a small depth STACK.  The old
 * single g_guard_jb[5] was overwritten by any nested tui_dt_guard()
 * call (an L1 app that guards its own sub-page), so a later panic in
 * the OUTER frame longjmp'ed into the dead inner frame - undefined
 * behavior waiting for the first nested L1 extension.  4 levels cover
 * desktop -> app -> guarded sub-page -> one more; deeper nesting is
 * refused honestly (-2) instead of corrupting the stack. */
#define TUI_DT_GUARD_DEPTH 4
static void *g_guard_jb[TUI_DT_GUARD_DEPTH][5];
static int   g_guard_depth = 0;

void tui_dt_panic(int code) {
    (void)code;
    if (g_guard_depth > 0)
        __builtin_longjmp(g_guard_jb[g_guard_depth - 1], 1);
    /* no active guard: harmless - the caller returns normally */
}

/* WP-11-crash_recovery: asked by the exception dispatcher (ring-0
 * fault path) BEFORE deciding between the TUI fence and a real
 * kernel panic. 1 = some tui_dt_guard() frame is live on this task,
 * so tui_dt_panic() would longjmp somewhere useful. */
int tui_dt_guard_active(void) {
    return g_guard_depth > 0 ? 1 : 0;
}

int tui_dt_guard(int (*fn)(tui_dt_node_t *), tui_dt_node_t *self) {
    int level, rc;
    if (!fn) return -1;
    if (g_guard_depth >= TUI_DT_GUARD_DEPTH) return -2;   /* too deep */
    level = g_guard_depth++;
    if (__builtin_setjmp(g_guard_jb[level]) == 0) {
        rc = fn(self);
    } else {
        rc = -1000;   /* escaped via tui_dt_panic */
        /* WP-11-crash_recovery: we arrived from the MIDDLE of an
         * exception handler (arch_exc_dispatch -> tui_dt_panic).
         * The exception cleared the IF flag on entry and no iretq will
         * ever restore it - this frame would run with interrupts
         * disabled forever and the very next hlt-based key wait would
         * freeze the desktop. Re-enable them HERE: the guard runs in
         * normal (interrupts-on) desktop context, so sti restores the
         * pre-fault state. */
        __asm__ volatile("sti");
    }
    g_guard_depth--;
    return rc;
}

/* ------------------------------------------------------------------ */
/* registry                                                            */
/* ------------------------------------------------------------------ */

/* the registry lives on the kernel heap: 56 nodes * ~450 B is too
 * precious for the 0x400000 identity window (.bss), and the desktop
 * session is exactly the lifetime the heap serves best */
static tui_dt_node_t *g_nodes = NULL;        /* slot 0 = "root" */
static u8            *g_node_used = NULL;
static int            g_node_count = 0;

static void dt_registry_alloc(void) {
    if (g_nodes) return;
    g_nodes = kmalloc(sizeof(tui_dt_node_t) * TUI_DT_MAX);
    g_node_used = kmalloc(TUI_DT_MAX);
    if (g_nodes) memset(g_nodes, 0, sizeof(tui_dt_node_t) * TUI_DT_MAX);
    if (g_node_used) memset(g_node_used, 0, TUI_DT_MAX);
    g_node_count = 0;
}

static tui_dt_node_t *dt_root(void) {
    return &g_nodes[0];
}

static void dt_clear_node(tui_dt_node_t *n) {
    memset(n, 0, sizeof(*n));
}

void tui_dt_reset(void) {
    if (!g_nodes) { dt_registry_alloc(); return; }
    for (int i = 0; i < TUI_DT_MAX; i++) {
        dt_clear_node(&g_nodes[i]);
        g_node_used[i] = 0;
    }
    g_node_count = 0;
}

int tui_dt_count(void) {
    return g_nodes ? g_node_count : 0;
}

tui_dt_node_t *tui_dt_find(const char *id) {
    if (!g_nodes) return NULL;
    if (!id || !id[0]) id = "root";
    for (int i = 0; i < TUI_DT_MAX; i++)
        if (g_node_used[i] && strcmp(g_nodes[i].id, id) == 0)
            return &g_nodes[i];
    return NULL;
}

int tui_dt_depth(const char *id) {
    tui_dt_node_t *n = tui_dt_find(id);
    return n ? n->level : -1;
}

/* insert a child pointer keeping (priority, en) ascending order */
static void dt_child_insert_sorted(tui_dt_node_t *parent, tui_dt_node_t *child) {
    int pos = parent->child_count;
    for (int i = 0; i < parent->child_count; i++) {
        tui_dt_node_t *c = parent->children[i];
        if (child->priority < c->priority ||
            (child->priority == c->priority &&
             strcasecmp(child->en, c->en) < 0)) {
            pos = i;
            break;
        }
    }
    for (int i = parent->child_count; i > pos; i--)
        parent->children[i] = parent->children[i - 1];
    parent->children[pos] = child;
    parent->child_count++;
}

static void dt_link(tui_dt_node_t *node, tui_dt_node_t *parent) {
    node->parent = parent;
    node->level = parent ? parent->level + 1 : 0;
    if (parent) dt_child_insert_sorted(parent, node);
}

static void dt_unlink(tui_dt_node_t *node) {
    tui_dt_node_t *p = node->parent;
    if (!p) return;
    int at = -1;
    for (int i = 0; i < p->child_count; i++)
        if (p->children[i] == node) { at = i; break; }
    if (at < 0) return;
    for (int i = at; i < p->child_count - 1; i++)
        p->children[i] = p->children[i + 1];
    p->children[p->child_count - 1] = NULL;
    p->child_count--;
    node->parent = NULL;
}

static int dt_remove_recursive(tui_dt_node_t *node) {
    int freed = 1;
    while (node->child_count > 0)
        freed += dt_remove_recursive(node->children[0]);
    int slot = (int)(node - g_nodes);
    dt_unlink(node);
    dt_clear_node(node);
    g_node_used[slot] = 0;
    g_node_count--;
    return freed;
}

static int dt_is_root_id(const char *id) {
    return (!id || !id[0] || strcmp(id, "root") == 0);
}

int tui_dt_node_add(const char *parent_id, const tui_dt_node_t *spec) {
    dt_registry_alloc();
    if (!g_nodes) return -3;
    if (!spec || !spec->id[0]) return -1;
    if (g_node_count >= TUI_DT_MAX) return -3;
    if (tui_dt_find(spec->id)) return -2;         /* duplicate id */
    tui_dt_node_t *parent = dt_is_root_id(parent_id)
                                ? dt_root() : tui_dt_find(parent_id);
    if (!parent) return -4;
    if (!dt_is_root_id(parent_id) && parent->level >= TUI_DT_DEPTH_MAX)
        return -5;                                /* deeper than the max */

    int slot = -1;
    for (int i = 1; i < TUI_DT_MAX; i++)
        if (!g_node_used[i]) { slot = i; break; }
    if (slot < 0) return -3;

    tui_dt_node_t *n = &g_nodes[slot];
    dt_clear_node(n);
    strncpy(n->id, spec->id, sizeof(n->id) - 1);
    strncpy(n->en, spec->en, sizeof(n->en) - 1);
    strncpy(n->zh, spec->zh, sizeof(n->zh) - 1);
    strncpy(n->desc_en, spec->desc_en, sizeof(n->desc_en) - 1);
    strncpy(n->desc_zh, spec->desc_zh, sizeof(n->desc_zh) - 1);
    strncpy(n->icon, spec->icon, sizeof(n->icon) - 1);
    n->kind = spec->kind;
    n->run = spec->run;
    n->populate = spec->populate;
    n->shell_cmd = spec->shell_cmd;
    n->priority = spec->priority;
    n->userptr = spec->userptr;
    strncpy(n->owner, spec->owner[0] ? spec->owner : "builtin",
            sizeof(n->owner) - 1);
    g_node_used[slot] = 1;
    g_node_count++;
    dt_link(n, parent);
    return 0;
}

int tui_dt_node_remove(const char *owner, const char *id) {
    if (!id || !id[0] || dt_is_root_id(id)) return -1;
    tui_dt_node_t *n = tui_dt_find(id);
    if (!n) return -2;
    if (owner && owner[0] && strcmp(n->owner, owner) != 0)
        return -3;   /* only the owning module may remove its nodes */
    return dt_remove_recursive(n);
}

/* bounded string append (the kernel string lib has no strncat) */
void tui_dt_str_append(char *dst, int cap, const char *src) {
    if (!dst || cap <= 0) return;
    int len = (int)strlen(dst);
    if (len + 1 >= cap) return;
    int k = 0;
    while (src[k] && len + 1 < cap) dst[len++] = src[k++];
    dst[len] = 0;
}

/* clear one full row to the window background (rendering helper) */
static void dt_clear_row(int row);

/* ------------------------------------------------------------------ */
/* feedback: toast, loading, status bar                                */
/* ------------------------------------------------------------------ */

#define DT_TOAST_MAX 128

static tui_window_t *g_dt_win = NULL;
static char g_toast[DT_TOAST_MAX];     /* "" = nothing to show         */
static int  g_toast_ok = 1;
static char g_loading[64];             /* "" = idle                    */
static u32  g_loading_frame = 0;

/* clear one full row to the window background (rendering helper) */
static void dt_clear_row(int row) {
    if (!g_dt_win || row < 0 || row >= g_dt_win->h) return;
    for (int x = 0; x < g_dt_win->w; x++) {
        g_dt_win->cells[row * g_dt_win->w + x].ch = ' ';
        g_dt_win->cells[row * g_dt_win->w + x].attr = g_dt_win->bkgd;
    }
}

void tui_dt_toast(int ok, const char *fmt, ...) {
    if (!fmt) { g_toast[0] = 0; return; }
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    int n = 0;
    for (const char *p = fmt; *p && n < DT_TOAST_MAX - 1; p++) {
        if (*p != '%') { g_toast[n++] = *p; continue; }
        p++;
        if (*p == 's') {
            const char *s = __builtin_va_arg(ap, const char *);
            while (s && *s && n < DT_TOAST_MAX - 1) g_toast[n++] = *s++;
        } else if (*p == 'd') {
            long long v = (long long)__builtin_va_arg(ap, int);
            char num[16];
            if (v < 0) { g_toast[n++] = '-'; v = -v; }
            u64_to_str((u64)v, num);
            for (int i = 0; num[i] && n < DT_TOAST_MAX - 1; i++)
                g_toast[n++] = num[i];
        } else if (*p == '%') {
            g_toast[n++] = '%';
        } else if (*p == 0) {
            break;
        }
    }
    __builtin_va_end(ap);
    g_toast[n] = 0;
    g_toast_ok = ok ? 1 : 0;
}

void tui_dt_status_bar(void) {
    if (!g_dt_win) return;
    int rows = g_dt_win->h;
    tui_attr_t bar = TUI_MAKE_ATTR(TUI_COLOR_BLACK, TUI_COLOR_AQUA);
    tui_attr_t save = g_dt_win->attr;
    g_dt_win->attr = bar;
    for (int x = 0; x < g_dt_win->w; x++) {
        g_dt_win->cells[(rows - 1) * g_dt_win->w + x].ch = ' ';
        g_dt_win->cells[(rows - 1) * g_dt_win->w + x].attr = bar;
    }
    g_dt_win->curs_x = 0;
    g_dt_win->curs_y = rows - 1;
    g_dt_win->attr = bar;
    tui_print(tui_tr(
        " Esc:Back 1-9:Quick Up/Down:Move Enter:Open F1:Help F2:Lang F5:Refresh F10:Exit ",
        " Esc:返回 1-9:快开 上下:选择 Enter:打开 F1:帮助 F2:语言 F5:刷新 F10:退出 "));
    g_dt_win->attr = save;
}

/* draw the toast row (row h-2); empty string clears it */
static void dt_draw_toast(void) {
    if (!g_dt_win) return;
    int rows = g_dt_win->h;
    int row = rows - 2;
    dt_clear_row(row);
    if (!g_toast[0]) return;
    tui_attr_t attr = g_toast_ok
        ? TUI_MAKE_ATTR(TUI_COLOR_LIME,  TUI_COLOR_BLACK) | TUI_ATTR_BOLD
        : TUI_MAKE_ATTR(TUI_COLOR_WHITE, TUI_COLOR_MAROON) | TUI_ATTR_REVERSE;
    tui_attr_t save = g_dt_win->attr;
    g_dt_win->attr = attr;
    g_dt_win->curs_x = 0;
    g_dt_win->curs_y = row;
    tui_print(g_toast_ok ? tui_tr(" OK: ", " 成功：") : tui_tr(" FAIL: ", " 失败："));
    tui_print(g_toast);
    g_dt_win->attr = save;
}

const char *tui_dt_toast_text(int *ok) {
    if (ok) *ok = g_toast_ok;
    return g_toast;
}

void tui_dt_loading_begin(const char *msg) {
    strncpy(g_loading, msg ? msg : "", sizeof(g_loading) - 1);
    g_loading[sizeof(g_loading) - 1] = 0;
    g_loading_frame = 0;
}

void tui_dt_loading_tick(void) {
    if (!g_loading[0] || !g_dt_win) return;
    static const char spin[] = "|/-\\";
    int rows = g_dt_win->h;
    int row = rows - 2;
    dt_clear_row(row);
    tui_attr_t attr = TUI_MAKE_ATTR(TUI_COLOR_YELLOW, TUI_COLOR_BLACK) | TUI_ATTR_BOLD;
    tui_attr_t save = g_dt_win->attr;
    g_dt_win->attr = attr;
    g_dt_win->curs_x = 0;
    g_dt_win->curs_y = row;
    tui_print(" [");
    char f[2] = { spin[g_loading_frame % 4], 0 };
    tui_print(f);
    tui_print("] ");
    tui_print(g_loading);
    tui_print(" ...");
    g_loading_frame++;
    g_dt_win->attr = save;
}

void tui_dt_loading_end(int ok, const char *okmsg, const char *errmsg) {
    g_loading[0] = 0;
    tui_dt_toast(ok, "%s", ok ? (okmsg ? okmsg : tui_tr("done", "完成"))
                              : (errmsg ? errmsg : tui_tr("failed", "失败")));
}

/* ------------------------------------------------------------------ */
/* WP-10-tui-native: modal confirmation dialog                          */
/* ------------------------------------------------------------------ */

int tui_dt_confirm(const char *en, const char *zh) {
    int rows = tui_dt_page_rows(), cols = tui_dt_page_cols();
    const char *msg = tui_tr(en ? en : "confirm?", zh ? zh : "确认？");
    int w = 54, h = 5;
    if (cols < 60) w = cols - 6;
    if (w < 30) w = 30;
    tui_window_t *win = tui_window_new(rows / 2 - 2, (cols - w) / 2, h, w);
    if (!win) return -1;
    tui_window_t *prev = tui_window_focus();
    tui_window_focus_set(win);
    tui_window_clear(win);
    tui_window_border(win, &TUI_BORDER_DOUBLE);
    tui_window_refresh(win);
    tui_attr_t hdr = TUI_MAKE_ATTR(TUI_COLOR_WHITE, TUI_COLOR_MAROON) | TUI_ATTR_BOLD;
    tui_set_attr(hdr);
    tui_print_at(0, 2, tui_tr(" Confirm ", " 确认 "));
    tui_set_attr(TUI_MAKE_ATTR(TUI_COLOR_SILVER, TUI_COLOR_BLACK));
    tui_print_at(1, 2, msg);
    tui_set_attr(TUI_MAKE_ATTR(TUI_COLOR_YELLOW, TUI_COLOR_BLACK));
    tui_print_at(3, 2, tui_tr(" y:yes  n:no  Esc:cancel ",
                              " y:是  n:否  Esc:取消 "));
    tui_window_refresh(win);
    int rc = -1;
    for (;;) {
        int k = tui_getch();
        if (k == 'y' || k == 'Y' || k == '\r' || k == TUI_KEY_ENTER) { rc = 1; break; }
        if (k == 'n' || k == 'N') { rc = 0; break; }
        if (k == TUI_KEY_ESC || k == TUI_KEY_F10) { rc = -1; break; }
    }
    tui_window_free(win);
    /* hand the focus back to whoever had it (the live page or menu) */
    if (prev && prev != win) tui_window_focus_set(prev);
    else {
        tui_window_t *pg = tui_dt_page_window();
        if (pg) tui_window_focus_set(pg);
        else if (g_dt_win) tui_window_focus_set(g_dt_win);
    }
    tui_update_panels();
    return rc;
}

/* ------------------------------------------------------------------ */
/* main-screen rendering                                               */
/* ------------------------------------------------------------------ */

static void dt_print_at(int row, int col, const char *s) {
    if (!g_dt_win) return;
    if (row < 0 || row >= g_dt_win->h || col < 0) return;
    g_dt_win->curs_x = col;
    g_dt_win->curs_y = row;
    tui_print(s);
}

/* forward: the app pages (tui_dt_apps.c) */
int tui_dt_help_page_run(tui_dt_node_t *self);
int tui_dt_cmdbridge_run(tui_dt_node_t *self);
int tui_dt_lang_run(tui_dt_node_t *self);
int tui_dt_crashtest_run(tui_dt_node_t *self);   /* WP-11 */
int tui_dt_update_run(tui_dt_node_t *self);
int tui_dt_disk_detail_run(tui_dt_node_t *self);
int tui_dt_disks_populate(tui_dt_node_t *self);

/* draw one full menu frame for `node` with `sel` selected */
static void dt_render_menu(tui_dt_node_t *node, int sel) {
    if (!g_dt_win) return;
    tui_window_focus_set(g_dt_win);   /* all tui_print* target g_focus */
    int rows = g_dt_win->h;
    int cols = g_dt_win->w;
    tui_attr_t def = g_dt_win->bkgd;

    /* title bar */
    tui_attr_t title = TUI_MAKE_ATTR(TUI_COLOR_WHITE, TUI_COLOR_TEAL) | TUI_ATTR_BOLD;
    g_dt_win->attr = title;
    for (int x = 0; x < cols; x++) {
        g_dt_win->cells[0 * cols + x].ch = ' ';
        g_dt_win->cells[0 * cols + x].attr = title;
    }
    {
        char hdr[96];
        strncpy(hdr, "Open Cube OS Desktop", sizeof(hdr) - 1);
        hdr[sizeof(hdr) - 1] = 0;
        /* right side: language marker */
        const char *lg = (shell_lang_get() == SHELL_LANG_ZH) ? "[zh]" : "[en]";
        int want = (int)strlen(hdr) + (int)strlen(lg) + 2;
        int pad = cols - want; if (pad < 1) pad = 1;
        g_dt_win->curs_x = pad > 0 ? pad : 0;
        g_dt_win->curs_y = 0;
        tui_print(hdr);
        tui_print("  ");
        tui_print(lg);
    }

    /* breadcrumb: root > ... > current */
    tui_attr_t crumb = TUI_MAKE_ATTR(TUI_COLOR_SILVER, TUI_COLOR_BLACK);
    g_dt_win->attr = crumb;
    dt_clear_row(1);
    {
        tui_dt_node_t *chain[8];
        int depth = 0;
        for (tui_dt_node_t *p = node; p && depth < 8; p = p->parent)
            chain[depth++] = p;
        g_dt_win->curs_x = 1;
        g_dt_win->curs_y = 1;
        for (int i = depth - 1; i >= 0; i--) {
            tui_print(i == depth - 1 ? "" : " > ");
            tui_print(tui_tr(chain[i]->en, chain[i]->zh));
        }
    }

    dt_clear_row(2);

    /* items */
    int first_row = 3;
    int max_items = rows - first_row - 2;   /* toast + status bar rows */
    if (max_items < 1) max_items = 1;
    int top = sel - (max_items - 1);
    if (top < 0) top = 0;
    /* clear the whole items band first: a menu with fewer children
     * than the previous frame must not leave stale rows behind */
    for (int r = first_row; r < first_row + max_items && r < rows - 2; r++)
        dt_clear_row(r);
    tui_attr_t normal = TUI_MAKE_ATTR(TUI_COLOR_SILVER, TUI_COLOR_BLACK);
    tui_attr_t focus  = TUI_MAKE_ATTR(TUI_COLOR_BLACK,  TUI_COLOR_SILVER)
                        | TUI_ATTR_BOLD;   /* strong focus highlight   */
    for (int i = 0; i < node->child_count; i++) {
        int row = first_row + i - top;
        if (row < first_row || row >= first_row + max_items) continue;
        tui_dt_node_t *c = node->children[i];
        g_dt_win->attr = (i == sel) ? focus : normal;
        dt_clear_row(row);
        char line[128];
        int n = 0;
        const char *icon = c->icon[0] ? c->icon : "   ";
        for (int k = 0; icon[k] && n < 120; k++) line[n++] = icon[k];
        if (n < 120) line[n++] = ' ';
        const char *lab = tui_tr(c->en, c->zh);
        for (int k = 0; lab[k] && n < 120; k++) line[n++] = lab[k];
        const char *desc = tui_tr(c->desc_en, c->desc_zh);
        if (desc[0] && n < 118) {
            line[n++] = ' '; line[n++] = ' '; line[n++] = '-';
            line[n++] = ' ';
            for (int k = 0; desc[k] && n < 122; k++) line[n++] = desc[k];
        }
        line[n] = 0;
        dt_print_at(row, 1, line);
    }

    /* loading / toast rows */
    if (g_loading[0]) tui_dt_loading_tick();
    else dt_draw_toast();

    tui_dt_status_bar();
    tui_window_refresh(g_dt_win);
    (void)def; (void)cols;
}

/* ------------------------------------------------------------------ */
/* node activation                                                     */
/* ------------------------------------------------------------------ */

/* wait for one key on the plain console (used after a SHELL/TERMINAL
 * node temporarily left the TUI) - same pump as the shell line editor */
int tui_dt_console_wait_key(void) {
    for (;;) {
        int k = driver_input_keyboard_getch();
        if (k >= 0) return k;
        __asm__ volatile("sti");
        __asm__ volatile("hlt");
    }
}

/* terminal size helpers (shared by the desktop paths) */
static int dt_term_rows(void) { int c, r; tui_term_size(&c, &r); return r; }
static int dt_term_cols(void) { int c, r; tui_term_size(&c, &r); return c; }

/* rebuild the desktop window after tui_init() (SHELL/TERMINAL return) */
int tui_dt_reenter(void) {
    if (tui_init() != 0) return -1;
    g_dt_win = tui_window_new(0, 0, dt_term_rows(), dt_term_cols());
    if (!g_dt_win) return -1;
    tui_window_focus_set(g_dt_win);
    return 0;
}

/* leave the TUI, run a shell command line with captured output, show
 * it on the restored console, wait for a key, come back */
static int dt_run_shell_cmd(const char *line, const char *what) {
    if (!line || !line[0]) { tui_dt_toast(0, tui_tr("empty command", "空命令")); return -1; }
    /* 16KB output buffer comes from the kernel heap (linker identity
     * window is precious - large buffers never live in .bss) */
    char *out = kmalloc(16384);
    if (!out) { tui_dt_toast(0, tui_tr("out of memory", "内存不足")); return -1; }
    tui_shutdown();
    screen_console_puts("\n[desktop] ");
    screen_console_puts(what ? what : line);
    screen_console_puts("\n");
    int rc = shell_execute_captured(line, out, 16384);
    screen_console_puts(out);
    if (!out[0]) screen_console_putc('\n');
    screen_console_puts("\n[rc=");
    char num[12];
    u64_to_str((u64)(rc < 0 ? 0 : (u32)rc), num);
    screen_console_puts(num);
    screen_console_puts(tui_tr("] press any key to return... ",
                               "] \xe6\x8c\x89\xe4\xbb\xbb\xe6\x84\x8f\xe9\x94\xae\xe8\xbf\x94\xe5\x9b\x9e..."));
    tui_dt_console_wait_key();
    kfree(out);
    if (tui_dt_reenter() != 0) return rc;
    return rc;
}

/* run a ring-3 terminal: leave the TUI, spawn ush (Ring 3, syscall
 * only), wait, then re-enter the desktop */
static int dt_run_terminal(void) {
    tui_shutdown();
    /* WP-10-tui-native: the banner follows shell.lang too */
    screen_console_puts("\n");
    screen_console_puts(tui_tr("[terminal] ring-3 ush - syscall-only sandbox; "
                               "type 'exit' to return to the desktop\n",
                               "[终端] Ring 3 ush - 仅 syscall 沙箱；"
                               "输入 'exit' 返回桌面\n"));
    int rc = shell_execute("run ush");
    screen_console_puts("\n");
    screen_console_puts(tui_tr("[terminal] ended (rc=", "[终端] 已结束 (rc="));
    char num[12];
    u64_to_str((u64)(rc < 0 ? 0 : (u32)rc), num);
    screen_console_puts(num);
    screen_console_puts(tui_tr(") - press any key for the desktop...",
                               ") - 按任意键返回桌面..."));
    tui_dt_console_wait_key();
    if (tui_dt_reenter() != 0) return rc;
    return rc;
}

static int dt_activate(tui_dt_node_t *node) {
    if (!node) return -1;
    switch (node->kind) {
    case TUI_DT_MENU:
        return 0;   /* handled by the nav loop (push) */
    case TUI_DT_PAGE:
    case TUI_DT_APP: {
        tui_dt_loading_begin(tui_tr("opening", "正在打开"));
        int rc = tui_dt_guard(node->run, node);
        if (rc < 0) {
            tui_dt_loading_end(0, NULL, tui_tr("page error",
                                               "页面错误"));
            tui_dt_toast(0, tui_tr("page '%s' failed (rc=%d)%s",
                                   "页面 '%s' 失败 (rc=%d)%s"),
                         node->id, rc,
                         rc == -1000 ? tui_tr(" - contained", " - 已隔离") : "");
        } else {
            tui_dt_loading_end(1, tui_tr("back to menu", "已返回菜单"), NULL);
        }
        return rc;
    }
    case TUI_DT_SHELL: {
        int rc = dt_run_shell_cmd(node->shell_cmd, node->shell_cmd);
        tui_dt_toast(rc >= 0, tui_tr("%s rc=%d", "%s rc=%d"), node->id, rc);
        return rc;
    }
    case TUI_DT_TERMINAL:
        return dt_run_terminal();
    default:
        tui_dt_toast(0, tui_tr("unknown node kind %d", "未知节点类型 %d"), node->kind);
        return -1;
    }
}

/* ------------------------------------------------------------------ */
/* navigation loop                                                     */
/* ------------------------------------------------------------------ */

#define DT_STACK_MAX (TUI_DT_DEPTH_MAX + 1)

static tui_dt_node_t *g_stack[DT_STACK_MAX];   /* g_stack[0] = root    */
static int            g_stack_top = 0;
static int            g_sel[DT_STACK_MAX];

static void dt_push(tui_dt_node_t *node) {
    if (g_stack_top + 1 < DT_STACK_MAX) {
        g_stack_top++;
        g_stack[g_stack_top] = node;
        if (g_sel[g_stack_top] >= node->child_count)
            g_sel[g_stack_top] = node->child_count ? node->child_count - 1 : 0;
    }
}

int tui_desktop_main(void) {
    dt_registry_alloc();
    if (!g_nodes) {
        screen_console_puts(tui_tr("tui: registry alloc failed\n",
                                    "TUI：注册表分配失败\n"));
        return -1;
    }

    if (!g_node_used[0]) {
        /* fresh registry: create the root */
        dt_clear_node(dt_root());
        strncpy(dt_root()->id, "root", sizeof(dt_root()->id) - 1);
        strncpy(dt_root()->en, "Desktop", sizeof(dt_root()->en) - 1);
        strncpy(dt_root()->zh, "\xe6\xa1\x8c\xe9\x9d\xa2", sizeof(dt_root()->zh) - 1);
        g_node_used[0] = 1;
        g_node_count = 1;
    }
    tui_dt_bootstrap();

    if (tui_init() != 0) {
        screen_console_puts(tui_tr("tui: desktop init failed\n",
                                    "TUI：桌面初始化失败\n"));
        return -1;
    }
    g_dt_win = tui_window_new(0, 0, dt_term_rows(), dt_term_cols());
    if (!g_dt_win) {
        tui_shutdown();
        screen_console_puts(tui_tr("tui: desktop window failed\n",
                                    "TUI：桌面窗口创建失败\n"));
        return -1;
    }
    tui_window_focus_set(g_dt_win);

    g_stack[0] = dt_root();
    g_stack_top = 0;
    if (g_sel[0] >= dt_root()->child_count)
        g_sel[0] = dt_root()->child_count ? dt_root()->child_count - 1 : 0;

    for (;;) {
        tui_dt_node_t *cur = g_stack[g_stack_top];
        /* lazily populate dynamic menus (device lists etc) */
        if (cur->populate) {
            tui_dt_guard(cur->populate, cur);
            cur->populate = NULL;   /* populated once per entry */
        }
        if (g_sel[g_stack_top] >= cur->child_count)
            g_sel[g_stack_top] = cur->child_count ? cur->child_count - 1 : 0;
        dt_render_menu(cur, g_sel[g_stack_top]);

        int k = tui_getch();
        switch (k) {
        case TUI_KEY_UP:
        case TUI_KEY_CTRL_P:
            if (g_sel[g_stack_top] > 0) g_sel[g_stack_top]--;
            break;
        case TUI_KEY_DOWN:
        case TUI_KEY_CTRL_N:
            if (g_sel[g_stack_top] < cur->child_count - 1)
                g_sel[g_stack_top]++;
            break;
        case TUI_KEY_ENTER: {
            tui_dt_node_t *c = cur->children[g_sel[g_stack_top]];
            if (!c) { tui_dt_toast(0, tui_tr("no entries", "无条目")); break; }
            g_toast[0] = 0;   /* a new action clears the old toast */
            if (c->kind == TUI_DT_MENU) {
                if (c->level > TUI_DT_DEPTH_MAX) {
                    tui_dt_toast(0, tui_tr("max menu depth %d", "菜单最深 %d 层"), TUI_DT_DEPTH_MAX);
                    break;
                }
                dt_push(c);
            } else {
                dt_activate(c);
            }
            break;
        }
        case TUI_KEY_ESC:
        case TUI_KEY_BACKSPACE:
            if (g_stack_top > 0) {
                g_stack_top--;            /* safe one-level back       */
                tui_dt_toast(1, "%s", "");
                g_toast[0] = 0;
            } else {
                goto done;                /* Esc at root leaves the TUI */
            }
            break;
        case TUI_KEY_F1:
            tui_dt_loading_begin(tui_tr("help", "帮助"));
            tui_dt_guard(tui_dt_help_page_run, NULL);
            tui_dt_loading_end(1, tui_tr("help closed", "帮助已关闭"), NULL);
            break;
        case TUI_KEY_F2:
            tui_lang_set(shell_lang_get() == SHELL_LANG_ZH ? SHELL_LANG_EN
                                                           : SHELL_LANG_ZH);
            break;
        case TUI_KEY_F5:
            if (g_dt_win) tui_window_refresh(g_dt_win);
            tui_dt_toast(1, tui_tr("refreshed", "已刷新"));
            break;
        case TUI_KEY_F10:
            goto done;
        case TUI_KEY_MOUSE:
            break;   /* mouse reports ignored by the desktop nav     */
        default: {
            /* quick-open: digits 1..9 activate the Nth child of the
             * current menu level (root quick-launch; the same semantic
             * as selecting with arrows + Enter).  Deterministic for
             * E2E drivers and a standard desktop accelerator. */
            if (k >= '1' && k <= '9') {
                int qidx = k - '1';
                if (qidx < cur->child_count) {
                    tui_dt_node_t *qc = cur->children[qidx];
                    if (qc) {
                        g_sel[g_stack_top] = qidx;
                        dt_render_menu(cur, qidx);
                        g_toast[0] = 0;
                        if (qc->kind == TUI_DT_MENU) {
                            if (qc->level > TUI_DT_DEPTH_MAX) {
                                tui_dt_toast(0, tui_tr("max menu depth %d",
                                                       "菜单最深 %d 层"),
                                             TUI_DT_DEPTH_MAX);
                            } else {
                                dt_push(qc);
                            }
                        } else {
                            dt_activate(qc);
                        }
                    }
                }
            }
            break;   /* unknown keys never crash the loop            */
        }
        }
    }
done:
    int rc = 0;
    if (g_dt_win) { tui_window_free(g_dt_win); g_dt_win = NULL; }
    tui_shutdown();
    screen_console_puts(tui_tr("tui: desktop closed, shell ok\n",
                               "tui\xef\xbc\x9a\xe6\xa1\x8c\xe9\x9d\xa2\xe5\xb7\xb2\xe9\x80\x80\xe5\x87\xba\xef\xbc\x8cshell \xe6\xad\xa3\xe5\xb8\xb8\n"));
    return rc;
}

/* ------------------------------------------------------------------ */
/* builtin application bootstrap (idempotent, registry-driven)         */
/* ------------------------------------------------------------------ */

/* app runners defined in tui_dt_apps.c (real pages, no stubs) */
int tui_dt_files_run(tui_dt_node_t *self);
int tui_dt_netconf_run(tui_dt_node_t *self);   /* WP-10-tui-native */
int tui_dt_proc_run(tui_dt_node_t *self);
int tui_dt_editor_run(tui_dt_node_t *self);
int tui_dt_terminal_run(tui_dt_node_t *self);
int tui_dt_help_page_run(tui_dt_node_t *self);
int tui_dt_cmdbridge_run(tui_dt_node_t *self);
int tui_dt_lang_run(tui_dt_node_t *self);
int tui_dt_update_run(tui_dt_node_t *self);
int tui_dt_disk_detail_run(tui_dt_node_t *self);
int tui_dt_disks_populate(tui_dt_node_t *self);

int tui_cn_hw_page(const char *en_name);   /* chinese/tui_cn_hw.c */
static int dt_hw_page_run(tui_dt_node_t *self) {
    const char *page = (const char *)self->userptr;
    /* WP-10-tui-native: direct page access follows the language too -
     * the Chinese twin renders the same real data in zh.  Returns -3
     * when the twin has no such page (falls back to English). */
    if (shell_lang_get() == SHELL_LANG_ZH) {
        int rc = tui_cn_hw_page(page);
        if (rc != -3) return rc;
    }
    return tui_hw_run_page(page);
}
static int dt_hw_app_run(tui_dt_node_t *self) { (void)self; return tui_hw_app(); }
static int dt_sysinfo_run(tui_dt_node_t *self)  { (void)self; return tui_sysinfo_app(); }
static int dt_net_run(tui_dt_node_t *self)      { (void)self; return tui_net_app(); }
static int dt_time_run(tui_dt_node_t *self)     { (void)self; return tui_time_main(); }
static int dt_ime_run(tui_dt_node_t *self)      { (void)self; tui_chinese_init(); return tui_cn_input_app(); }
static int dt_ext_run(tui_dt_node_t *self)      { (void)self; return tui_ext_page_main("ext"); }
static int dt_security_run(tui_dt_node_t *self) { (void)self; return tui_ext_page_main("sys.security"); }
static int dt_log_run(tui_dt_node_t *self)      { (void)self; return tui_ext_page_main("sys.log"); }

/* helper: add one node; return rc (toast on failure is done by caller) */
static int dt_add(const char *parent, const char *id,
                  const char *en, const char *zh,
                  const char *den, const char *dzh,
                  const char *icon, int kind,
                  int (*run)(tui_dt_node_t *), int (*populate)(tui_dt_node_t *),
                  const char *shell_cmd, int prio, void *userptr) {
    tui_dt_node_t n;
    memset(&n, 0, sizeof(n));
    strncpy(n.id, id, sizeof(n.id) - 1);
    strncpy(n.en, en, sizeof(n.en) - 1);
    strncpy(n.zh, zh, sizeof(n.zh) - 1);
    strncpy(n.desc_en, den, sizeof(n.desc_en) - 1);
    strncpy(n.desc_zh, dzh, sizeof(n.desc_zh) - 1);
    strncpy(n.icon, icon, sizeof(n.icon) - 1);
    n.kind = kind;
    n.run = run;
    n.populate = populate;
    n.shell_cmd = shell_cmd;
    n.priority = prio;
    n.userptr = userptr;
    strncpy(n.owner, "builtin", sizeof(n.owner) - 1);
    return tui_dt_node_add(parent, &n);
}

void tui_dt_bootstrap(void) {
    if (tui_dt_find("cmds")) return;   /* already bootstrapped */

    /* ---- Control Panel (3 branches go 4..6 deep: 6-level proof) ---- */
    dt_add("root", "panel", "Control Panel", "\xe6\x8e\xa7\xe5\x88\xb6\xe9\x9d\xa2\xe6\x9d\xbf",
           "network, hardware, storage, update, time, security, log",
           "\xe7\xbd\x91\xe7\xbb\x9c\xe3\x80\x81\xe7\xa1\xac\xe4\xbb\xb6\xe3\x80\x81\xe5\xad\x98\xe5\x82\xa8\xe3\x80\x81\xe6\x9b\xb4\xe6\x96\xb0\xe3\x80\x81\xe6\x97\xb6\xe9\x97\xb4\xe3\x80\x81\xe5\xae\x89\xe5\x85\xa8\xe3\x80\x81\xe6\x97\xa5\xe5\xbf\x97",
           "[C]", TUI_DT_MENU, NULL, NULL, NULL, 10, NULL);

    /* Hardware: the 12 real hardware pages of tui-hw, plus the
     * language-aware full app (WP-10-tui-qa: the Chinese twin runs
     * when shell.lang == zh; the 12 per-page entries below stay on
     * the English pages - direct page access has no twin). */
    dt_add("panel", "panel.hw", "Hardware", "\xe7\xa1\xac\xe4\xbb\xb6",
           "the 12 hardware domain pages (cpu..display)",
           "12 \xe4\xb8\xaa\xe7\xa1\xac\xe4\xbb\xb6\xe5\x9f\x9f\xe9\xa1\xb5\xe9\x9d\xa2",
           "[H]", TUI_DT_MENU, NULL, NULL, NULL, 10, NULL);
    dt_add("panel.hw", "panel.hw.app", "Overview (all pages)",
           "\xe6\x80\xbb\xe8\xa7\x88\xef\xbc\x88\xe5\x85\xa8\xe9\x83\xa8\xe9\xa1\xb5\xef\xbc\x89",
           "full hardware app, follows shell.lang",
           "\xe5\xae\x8c\xe6\x95\xb4\xe7\xa1\xac\xe4\xbb\xb6\xe5\xba\x94\xe7\x94\xa8\xef\xbc\x88\xe8\xb7\x9f\xe9\x9a\x8f shell.lang \xe8\xaf\xad\xe8\xa8\x80\xef\xbc\x89",
           "[A]", TUI_DT_APP, dt_hw_app_run, NULL, NULL, 5, NULL);
    static const char *hw_pages[] = {
        "CPU", "Memory", "Storage", "Network", "Sound", "USB",
        "PCI", "Input", "Camera", "Wireless", "Timers", "Display"
    };
    for (int i = 0; i < 12; i++) {
        char id[40];
        strncpy(id, "panel.hw.", sizeof(id) - 1);
        id[sizeof(id) - 1] = 0;
        /* lowercase page name as the id */
        int L = (int)strlen(id), k = 0;
        for (; hw_pages[i][k] && L + k < (int)sizeof(id) - 1; k++)
            id[L + k] = (char)(hw_pages[i][k] | 0x20);
        id[L + k] = 0;
        dt_add("panel.hw", id, hw_pages[i], hw_pages[i],
               "hardware page", "\xe7\xa1\xac\xe4\xbb\xb6\xe9\xa1\xb5",
               "   ", TUI_DT_PAGE, dt_hw_page_run, NULL, NULL, 10 + i,
               (void *)hw_pages[i]);
    }

    /* Storage: mkfs/fsck/mounts + a DYNAMIC per-device tree (level 6) */
    dt_add("panel", "panel.st", "Storage", "\xe5\xad\x98\xe5\x82\xa8",
           "disks, mkfs, fsck, mounts",
           "\xe7\xa3\x81\xe7\x9b\x98\xe3\x80\x81\xe6\xa0\xbc\xe5\xbc\x8f\xe5\x8c\x96\xe3\x80\x81\xe6\xa3\x80\xe6\x9f\xa5\xe3\x80\x81\xe6\x8c\x82\xe8\xbd\xbd",
           "[S]", TUI_DT_MENU, NULL, NULL, NULL, 20, NULL);
    dt_add("panel.st", "panel.st.disks", "Disks", "\xe7\xa3\x81\xe7\x9b\x98",
           "block devices (enumerated live)",
           "\xe5\x9d\x97\xe8\xae\xbe\xe5\xa4\x87\xef\xbc\x88\xe5\x8a\xa8\xe6\x80\x81\xe6\x9e\x9a\xe4\xb8\xbe\xef\xbc\x89",
           "[D]", TUI_DT_MENU, NULL, tui_dt_disks_populate, NULL, 10, NULL);
    dt_add("panel.st", "panel.st.mounts", "mounts", "mounts",
           "list VFS mounts (runs: mounts)", "\xe5\x88\x97\xe5\x87\xba\xe6\x8c\x82\xe8\xbd\xbd\xef\xbc\x88mounts\xef\xbc\x89",
           "   ", TUI_DT_SHELL, NULL, NULL, "mounts", 20, NULL);
    dt_add("panel.st", "panel.st.fsck", "fsck", "fsck",
           "filesystem check (runs: fsck)", "\xe6\x96\x87\xe4\xbb\xb6\xe7\xb3\xbb\xe7\xbb\x9f\xe6\xa3\x80\xe6\x9f\xa5\xef\xbc\x88" "fsck\xef\xbc\x89",
           "   ", TUI_DT_SHELL, NULL, NULL, "fsck", 30, NULL);

    /* WP-11-crash_recovery: the crash-isolation proof page. Opening it
     * and pressing C raises a REAL ring-0 page fault inside TUI code;
     * the tui_dt_guard() fence (wired to the exception dispatcher)
     * must longjmp back to this menu instead of a kernel panic. */
    dt_add("panel", "panel.crash", "Crash test", "\xe5\xb4\xa9\xe6\xba\x83\xe6\xb5\x8b\xe8\xaf\x95",
           "WP-11: verify the TUI crash guard",
           "WP-11\uff1a\u9a8c\u8bc1 TUI \u5d29\u6e83\u62a4\u680f",
           "[X]", TUI_DT_PAGE, tui_dt_crashtest_run, NULL, NULL, 45, NULL);

    /* Network: real shell commands through the bridge + wireless app */
    dt_add("panel", "panel.net", "Network", "\xe7\xbd\x91\xe7\xbb\x9c",
           "dhcp/ip/route/dns/firewall/tcpstats + wireless",
           "dhcp/ip/\xe8\xb7\xaf\xe7\x94\xb1/dns/\xe9\x98\xb2\xe7\x81\xab\xe5\xa2\x99 + \xe6\x97\xa0\xe7\xba\xbf",
           "[N]", TUI_DT_MENU, NULL, NULL, NULL, 30, NULL);
    /* WP-10-tui-native: the NATIVE network configuration page (static
     * IP / DHCP / DNS through the real net_* kernel API - no bridge).
     * The read-only bridge entries below stay for parity views. */
    dt_add("panel.net", "panel.net.conf", "Net config", "\xe7\xbd\x91\xe7\xbb\x9c\xe9\x85\x8d\xe7\xbd\xae",
           "IP/mask/gateway/DNS + DHCP (native)",
           "IP/\xe6\x8e\xa9\xe7\xa0\x81/\xe7\xbd\x91\xe5\x85\xb3/DNS + DHCP\xef\xbc\x88\xe5\x8e\x9f\xe7\x94\x9f\xef\xbc\x89",
           "[N]", TUI_DT_APP, tui_dt_netconf_run, NULL, NULL, 5, NULL);
    dt_add("panel.net", "panel.net.dhcp", "dhcp", "dhcp",
           "DHCP lease (runs: dhcp)", "\xe8\x8e\xb7\xe5\x8f\x96 DHCP\xef\xbc\x88" "dhcp\xef\xbc\x89",
           "   ", TUI_DT_SHELL, NULL, NULL, "dhcp", 10, NULL);
    dt_add("panel.net", "panel.net.ip", "ip", "ip",
           "IP configuration (runs: ip)", "IP \xe9\x85\x8d\xe7\xbd\xae\xef\xbc\x88ip\xef\xbc\x89",
           "   ", TUI_DT_SHELL, NULL, NULL, "ip", 20, NULL);
    dt_add("panel.net", "panel.net.route", "route", "route",
           "routing table (runs: route)", "\xe8\xb7\xaf\xe7\x94\xb1\xe8\xa1\xa8\xef\xbc\x88route\xef\xbc\x89",
           "   ", TUI_DT_SHELL, NULL, NULL, "route", 30, NULL);
    dt_add("panel.net", "panel.net.dns", "dns", "dns",
           "DNS resolver status (runs: dns)", "DNS \xe7\x8a\xb6\xe6\x80\x81\xef\xbc\x88" "dns\xef\xbc\x89",
           "   ", TUI_DT_SHELL, NULL, NULL, "dns", 40, NULL);
    dt_add("panel.net", "panel.net.fw", "firewall", "\xe9\x98\xb2\xe7\x81\xab\xe5\xa2\x99",
           "netfilter rules (runs: firewall)", "\xe9\x98\xb2\xe7\x81\xab\xe5\xa2\x99\xe8\xa7\x84\xe5\x88\x99\xef\xbc\x88" "firewall\xef\xbc\x89",
           "   ", TUI_DT_SHELL, NULL, NULL, "firewall", 50, NULL);
    dt_add("panel.net", "panel.net.tcpstats", "tcpstats", "tcpstats",
           "TCP/IP statistics (runs: tcpstats)", "TCP \xe7\xbb\x9f\xe8\xae\xa1\xef\xbc\x88tcpstats\xef\xbc\x89",
           "   ", TUI_DT_SHELL, NULL, NULL, "tcpstats", 60, NULL);
    dt_add("panel.net", "panel.net.wifi", "Wireless", "\xe6\x97\xa0\xe7\xba\xbf\xe7\xbd\x91\xe7\xbb\x9c",
           "WiFi scan/connect + Bluetooth", "WiFi \xe6\x89\xab\xe6\x8f\x8f/\xe8\xbf\x9e\xe6\x8e\xa5 + \xe8\x93\x9d\xe7\x89\x99",
           "[W]", TUI_DT_APP, dt_net_run, NULL, NULL, 70, NULL);

    /* Time & Language */
    dt_add("panel", "panel.time", "Time & Language", "\xe6\x97\xb6\xe9\x97\xb4\xe4\xb8\x8e\xe8\xaf\xad\xe8\xa8\x80",
           "clock/sources/RTC/timezone + language",
           "\xe6\x97\xb6\xe9\x92\x9f/\xe6\x97\xb6\xe6\xba\x90/RTC/\xe6\x97\xb6\xe5\x8c\xba + \xe8\xaf\xad\xe8\xa8\x80",
           "[T]", TUI_DT_MENU, NULL, NULL, NULL, 40, NULL);
    dt_add("panel.time", "panel.time.clock", "Clock", "\xe6\x97\xb6\xe9\x92\x9f",
           "time TUI (clock/set/tz)", "\xe6\x97\xb6\xe9\x97\xb4 TUI\xef\xbc\x88" "clock/set/tz\xef\xbc\x89",
           "   ", TUI_DT_APP, dt_time_run, NULL, NULL, 10, NULL);
    dt_add("panel.time", "panel.time.lang", "Language", "\xe8\xaf\xad\xe8\xa8\x80",
           "switch en/zh (syncs shell.lang)", "\xe5\x88\x87\xe6\x8d\xa2 en/zh\xef\xbc\x88\xe5\x90\x8c\xe6\xad\xa5 shell.lang\xef\xbc\x89",
           "   ", TUI_DT_PAGE, tui_dt_lang_run, NULL, NULL, 20, NULL);

    /* Update / Security / Log pages (real tui-sys pages) */
    dt_add("panel", "panel.update", "Update", "\xe6\x9b\xb4\xe6\x96\xb0",
           "checkupdate / A-B slots", "\xe6\xa3\x80\xe6\x9f\xa5\xe6\x9b\xb4\xe6\x96\xb0 / A-B \xe6\xa7\xbd",
           "[U]", TUI_DT_PAGE, tui_dt_update_run, NULL, NULL, 50, NULL);
    dt_add("panel", "panel.sec", "Security", "\xe5\xae\x89\xe5\x85\xa8",
           "TOCU/known_hosts/netfilter/conntrack", "TOCU/known_hosts/\xe9\x98\xb2\xe7\x81\xab\xe5\xa2\x99/\xe8\xbf\x9e\xe6\x8e\xa5\xe8\xb7\x9f\xe8\xb8\xaa",
           "[!]", TUI_DT_PAGE, dt_security_run, NULL, NULL, 60, NULL);
    dt_add("panel", "panel.log", "Log", "\xe6\x97\xa5\xe5\xbf\x97",
           "kernel log ring", "\xe5\x86\x85\xe6\xa0\xb8\xe6\x97\xa5\xe5\xbf\x97\xe7\x8e\xaf",
           "[L]", TUI_DT_PAGE, dt_log_run, NULL, NULL, 70, NULL);

    /* ---- top-level applications ---- */
    dt_add("root", "files", "Files", "\xe6\x96\x87\xe4\xbb\xb6",
           "file manager: browse/copy/move/rename/delete/mount",
           "\xe6\x96\x87\xe4\xbb\xb6\xe7\xae\xa1\xe7\x90\x86\xef\xbc\x9a\xe6\xb5\x8f\xe8\xa7\x88/\xe5\xa4\x8d\xe5\x88\xb6/\xe7\xa7\xbb\xe5\x8a\xa8/\xe9\x87\x8d\xe5\x91\xbd\xe5\x90\x8d/\xe5\x88\xa0\xe9\x99\xa4/\xe6\x8c\x82\xe8\xbd\xbd",
           "[F]", TUI_DT_APP, tui_dt_files_run, NULL, NULL, 5, NULL);
    dt_add("root", "procs", "Processes", "\xe8\xbf\x9b\xe7\xa8\x8b",
           "task manager: live process table, kill, resources",
           "\xe4\xbb\xbb\xe5\x8a\xa1\xe7\xae\xa1\xe7\x90\x86\xef\xbc\x9a\xe8\xbf\x9b\xe7\xa8\x8b/\xe7\xbb\x93\xe6\x9d\x9f/\xe8\xb5\x84\xe6\xba\x90",
           "[P]", TUI_DT_APP, tui_dt_proc_run, NULL, NULL, 8, NULL);
    dt_add("root", "editor", "Editor", "\xe7\xbc\x96\xe8\xbe\x91\xe5\x99\xa8",
           "full-screen nano editor on any file",
           "\xe5\x85\xa8\xe5\xb1\x8f nano \xe7\xbc\x96\xe8\xbe\x91\xe4\xbb\xbb\xe6\x84\x8f\xe6\x96\x87\xe4\xbb\xb6",
           "[E]", TUI_DT_APP, tui_dt_editor_run, NULL, NULL, 12, NULL);
    dt_add("root", "terminal", "Terminal", "\xe7\xbb\x88\xe7\xab\xaf",
           "ring-3 user shell (ush), syscall-only",
           "Ring 3 \xe7\x94\xa8\xe6\x88\xb7\xe6\x80\x81 shell\xef\xbc\x88ush\xef\xbc\x89",
           "[_]", TUI_DT_TERMINAL, tui_dt_terminal_run, NULL, NULL, 14, NULL);
    dt_add("root", "sysinfo", "System Info", "\xe7\xb3\xbb\xe7\xbb\x9f\xe4\xbf\xa1\xe6\x81\xaf",
           "overview/processes/logs/monitor/power", "\xe6\xa6\x82\xe8\xa7\x88/\xe8\xbf\x9b\xe7\xa8\x8b/\xe6\x97\xa5\xe5\xbf\x97/\xe7\x9b\x91\xe6\x8e\xa7/\xe7\x94\xb5\xe6\xba\x90",
           "[i]", TUI_DT_APP, dt_sysinfo_run, NULL, NULL, 20, NULL);
    dt_add("root", "ime", "Chinese Input", "\xe4\xb8\xad\xe6\x96\x87\xe8\xbe\x93\xe5\x85\xa5",
           "pinyin IME pad", "\xe6\x8b\xbc\xe9\x9f\xb3\xe8\xbe\x93\xe5\x85\xa5\xe6\x9d\xbf",
           "[\xe4\xb8\xad]", TUI_DT_APP, dt_ime_run, NULL, NULL, 30, NULL);
    dt_add("root", "cmds", "Command Bridge", "\xe5\x91\xbd\xe4\xbb\xa4\xe6\xa1\xa5",
           "run any shell command with live output",
           "\xe8\xbf\x90\xe8\xa1\x8c\xe4\xbb\xbb\xe6\x84\x8f Shell \xe5\x91\xbd\xe4\xbb\xa4\xef\xbc\x88\xe5\xae\x9e\xe6\x97\xb6\xe8\xbe\x93\xe5\x87\xba\xef\xbc\x89",
           "[>]", TUI_DT_PAGE, tui_dt_cmdbridge_run, NULL, NULL, 40, NULL);
    dt_add("root", "ext", "Extensions", "\xe6\x89\xa9\xe5\xb1\x95",
           "TUI L1 extension lab (7 interface sets)",
           "TUI L1 \xe6\x89\xa9\xe5\xb1\x95\xe5\xae\x9e\xe9\xaa\x8c\xe5\xae\xa4\xef\xbc\x88" "7 \xe7\xb1\xbb\xe6\x8e\xa5\xe5\x8f\xa3\xef\xbc\x89",
           "[x]", TUI_DT_PAGE, dt_ext_run, NULL, NULL, 50, NULL);
}
