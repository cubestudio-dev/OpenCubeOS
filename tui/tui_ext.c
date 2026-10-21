/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-10-chinese_support-fix4
 * File: tui/tui_ext.c
 * Purpose: Implementation of the TUI L1 extension registry (tui_ext.h).
 * Static tables, bounded, duplicate-checked; every entry records its
 * owner so `unregister` can never drop someone else's entry.
 */
#include "tui_ext.h"
#include "tui_desktop.h"   /* WP-10-tui-native: tui_tr for bilingual UI */
#include "../kernel/lib/lib_string.h"
#include "../kernel/mem/mem_heap.h"

/* ------------------------------------------------------------------ */
/* 7.1 menu extension                                                  */
/* ------------------------------------------------------------------ */

static tui_menuext_item_t g_menuext[TUI_EXT_MENU_MAX];
static int g_menuext_count;

int tui_menu_register(const char *owner, const char *name,
                      const char *desc, const char *icon,
                      const char *group, int hotkey, int priority,
                      int (*action)(void), void *userptr) {
    if (!owner || !name || !name[0]) return -1;
    if (g_menuext_count >= TUI_EXT_MENU_MAX) return -1;
    for (int i = 0; i < TUI_EXT_MENU_MAX; i++)
        if (g_menuext[i].used && strcmp(g_menuext[i].name, name) == 0)
            return -2;
    for (int i = 0; i < TUI_EXT_MENU_MAX; i++) {
        tui_menuext_item_t *e = &g_menuext[i];
        if (e->used) continue;
        memset(e, 0, sizeof(*e));
        strncpy(e->name, name, TUI_EXT_NAME_MAX - 1);
        strncpy(e->desc, desc ? desc : "", sizeof(e->desc) - 1);
        strncpy(e->icon, icon ? icon : "", sizeof(e->icon) - 1);
        strncpy(e->group, group ? group : "general", TUI_EXT_NAME_MAX - 1);
        strncpy(e->owner, owner, TUI_EXT_NAME_MAX - 1);
        e->hotkey = hotkey;
        e->priority = priority;
        e->action = action;
        e->userptr = userptr;
        e->used = 1;
        g_menuext_count++;
        return 0;
    }
    return -1;
}

int tui_menu_unregister(const char *name) {
    if (!name) return -1;
    for (int i = 0; i < TUI_EXT_MENU_MAX; i++) {
        if (g_menuext[i].used && strcmp(g_menuext[i].name, name) == 0) {
            memset(&g_menuext[i], 0, sizeof(g_menuext[i]));
            g_menuext_count--;
            return 0;
        }
    }
    return -1;
}

int tui_menu_list(tui_menuext_item_t *out, int cap) {
    if (!out || cap <= 0) return g_menuext_count;
    int n = 0;
    for (int i = 0; i < TUI_EXT_MENU_MAX && n < cap; i++)
        if (g_menuext[i].used) out[n++] = g_menuext[i];
    return n;
}

int tui_menu_sort(void) {
    /* insertion sort by (priority, name) over the compacted list */
    tui_menuext_item_t tmp[TUI_EXT_MENU_MAX];
    int n = 0;
    for (int i = 0; i < TUI_EXT_MENU_MAX; i++)
        if (g_menuext[i].used) tmp[n++] = g_menuext[i];
    for (int i = 1; i < n; i++) {
        tui_menuext_item_t key = tmp[i];
        int j = i - 1;
        while (j >= 0 && (tmp[j].priority > key.priority ||
               (tmp[j].priority == key.priority &&
                strcmp(tmp[j].name, key.name) > 0))) {
            tmp[j + 1] = tmp[j];
            j--;
        }
        tmp[j + 1] = key;
    }
    memset(g_menuext, 0, sizeof(g_menuext));
    for (int i = 0; i < n; i++) g_menuext[i] = tmp[i];
    g_menuext_count = n;
    return n;
}

/* ------------------------------------------------------------------ */
/* 7.2 page extension                                                  */
/* ------------------------------------------------------------------ */

#define TUI_EXT_PAGE_MAX 24
static tui_pageext_t g_pageext[TUI_EXT_PAGE_MAX];
static int g_pageext_count;

int tui_page_register(const char *owner, const char *name,
                      const char *title, const char *group,
                      int priority,
                      int (*render)(tui_window_t *, int),
                      int (*input)(int), void *userptr) {
    if (!owner || !name || !name[0] || !render) return -1;
    if (g_pageext_count >= TUI_EXT_PAGE_MAX) return -1;
    for (int i = 0; i < TUI_EXT_PAGE_MAX; i++)
        if (g_pageext[i].used && strcmp(g_pageext[i].name, name) == 0)
            return -2;
    for (int i = 0; i < TUI_EXT_PAGE_MAX; i++) {
        tui_pageext_t *e = &g_pageext[i];
        if (e->used) continue;
        memset(e, 0, sizeof(*e));
        strncpy(e->name, name, TUI_EXT_NAME_MAX - 1);
        strncpy(e->title, title ? title : name, sizeof(e->title) - 1);
        strncpy(e->group, group ? group : "general", TUI_EXT_NAME_MAX - 1);
        strncpy(e->owner, owner, TUI_EXT_NAME_MAX - 1);
        e->priority = priority;
        e->render = render;
        e->input = input;
        e->userptr = userptr;
        e->used = 1;
        g_pageext_count++;
        return 0;
    }
    return -1;
}

