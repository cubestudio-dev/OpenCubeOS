/* SPDX-License-Identifier: Apache-2.0
 * [3.0.0-WP04 G11c: revived verbatim from v2.2.0 tui/tui_ext.h; include
 *  paths flattened to the WP-03 -I layout, shell_lang->disp_lang and
 *  shell_execute*->tui_exec* renamed (see tui_exec.h).]
 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-10-chinese_support-fix4
 * File: tui/tui_ext.h
 * Purpose: L1 extension interface for the TUI - the same idea as the
 *          shell's command registry, so an L1 module can add menu
 *          entries, full pages, widgets, commands, themes, hotkeys and
 *          event handlers to the TUI without touching the core.
 *
 *          Interface sets (fix4 spec 7.1 - 7.7):
 *            7.1 menu    tui_menu_register / unregister / list / sort
 *            7.2 page    tui_page_register / unregister / list /
 *                        tui_page_render / tui_page_input
 *            7.3 widget  tui_widget_register / unregister / list /
 *                        tui_widget_render / tui_widget_event
 *            7.4 command tui_cmd_register / unregister / list /
 *                        tui_cmd_execute
 *            7.5 theme   tui_theme_register / unregister / list /
 *                        tui_theme_set
 *            7.6 hotkey  tui_key_register / unregister / list /
 *                        tui_key_conflict
 *            7.7 event   tui_event_register / unregister / list /
 *                        tui_event_dispatch
 *
 * All registries are static tables (freestanding kernel, no malloc
 * dependency); registration returns 0 on success, -1 when the table is
 * full, -2 for a duplicate name/key.  Every entry is tagged with its
 * owner ("builtin" for the in-kernel pages) so unregister works.
 */
#ifndef OC_TUI_EXT_H
#define OC_TUI_EXT_H

#include "tui.h"

/* ------------------------------------------------------------------ */
/* 7.1 menu extension                                                  */
/* ------------------------------------------------------------------ */

#define TUI_EXT_MENU_MAX  24
#define TUI_EXT_NAME_MAX  40

typedef struct {
    char name[TUI_EXT_NAME_MAX];     /* menu label                       */
    char desc[80];                   /* one-line description             */
    char icon[8];                    /* short ASCII glyph, e.g. "[T]"    */
    char group[TUI_EXT_NAME_MAX];    /* grouping key ("system", ...)     */
    int  hotkey;                     /* TUI_KEY_* or 0 = none            */
    int  priority;                   /* lower sorts first                */
    int  (*action)(void);            /* invoked when selected            */
    void *userptr;                   /* opaque owner data                */
    char owner[TUI_EXT_NAME_MAX];    /* who registered this entry        */
    int  used;
} tui_menuext_item_t;

int  tui_menu_register(const char *owner, const char *name,
                       const char *desc, const char *icon,
                       const char *group, int hotkey, int priority,
                       int (*action)(void), void *userptr);
int  tui_menu_unregister(const char *name);
int  tui_menu_list(tui_menuext_item_t *out, int cap);   /* returns count */
int  tui_menu_sort(void);     /* stable sort by (priority, name); returns count */

/* ------------------------------------------------------------------ */
/* 7.2 page extension                                                  */
/* ------------------------------------------------------------------ */

typedef struct {
    char name[TUI_EXT_NAME_MAX];     /* page id (tui-ext-page X)         */
    char title[80];
    char group[TUI_EXT_NAME_MAX];
    int  priority;
    /* render the page body into the full-screen window; `scroll` is
     * the caller-maintained offset. Return lines drawn. */
    int  (*render)(tui_window_t *win, int scroll);
    /* feed one key; return 1 = consumed, 0 = let the generic loop
     * handle it (scroll/tab/F-keys/esc). */
    int  (*input)(int key);
    void *userptr;
    char owner[TUI_EXT_NAME_MAX];
    int  used;
} tui_pageext_t;

int  tui_page_register(const char *owner, const char *name,
                       const char *title, const char *group,
                       int priority,
                       int (*render)(tui_window_t *, int),
                       int (*input)(int), void *userptr);
int  tui_page_unregister(const char *name);
int  tui_page_list(char (*out)[TUI_EXT_NAME_MAX], int cap);
int  tui_page_render(const char *name, tui_window_t *win, int scroll);
int  tui_page_input(const char *name, int key);        /* 1 = consumed */
int  tui_ext_page_main(const char *name);  /* generic page runner:
                                              Up/Down scroll, Tab next
                                              registered page, F1 help,
                                              F2 save, F5 repaint,
                                              Esc/F10/q exit           */

/* ------------------------------------------------------------------ */
/* 7.3 widget extension                                                */
/* ------------------------------------------------------------------ */