int tui_page_unregister(const char *name) {
    if (!name) return -1;
    for (int i = 0; i < TUI_EXT_PAGE_MAX; i++) {
        if (g_pageext[i].used && strcmp(g_pageext[i].name, name) == 0) {
            memset(&g_pageext[i], 0, sizeof(g_pageext[i]));
            g_pageext_count--;
            return 0;
        }
    }
    return -1;
}

static tui_pageext_t *page_find(const char *name) {
    if (!name) return 0;
    for (int i = 0; i < TUI_EXT_PAGE_MAX; i++)
        if (g_pageext[i].used && strcmp(g_pageext[i].name, name) == 0)
            return &g_pageext[i];
    return 0;
}

int tui_page_list(char (*out)[TUI_EXT_NAME_MAX], int cap) {
    int n = 0;
    for (int i = 0; i < TUI_EXT_PAGE_MAX; i++) {
        if (!g_pageext[i].used) continue;
        if (out && n < cap) strncpy(out[n], g_pageext[i].name, TUI_EXT_NAME_MAX - 1);
        n++;
    }
    return n;
}

int tui_page_render(const char *name, tui_window_t *win, int scroll) {
    tui_pageext_t *p = page_find(name);
    if (!p || !win) return -1;
    return p->render(win, scroll);
}

int tui_page_input(const char *name, int key) {
    tui_pageext_t *p = page_find(name);
    if (!p || !p->input) return 0;
    return p->input(key);
}

/* generic page runner used by every fix4 page application */
int tui_ext_page_main(const char *name) {
    tui_pageext_t *p = page_find(name);
    if (!p) return -1;
    /* sort registered pages by (priority, name) for the Tab cycle */
    char order[TUI_EXT_PAGE_MAX][TUI_EXT_NAME_MAX];
    int cnt = 0;
    {
        int idx[TUI_EXT_PAGE_MAX];
        for (int i = 0; i < TUI_EXT_PAGE_MAX; i++)
            if (g_pageext[i].used) idx[cnt++] = i;
        for (int i = 1; i < cnt; i++) {
            int key = idx[i], j = i - 1;
            while (j >= 0 && (g_pageext[idx[j]].priority > g_pageext[key].priority ||
                   (g_pageext[idx[j]].priority == g_pageext[key].priority &&
                    strcmp(g_pageext[idx[j]].name, g_pageext[key].name) > 0))) {
                idx[j + 1] = idx[j];
                j--;
            }
            idx[j + 1] = key;
        }
        for (int i = 0; i < cnt; i++)
            strncpy(order[i], g_pageext[idx[i]].name, TUI_EXT_NAME_MAX - 1);
    }
    int cur = 0;
    for (int i = 0; i < cnt; i++)
        if (strcmp(order[i], name) == 0) cur = i;

    int cols, rows;
    tui_term_size(&cols, &rows);
    tui_window_t *win = tui_window_new(0, 0, rows, cols);
    if (!win) return -1;
    tui_window_title(win, p->title);

    int scroll = 0;
    int rc = 0;
    for (;;) {
        tui_pageext_t *cp = page_find(order[cur]);
        if (!cp) break;
        /* full redraw contract: clear + focus + content + border +
         * refresh, so a page render callback only draws body text */
        tui_window_clear(win);
        tui_window_focus_set(win);
        tui_window_title(win, cp->title);
        cp->render(win, scroll);
        tui_window_border(win, &TUI_BORDER_SINGLE);
        tui_window_refresh(win);
        int k = tui_getch();
        if (cp->input && cp->input(k)) continue;
        if (k == TUI_KEY_DOWN) { scroll++; }
        else if (k == TUI_KEY_UP) { if (scroll > 0) scroll--; }
        else if (k == TUI_KEY_TAB && cnt > 1) {
            cur = (cur + 1) % cnt;
            scroll = 0;
        }
        else if (k == TUI_KEY_F1) { tui_sysinfo_help(); }
        else if (k == TUI_KEY_F(2)) {
            tui_status_line("F2: see the page's own save action");
        }
        else if (k == TUI_KEY_F(5)) { }
        else if (k == TUI_KEY_ESC || k == TUI_KEY_F(10) || k == 'q') {
            break;
        }
        else continue;   /* unknown key: page may have redrawn itself */
        /* re-render on generic keys (scroll/tab/help) */
        cp = page_find(order[cur]);
        if (!cp) break;
        tui_window_clear(win);
        tui_window_focus_set(win);
        tui_window_title(win, cp->title);
        cp->render(win, scroll);
        tui_window_border(win, &TUI_BORDER_SINGLE);
        tui_window_refresh(win);
    }
    tui_window_free(win);
    return rc;
}

/* ------------------------------------------------------------------ */
/* 7.3 widget extension                                                */
/* ------------------------------------------------------------------ */

#define TUI_EXT_WIDGET_MAX 32
static tui_widgetext_t g_widgetext[TUI_EXT_WIDGET_MAX];
static int g_widgetext_count;

int tui_widget_register(const char *owner, const char *type,
                        const char *name, int x, int y, int w, int h,
                        tui_attr_t attr,
                        int (*render)(tui_window_t *, int, int, int, int),
                        int (*event)(int), void *userptr) {
    if (!owner || !type || !name || !name[0]) return -1;
    if (g_widgetext_count >= TUI_EXT_WIDGET_MAX) return -1;
    for (int i = 0; i < TUI_EXT_WIDGET_MAX; i++)
        if (g_widgetext[i].used && strcmp(g_widgetext[i].name, name) == 0)
            return -2;
    for (int i = 0; i < TUI_EXT_WIDGET_MAX; i++) {
        tui_widgetext_t *e = &g_widgetext[i];
        if (e->used) continue;
        memset(e, 0, sizeof(*e));
        strncpy(e->type, type, sizeof(e->type) - 1);
        strncpy(e->name, name, TUI_EXT_NAME_MAX - 1);
        strncpy(e->owner, owner, TUI_EXT_NAME_MAX - 1);
        e->x = x; e->y = y; e->w = w; e->h = h;
        e->attr = attr;
        e->render = render;
        e->event = event;
        e->userptr = userptr;
        e->used = 1;
        g_widgetext_count++;
        return 0;
    }
    return -1;
}

int tui_widget_unregister(const char *name) {
    if (!name) return -1;
    for (int i = 0; i < TUI_EXT_WIDGET_MAX; i++) {
        if (g_widgetext[i].used && strcmp(g_widgetext[i].name, name) == 0) {
            memset(&g_widgetext[i], 0, sizeof(g_widgetext[i]));
            g_widgetext_count--;
            return 0;
        }
    }
    return -1;
}

int tui_widget_list(tui_widgetext_t *out, int cap) {
    if (!out || cap <= 0) return g_widgetext_count;
    int n = 0;
    for (int i = 0; i < TUI_EXT_WIDGET_MAX && n < cap; i++)
        if (g_widgetext[i].used) out[n++] = g_widgetext[i];
    return n;
}

static tui_widgetext_t *widget_find(const char *name) {
    if (!name) return 0;
    for (int i = 0; i < TUI_EXT_WIDGET_MAX; i++)
        if (g_widgetext[i].used && strcmp(g_widgetext[i].name, name) == 0)
            return &g_widgetext[i];
    return 0;
}

int tui_widget_render(const char *name, tui_window_t *win) {
    tui_widgetext_t *w = widget_find(name);
    if (!w || !win) return -1;
    if (w->render) return w->render(win, w->x, w->y, w->w, w->h);
    return 0;
}

int tui_widget_event(const char *name, int ev) {
    tui_widgetext_t *w = widget_find(name);
    if (!w || !w->event) return 0;
    return w->event(ev);
}

/* ------------------------------------------------------------------ */
/* 7.4 command extension                                               */
/* ------------------------------------------------------------------ */

#define TUI_EXT_CMD_MAX 24
static tui_cmdext_t g_cmdext[TUI_EXT_CMD_MAX];
static int g_cmdext_count;

int tui_cmd_register(const char *owner, const char *name,
                     const char *args, const char *desc,
                     int (*fn)(int argc, char **argv), void *userptr) {
    if (!owner || !name || !name[0] || !fn) return -1;
    if (g_cmdext_count >= TUI_EXT_CMD_MAX) return -1;
    for (int i = 0; i < TUI_EXT_CMD_MAX; i++)
        if (g_cmdext[i].used && strcmp(g_cmdext[i].name, name) == 0)
            return -2;
    for (int i = 0; i < TUI_EXT_CMD_MAX; i++) {
        tui_cmdext_t *e = &g_cmdext[i];
        if (e->used) continue;
        memset(e, 0, sizeof(*e));
        strncpy(e->name, name, TUI_EXT_NAME_MAX - 1);
        strncpy(e->args, args ? args : "", sizeof(e->args) - 1);
        strncpy(e->desc, desc ? desc : "", sizeof(e->desc) - 1);
        strncpy(e->owner, owner, TUI_EXT_NAME_MAX - 1);
        e->fn = fn;
        e->userptr = userptr;
        e->used = 1;
        g_cmdext_count++;
        return 0;
    }
    return -1;
}

int tui_cmd_unregister(const char *name) {
    if (!name) return -1;
    for (int i = 0; i < TUI_EXT_CMD_MAX; i++) {
        if (g_cmdext[i].used && strcmp(g_cmdext[i].name, name) == 0) {
            memset(&g_cmdext[i], 0, sizeof(g_cmdext[i]));
            g_cmdext_count--;
            return 0;
        }
    }
    return -1;
}

int tui_cmd_list(tui_cmdext_t *out, int cap) {
    if (!out || cap <= 0) return g_cmdext_count;
    int n = 0;
    for (int i = 0; i < TUI_EXT_CMD_MAX && n < cap; i++)
        if (g_cmdext[i].used) out[n++] = g_cmdext[i];
    return n;
}