typedef struct {
    char type[16];                   /* text/button/input/list/table/
                                        progress/chart                    */
    char name[TUI_EXT_NAME_MAX];
    int  x, y, w, h;                 /* cell rect                        */
    tui_attr_t attr;
    /* draw the widget body at its rect (window is already cleared)   */
    int  (*render)(struct tui_window *win, int x, int y, int w, int h);
    /* deliver one event: a TUI key code, an ASCII char, or a mouse
     * report encoded as (button | action<<8 | x<<16 | y<<24);
     * return 1 = consumed                                            */
    int  (*event)(int ev);
    void *userptr;
    char owner[TUI_EXT_NAME_MAX];
    int  used;
} tui_widgetext_t;

int  tui_widget_register(const char *owner, const char *type,
                         const char *name, int x, int y, int w, int h,
                         tui_attr_t attr,
                         int (*render)(tui_window_t *, int, int, int, int),
                         int (*event)(int), void *userptr);
int  tui_widget_unregister(const char *name);
int  tui_widget_list(tui_widgetext_t *out, int cap);
int  tui_widget_render(const char *name, tui_window_t *win);
int  tui_widget_event(const char *name, int ev);

/* ------------------------------------------------------------------ */
/* 7.4 command extension (TUI-internal command registry)               */
/* ------------------------------------------------------------------ */

typedef struct {
    char name[TUI_EXT_NAME_MAX];     /* command word                     */
    char args[64];                   /* usage string                     */
    char desc[80];
    int  (*fn)(int argc, char **argv);
    void *userptr;
    char owner[TUI_EXT_NAME_MAX];
    int  used;
} tui_cmdext_t;

int  tui_cmd_register(const char *owner, const char *name,
                      const char *args, const char *desc,
                      int (*fn)(int argc, char **argv), void *userptr);
int  tui_cmd_unregister(const char *name);
int  tui_cmd_list(tui_cmdext_t *out, int cap);
int  tui_cmd_execute(const char *line);   /* returns fn's rc or -3 no cmd */

/* ------------------------------------------------------------------ */
/* 7.5 theme extension                                                 */
/* ------------------------------------------------------------------ */

typedef struct {
    char name[TUI_EXT_NAME_MAX];
    char desc[80];
    int  fg, bg;                     /* VGA palette indexes              */
    void *userptr;
    char owner[TUI_EXT_NAME_MAX];
    int  used;
} tui_themeext_t;

int  tui_theme_register(const char *owner, const char *name,
                        const char *desc, int fg, int bg, void *userptr);
int  tui_theme_unregister(const char *name);
int  tui_theme_list(tui_themeext_t *out, int cap);
int  tui_theme_set(const char *name);   /* applies immediately; 0 ok   */
const char *tui_theme_current(void);

/* ------------------------------------------------------------------ */
/* 7.6 hotkey extension                                                */
/* ------------------------------------------------------------------ */

typedef struct {
    int  key;                        /* TUI_KEY_*                        */
    int  mods;                       /* 0 none, 1 shift, 2 alt, 4 ctrl   */
    char name[TUI_EXT_NAME_MAX];     /* symbolic id                      */
    char desc[80];
    int  (*fn)(void);                /* action                           */
    void *userptr;
    char owner[TUI_EXT_NAME_MAX];
    int  used;
} tui_keyext_t;

int  tui_key_register(const char *owner, int key, int mods,
                      const char *name, const char *desc,
                      int (*fn)(void), void *userptr);
int  tui_key_unregister(const char *name);
int  tui_key_list(tui_keyext_t *out, int cap);
int  tui_key_conflict(int key, int mods);  /* -1 free, else index of
                                              the registered owner     */
int  tui_key_dispatch(int key, int mods);  /* 1 = a handler consumed it */

/* ------------------------------------------------------------------ */
/* 7.7 event extension                                                 */
/* ------------------------------------------------------------------ */

#define TUI_EVT_KEYBOARD "keyboard"
#define TUI_EVT_MOUSE    "mouse"
#define TUI_EVT_TIMER    "timer"

typedef struct {
    char type[TUI_EXT_NAME_MAX];     /* keyboard/mouse/timer/<custom>    */
    char name[TUI_EXT_NAME_MAX];     /* handler id                       */
    int  (*handler)(const char *type, void *payload);
    void *userptr;
    char owner[TUI_EXT_NAME_MAX];
    int  used;
} tui_eventext_t;

int  tui_event_register(const char *owner, const char *type,
                        const char *name,
                        int (*handler)(const char *, void *), void *userptr);
int  tui_event_unregister(const char *name);
int  tui_event_list(tui_eventext_t *out, int cap);
int  tui_event_dispatch(const char *type, void *payload); /* # of handlers run */

/* ------------------------------------------------------------------ */
/* shared                                                              */
/* ------------------------------------------------------------------ */

void tui_ext_init(void);        /* register builtin themes/keys       */
void tui_ext_register_ext_page(void); /* fix4: the ext lab page          */
void tui_ext_reset(void);       /* drop every non-builtin entry       */
int  tui_ext_selftest(void);    /* 7-group self-test, returns #failed */

#endif /* OC_TUI_EXT_H */