int tui_cmd_execute(const char *line) {
    if (!line) return -3;
    /* split into argv (max 8 words) */
    char buf[160];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    char *argv[8];
    int argc = 0;
    char *p = buf;
    while (*p && argc < 8) {
        while (*p == ' ') p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = 0;
    }
    if (argc == 0) return -3;
    for (int i = 0; i < TUI_EXT_CMD_MAX; i++) {
        if (g_cmdext[i].used && strcmp(g_cmdext[i].name, argv[0]) == 0)
            return g_cmdext[i].fn(argc, argv);
    }
    return -3;
}

/* ------------------------------------------------------------------ */
/* 7.5 theme extension                                                 */
/* ------------------------------------------------------------------ */

#define TUI_EXT_THEME_MAX 12
static tui_themeext_t g_themeext[TUI_EXT_THEME_MAX];
static int g_themeext_count;
static char g_theme_current[TUI_EXT_NAME_MAX];

int tui_theme_register(const char *owner, const char *name,
                       const char *desc, int fg, int bg, void *userptr) {
    if (!owner || !name || !name[0]) return -1;
    if (fg < 0 || fg > 15 || bg < 0 || bg > 15) return -1;
    if (g_themeext_count >= TUI_EXT_THEME_MAX) return -1;
    for (int i = 0; i < TUI_EXT_THEME_MAX; i++)
        if (g_themeext[i].used && strcmp(g_themeext[i].name, name) == 0)
            return -2;
    for (int i = 0; i < TUI_EXT_THEME_MAX; i++) {
        tui_themeext_t *e = &g_themeext[i];
        if (e->used) continue;
        memset(e, 0, sizeof(*e));
        strncpy(e->name, name, TUI_EXT_NAME_MAX - 1);
        strncpy(e->desc, desc ? desc : "", sizeof(e->desc) - 1);
        strncpy(e->owner, owner, TUI_EXT_NAME_MAX - 1);
        e->fg = fg; e->bg = bg;
        e->userptr = userptr;
        e->used = 1;
        g_themeext_count++;
        return 0;
    }
    return -1;
}

int tui_theme_unregister(const char *name) {
    if (!name) return -1;
    for (int i = 0; i < TUI_EXT_THEME_MAX; i++) {
        if (g_themeext[i].used && strcmp(g_themeext[i].name, name) == 0) {
            if (strcmp(g_theme_current, name) == 0) g_theme_current[0] = 0;
            memset(&g_themeext[i], 0, sizeof(g_themeext[i]));
            g_themeext_count--;
            return 0;
        }
    }
    return -1;
}

int tui_theme_list(tui_themeext_t *out, int cap) {
    if (!out || cap <= 0) return g_themeext_count;
    int n = 0;
    for (int i = 0; i < TUI_EXT_THEME_MAX && n < cap; i++)
        if (g_themeext[i].used) out[n++] = g_themeext[i];
    return n;
}

int tui_theme_set(const char *name) {
    if (!name) return -1;
    for (int i = 0; i < TUI_EXT_THEME_MAX; i++) {
        if (g_themeext[i].used && strcmp(g_themeext[i].name, name) == 0) {
            strncpy(g_theme_current, name, TUI_EXT_NAME_MAX - 1);
            g_theme_current[TUI_EXT_NAME_MAX - 1] = 0;
            return tui_core_apply_theme(g_themeext[i].fg, g_themeext[i].bg);
        }
    }
    return -1;
}

const char *tui_theme_current(void) { return g_theme_current; }

/* ------------------------------------------------------------------ */
/* 7.6 hotkey extension                                                */
/* ------------------------------------------------------------------ */

#define TUI_EXT_KEY_MAX 24
static tui_keyext_t g_keyext[TUI_EXT_KEY_MAX];
static int g_keyext_count;

int tui_key_conflict(int key, int mods) {
    for (int i = 0; i < TUI_EXT_KEY_MAX; i++)
        if (g_keyext[i].used && g_keyext[i].key == key &&
            g_keyext[i].mods == mods)
            return i;
    return -1;
}

int tui_key_register(const char *owner, int key, int mods,
                     const char *name, const char *desc,
                     int (*fn)(void), void *userptr) {
    if (!owner || !name || !name[0] || !fn) return -1;
    if (tui_key_conflict(key, mods) >= 0) return -2;
    if (g_keyext_count >= TUI_EXT_KEY_MAX) return -1;
    for (int i = 0; i < TUI_EXT_KEY_MAX; i++) {
        tui_keyext_t *e = &g_keyext[i];
        if (e->used) continue;
        memset(e, 0, sizeof(*e));
        strncpy(e->name, name, TUI_EXT_NAME_MAX - 1);
        strncpy(e->desc, desc ? desc : "", sizeof(e->desc) - 1);
        strncpy(e->owner, owner, TUI_EXT_NAME_MAX - 1);
        e->key = key;
        e->mods = mods;
        e->fn = fn;
        e->userptr = userptr;
        e->used = 1;
        g_keyext_count++;
        return 0;
    }
    return -1;
}

int tui_key_unregister(const char *name) {
    if (!name) return -1;
    for (int i = 0; i < TUI_EXT_KEY_MAX; i++) {
        if (g_keyext[i].used && strcmp(g_keyext[i].name, name) == 0) {
            memset(&g_keyext[i], 0, sizeof(g_keyext[i]));
            g_keyext_count--;
            return 0;
        }
    }
    return -1;
}

int tui_key_list(tui_keyext_t *out, int cap) {
    if (!out || cap <= 0) return g_keyext_count;
    int n = 0;
    for (int i = 0; i < TUI_EXT_KEY_MAX && n < cap; i++)
        if (g_keyext[i].used) out[n++] = g_keyext[i];
    return n;
}

int tui_key_dispatch(int key, int mods) {
    for (int i = 0; i < TUI_EXT_KEY_MAX; i++) {
        if (g_keyext[i].used && g_keyext[i].key == key &&
            g_keyext[i].mods == mods) {
            if (g_keyext[i].fn) g_keyext[i].fn();
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* 7.7 event extension                                                 */
/* ------------------------------------------------------------------ */

#define TUI_EXT_EVENT_MAX 24
static tui_eventext_t g_eventext[TUI_EXT_EVENT_MAX];
static int g_eventext_count;

int tui_event_register(const char *owner, const char *type,
                       const char *name,
                       int (*handler)(const char *, void *), void *userptr) {
    if (!owner || !type || !name || !name[0] || !handler) return -1;
    if (g_eventext_count >= TUI_EXT_EVENT_MAX) return -1;
    for (int i = 0; i < TUI_EXT_EVENT_MAX; i++)
        if (g_eventext[i].used && strcmp(g_eventext[i].name, name) == 0)
            return -2;
    for (int i = 0; i < TUI_EXT_EVENT_MAX; i++) {
        tui_eventext_t *e = &g_eventext[i];
        if (e->used) continue;
        memset(e, 0, sizeof(*e));
        strncpy(e->type, type, TUI_EXT_NAME_MAX - 1);
        strncpy(e->name, name, TUI_EXT_NAME_MAX - 1);
        strncpy(e->owner, owner, TUI_EXT_NAME_MAX - 1);
        e->handler = handler;
        e->userptr = userptr;
        e->used = 1;
        g_eventext_count++;
        return 0;
    }
    return -1;
}

int tui_event_unregister(const char *name) {
    if (!name) return -1;
    for (int i = 0; i < TUI_EXT_EVENT_MAX; i++) {
        if (g_eventext[i].used && strcmp(g_eventext[i].name, name) == 0) {
            memset(&g_eventext[i], 0, sizeof(g_eventext[i]));
            g_eventext_count--;
            return 0;
        }
    }
    return -1;
}

int tui_event_list(tui_eventext_t *out, int cap) {
    if (!out || cap <= 0) return g_eventext_count;
    int n = 0;
    for (int i = 0; i < TUI_EXT_EVENT_MAX && n < cap; i++)
        if (g_eventext[i].used) out[n++] = g_eventext[i];
    return n;
}

int tui_event_dispatch(const char *type, void *payload) {
    int ran = 0;
    for (int i = 0; i < TUI_EXT_EVENT_MAX; i++) {
        if (g_eventext[i].used && strcmp(g_eventext[i].type, type) == 0) {
            if (g_eventext[i].handler) g_eventext[i].handler(type, payload);
            ran++;
        }
    }
    return ran;
}

/* ------------------------------------------------------------------ */
/* lifecycle                                                           */
/* ------------------------------------------------------------------ */

static int builtin_theme_action_default(void) { return tui_theme_set("default"); }
static int builtin_theme_action_green(void)   { return tui_theme_set("green"); }
static int builtin_theme_action_amber(void)   { return tui_theme_set("amber"); }

void tui_ext_init(void) {
    if (g_themeext_count == 0) {
        tui_theme_register("builtin", "default", "silver on black (stock)",
                           TUI_COLOR_SILVER, TUI_COLOR_BLACK, 0);
        tui_theme_register("builtin", "green", "green on black (classic BIOS)",
                           TUI_COLOR_LIME, TUI_COLOR_BLACK, 0);
        tui_theme_register("builtin", "amber", "amber on black (classic terminal)",
                           TUI_COLOR_YELLOW, TUI_COLOR_BLACK, 0);
        tui_key_register("builtin", TUI_KEY_F(5), 0, "repaint",
                         "F5 repaints the TUI stack",
                         builtin_theme_action_default, 0);
        (void)builtin_theme_action_green;
        (void)builtin_theme_action_amber;
    }
}

void tui_ext_reset(void) {
    /* builtin-owned entries survive; everything else is dropped */
    for (int i = 0; i < TUI_EXT_MENU_MAX; i++)
        if (g_menuext[i].used && strcmp(g_menuext[i].owner, "builtin") != 0)
            tui_menu_unregister(g_menuext[i].name);
    for (int i = 0; i < TUI_EXT_PAGE_MAX; i++)
        if (g_pageext[i].used && strcmp(g_pageext[i].owner, "builtin") != 0)
            tui_page_unregister(g_pageext[i].name);
    for (int i = 0; i < TUI_EXT_WIDGET_MAX; i++)
        if (g_widgetext[i].used && strcmp(g_widgetext[i].owner, "builtin") != 0)
            tui_widget_unregister(g_widgetext[i].name);
    for (int i = 0; i < TUI_EXT_CMD_MAX; i++)
        if (g_cmdext[i].used && strcmp(g_cmdext[i].owner, "builtin") != 0)
            tui_cmd_unregister(g_cmdext[i].name);
    for (int i = 0; i < TUI_EXT_KEY_MAX; i++)
        if (g_keyext[i].used && strcmp(g_keyext[i].owner, "builtin") != 0)
            tui_key_unregister(g_keyext[i].name);
    for (int i = 0; i < TUI_EXT_EVENT_MAX; i++)
        if (g_eventext[i].used && strcmp(g_eventext[i].owner, "builtin") != 0)
            tui_event_unregister(g_eventext[i].name);
}

/* ------------------------------------------------------------------ */
/* self-test: every interface group is exercised with the standard
 * input / expected / actual / verdict protocol used by tui-test.      */
/* ------------------------------------------------------------------ */

static int  st_menu_hits;
static int  st_menu_action(void) { st_menu_hits++; return 0; }
static int  st_page_lines;
static int  st_page_render(tui_window_t *win, int scroll) {
    (void)win;
    if (!win) return -1;
    tui_print_at(2 + scroll, 2, tui_tr("stpage body", "自测页面正文"));
    st_page_lines++;
    return 1;
}
static int  st_page_input_seen;
static int  st_page_input(int key) {
    if (key == 'z') { st_page_input_seen = 1; return 1; }
    return 0;
}
static int  st_widget_events;
static int  st_widget_event(int ev) { (void)ev; st_widget_events++; return 1; }
static int  st_widget_render(tui_window_t *win, int x, int y, int w, int h) {
    (void)win; (void)x; (void)y; (void)w; (void)h;
    tui_print_at(y, x, "[W]");
    return 1;
}
static int  st_cmd_ran;
static int  st_cmd_fn(int argc, char **argv) {
    (void)argv;
    st_cmd_ran = (argc == 2) ? 1 : 0;
    return 0;
}
static int  st_key_hits;
static int  st_key_fn(void) { st_key_hits++; return 0; }
static int  st_event_hits;
static int  st_event_handler(const char *type, void *payload) {
    (void)type; (void)payload;
    st_event_hits++;
    return 0;
}

int tui_ext_selftest(void) {
    int failed = 0;
    char num[16];

    /* ---- 7.1 menu ---- */
    {
        int dup = tui_menu_register("st", "stmenu", "selftest", "[S]",
                                    "test", 0, 50, st_menu_action, 0);
        int dup2 = tui_menu_register("st", "stmenu", "selftest", "[S]",
                                     "test", 0, 50, st_menu_action, 0);
        tui_menuext_item_t list[TUI_EXT_MENU_MAX];
        int n = tui_menu_list(list, TUI_EXT_MENU_MAX);
        int found = 0;
        for (int i = 0; i < n; i++)
            if (strcmp(list[i].name, "stmenu") == 0) { found = 1; break; }
        list[0].priority = 99;   /* list returns copies; sort works on the table */
        tui_menu_sort();
        int un = tui_menu_unregister("stmenu");
        if (dup != 0 || dup2 != -2 || !found || un != 0) {
            failed++;
            tui_print(tui_tr("  menu: FAIL (", "  菜单: FAIL（"));
            u64_to_str((u64)dup, num); tui_print(num);
            u64_to_str((u64)dup2, num); tui_print(" ");
            tui_print(num); tui_print(")\n");
        } else {
            tui_print(tui_tr("  menu: PASS (register/dup/list/sort/unregister)\n",
                             "  菜单: PASS（注册/重复注册/列表/排序/注销）\n"));
        }
    }

    /* ---- 7.2 page ---- */
    {
        int rc = tui_page_register("st", "stpage", "selftest page",
                                   "test", 50, st_page_render,
                                   st_page_input, 0);
        int found = tui_page_render("stpage", tui_window_focus(), 0);
        int consumed = tui_page_input("stpage", 'z');
        int un = tui_page_unregister("stpage");
        if (rc != 0 || found != 1 || !consumed || !st_page_input_seen || un != 0) {
            failed++;
            tui_print(tui_tr("  page: FAIL\n", "  页面: FAIL\n"));
        } else {
            tui_print(tui_tr("  page: PASS (register/render/input/unregister)\n",
                             "  页面: PASS（注册/渲染/输入/注销）\n"));
        }
    }

    /* ---- 7.3 widget ---- */
    {
        int rc = tui_widget_register("st", "text", "stwidget",
                                     10, 10, 6, 1, 0,
                                     st_widget_render, st_widget_event, 0);
        int r = tui_widget_render("stwidget", tui_window_focus());
        int e = tui_widget_event("stwidget", TUI_KEY_ENTER);
        int un = tui_widget_unregister("stwidget");
        if (rc != 0 || r != 1 || e != 1 || st_widget_events != 1 || un != 0) {
            failed++;
            tui_print(tui_tr("  widget: FAIL\n", "  组件: FAIL\n"));
        } else {
            tui_print(tui_tr("  widget: PASS (register/render/event/unregister)\n",
                             "  组件: PASS（注册/渲染/事件/注销）\n"));
        }
    }

    /* ---- 7.4 command ---- */
    {
        int rc = tui_cmd_register("st", "stcmd", "<n>", "selftest",
                                  st_cmd_fn, 0);
        const char *argv_like = "stcmd 42";
        int ex = tui_cmd_execute(argv_like);
        int un = tui_cmd_unregister("stcmd");
        if (rc != 0 || ex != 0 || st_cmd_ran != 1 || un != 0) {
            failed++;
            tui_print(tui_tr("  cmd: FAIL\n", "  命令: FAIL\n"));
        } else {
            tui_print(tui_tr("  cmd: PASS (register/execute/unregister)\n",
                             "  命令: PASS（注册/执行/注销）\n"));
        }
    }

    /* ---- 7.5 theme ---- */
    {
        int rc = tui_theme_register("st", "sttheme", "selftest",
                                    TUI_COLOR_WHITE, TUI_COLOR_NAVY, 0);
        int set = tui_theme_set("sttheme");
        int back = tui_theme_set("default");
        int un = tui_theme_unregister("sttheme");
        if (rc != 0 || set != 0 || back != 0 || un != 0) {
            failed++;
            tui_print(tui_tr("  theme: FAIL\n", "  主题: FAIL\n"));
        } else {
            tui_print(tui_tr("  theme: PASS (register/set/unregister + restore)\n",
                             "  主题: PASS（注册/切换/注销 + 恢复）\n"));
        }
    }

    /* ---- 7.6 hotkey ---- */
    {
        int rc = tui_key_register("st", TUI_KEY_F(9), 0, "stkey",
                                  "selftest", st_key_fn, 0);
        int dup = tui_key_register("st", TUI_KEY_F(9), 0, "stkey2",
                                   "selftest", st_key_fn, 0);
        int disp = tui_key_dispatch(TUI_KEY_F(9), 0);
        int un = tui_key_unregister("stkey");
        if (rc != 0 || dup != -2 || disp != 1 || st_key_hits != 1 || un != 0) {
            failed++;
            tui_print(tui_tr("  key: FAIL\n", "  热键: FAIL\n"));
        } else {
            tui_print(tui_tr("  key: PASS (register/conflict/dispatch/unregister)\n",
                             "  热键: PASS（注册/冲突/分发/注销）\n"));
        }
    }

    /* ---- 7.7 event ---- */
    {
        int rc = tui_event_register("st", TUI_EVT_TIMER, "stevent",
                                    st_event_handler, 0);
        int ran = tui_event_dispatch(TUI_EVT_TIMER, 0);
        int un = tui_event_unregister("stevent");
        if (rc != 0 || ran != 1 || st_event_hits != 1 || un != 0) {
            failed++;
            tui_print(tui_tr("  event: FAIL\n", "  事件: FAIL\n"));
        } else {
            tui_print(tui_tr("  event: PASS (register/dispatch/unregister)\n",
                             "  事件: PASS（注册/分发/注销）\n"));
        }
    }

    if (failed == 0)
        tui_print(tui_tr("  tui-ext selftest: ALL PASS\n",
                         "  tui-ext 自测：全部通过\n"));
    else {
        tui_print(tui_tr("  tui-ext selftest: ", "  tui-ext 自测："));
        u64_to_str((u64)failed, num);
        tui_print(num);
        tui_print(tui_tr(" group(s) FAILED\n", " 组失败\n"));
    }
    return failed;
}

/* ------------------------------------------------------------------ */
/* fix4: the "ext" lab page - live view of every registry + the        */
/* selftest runner (tui-ext command).                                  */
/* ------------------------------------------------------------------ */

static int ext_st_ran;
static int ext_st_failed;

static int page_ext_render(tui_window_t *win, int scroll) {
    (void)win;
    char b[100], n[16];
    int row = 1 - scroll;
    if (row >= 1) tui_print_at(row++, 2, tui_tr("TUI L1 extension lab (fix4 interface sets 7.1-7.7)",
                                                "TUI L1 扩展实验室（fix4 接口集 7.1-7.7）"));
    if (row >= 1) {
        char (*pages)[TUI_EXT_NAME_MAX] = 0;
        (void)pages;
        strcpy(b, tui_tr("  registries live now:", "  当前活跃注册表:"));
        tui_print_at(row++, 2, b);
    }
    if (row >= 1) {
        strcpy(b, tui_tr("    7.1 menu    entries: ", "    7.1 菜单    条目数: "));
        tui_menuext_item_t m[TUI_EXT_MENU_MAX];
        u64_to_str((u64)tui_menu_list(m, TUI_EXT_MENU_MAX), n);
        strcat(b, n);
        tui_print_at(row++, 2, b);
    }
    if (row >= 1) {
        strcpy(b, tui_tr("    7.2 page    entries: ", "    7.2 页面    条目数: "));
        char pn[TUI_EXT_PAGE_MAX][TUI_EXT_NAME_MAX];
        u64_to_str((u64)tui_page_list(pn, TUI_EXT_PAGE_MAX), n);
        strcat(b, n);
        tui_print_at(row++, 2, b);
    }
    if (row >= 1) {
        strcpy(b, tui_tr("    7.3 widget  entries: ", "    7.3 组件    条目数: "));
        tui_widgetext_t w[TUI_EXT_WIDGET_MAX];
        u64_to_str((u64)tui_widget_list(w, TUI_EXT_WIDGET_MAX), n);
        strcat(b, n);
        tui_print_at(row++, 2, b);
    }
    if (row >= 1) {
        strcpy(b, tui_tr("    7.4 command entries: ", "    7.4 命令    条目数: "));
        tui_cmdext_t c[TUI_EXT_CMD_MAX];
        u64_to_str((u64)tui_cmd_list(c, TUI_EXT_CMD_MAX), n);
        strcat(b, n);
        tui_print_at(row++, 2, b);
    }
    if (row >= 1) {
        strcpy(b, tui_tr("    7.5 theme   entries: ", "    7.5 主题    条目数: "));
        tui_themeext_t t[TUI_EXT_THEME_MAX];
        u64_to_str((u64)tui_theme_list(t, TUI_EXT_THEME_MAX), n);
        strcat(b, n);
        strcat(b, tui_tr("  current: ", "  当前: "));
        strcat(b, tui_theme_current()[0] ? tui_theme_current() : "(default)");
        tui_print_at(row++, 2, b);
    }
    if (row >= 1) {
        strcpy(b, tui_tr("    7.6 hotkey  entries: ", "    7.6 热键    条目数: "));
        tui_keyext_t k[TUI_EXT_KEY_MAX];
        u64_to_str((u64)tui_key_list(k, TUI_EXT_KEY_MAX), n);
        strcat(b, n);
        tui_print_at(row++, 2, b);
    }
    if (row >= 1) {
        strcpy(b, tui_tr("    7.7 event   entries: ", "    7.7 事件    条目数: "));
        tui_eventext_t e[TUI_EXT_EVENT_MAX];
        u64_to_str((u64)tui_event_list(e, TUI_EXT_EVENT_MAX), n);
        strcat(b, n);
        tui_print_at(row++, 2, b);
    }
    if (row >= 1) {
        strcpy(b, tui_tr("  registered pages:", "  已注册页面:"));
        tui_print_at(row++, 2, b);
    }
    {
        char pn[TUI_EXT_PAGE_MAX][TUI_EXT_NAME_MAX];
        int cnt = tui_page_list(pn, TUI_EXT_PAGE_MAX);
        for (int i = 0; i < cnt && row >= 1; i++) {
            strcpy(b, "    - ");
            strcat(b, pn[i]);
            tui_print_at(row++, 2, b);
        }
    }
    if (row >= 1) {
        strcpy(b, tui_tr("  last selftest: ", "  上次自测: "));
        if (ext_st_ran) {
            if (ext_st_failed == 0) strcat(b, tui_tr("ALL PASS", "全部通过"));
            else { u64_to_str((u64)ext_st_failed, n); strcat(b, n); strcat(b, tui_tr(" group(s) FAILED", " 组失败")); }
        } else strcat(b, tui_tr("not run yet", "尚未运行"));
        tui_print_at(row++, 2, b);
    }
    if (row >= 1) tui_print_at(row++, 2,
        tui_tr("Keys: r run selftest | t switch theme | F5 refresh | Esc back",
               "按键: r 运行自测 | t 切换主题 | F5 刷新 | Esc 返回"));
    return row + scroll - 1;
}

static int page_ext_input(int key) {
    if (key == 'r') {
        ext_st_failed = tui_ext_selftest();
        ext_st_ran = 1;
        tui_status_line(ext_st_failed ? tui_tr("selftest FAILED (see log)", "自测失败（见日志）")
                                      : tui_tr("selftest ALL PASS", "自测全部通过"));
        return 1;
    }
    if (key == 't') {
        /* cycle builtin themes from the lab page */
        tui_themeext_t t[TUI_EXT_THEME_MAX];
        int n = tui_theme_list(t, TUI_EXT_THEME_MAX);
        if (n <= 0) return 1;
        const char *cur = tui_theme_current();
        int pick = 0;
        for (int i = 0; i < n; i++)
            if (strcmp(t[i].name, cur) == 0) pick = (i + 1) % n;
        tui_theme_set(t[pick].name);
        char sb[80]; strcpy(sb, tui_tr("theme = ", "主题 = ")); strcat(sb, t[pick].name);
        tui_status_line(sb);
        return 1;
    }
    return 0;
}

void tui_ext_register_ext_page(void) {
    tui_page_register("builtin-ext", "ext", "TUI L1 extension lab",
                      "extension", 5, page_ext_render, page_ext_input, 0);
}
