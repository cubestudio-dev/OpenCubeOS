/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-10-tui-unified
 * File: tui/tui_desktop.h
 * Purpose: the unified TUI desktop - one `tui` entry that adapts to
 *          shell.lang (en|zh), a fully DYNAMIC menu tree (registered,
 *          never hard-coded in the UI, up to 6 levels deep), resident
 *          hotkeys, strong focus highlighting, a permanent status bar
 *          and loading/result feedback for every action.
 *
 * Design rules (WP-10-tui-unified spec):
 *   - single entry: `tui` (tui-cn is REMOVED); the desktop reads
 *     shell.lang at start and every label is selected per language.
 *   - everything is mounted through the registry below; the desktop
 *     runner renders whatever is registered, so L1 code can add or
 *     remove applications at runtime (tui_dt_node_add/remove).
 *   - menu depth: up to TUI_DT_DEPTH_MAX levels of submenu below the
 *     desktop root; Esc always pops one level and never loses state.
 *   - every long action shows a loading spinner and ends with an
 *     explicit OK / FAIL(reason) status line - no silent operations.
 *   - page crashes are contained: a runner wrapped by tui_dt_guard
 *     that longjmps back is reported and the menu survives; a desktop
 *     failure returns to the `oc>` prompt, never a kernel panic.
 */
#ifndef OC_TUI_DESKTOP_H
#define OC_TUI_DESKTOP_H

#include "tui.h"
#include "tui_ext.h"
#include "../shell/shell_lang.h"

/* ------------------------------------------------------------------ */
/* Language                                                            */
/* ------------------------------------------------------------------ */

/* current desktop language: SHELL_LANG_EN or SHELL_LANG_ZH.  Read from
 * shell.lang at desktop start; tui_lang_set() switches at runtime and
 * persists the change back into shell.lang (+/etc/opencube.conf). */
int  tui_lang_current(void);
int  tui_lang_set(int lang);               /* 0 ok; repaints on next page */
/* pick the right string for the current language (never NULL) */
const char *tui_tr(const char *en, const char *zh);

/* ------------------------------------------------------------------ */
/* Desktop node registry (dynamic menu tree)                           */
/* ------------------------------------------------------------------ */

#define TUI_DT_MAX          56             /* registry capacity        */
#define TUI_DT_CHILD_MAX    16             /* children per node        */
#define TUI_DT_DEPTH_MAX    6              /* deepest submenu level    */
#define TUI_DT_ID_MAX       28
#define TUI_DT_LABEL_MAX    32
#define TUI_DT_DESC_MAX     64
#define TUI_DT_OWNER_MAX    24

/* node kinds */
enum {
    TUI_DT_MENU = 0,   /* opens a submenu (children or populate())     */
    TUI_DT_PAGE,       /* full-screen page rendered by run()           */
    TUI_DT_APP,        /* full application with its own loop           */
    TUI_DT_SHELL,      /* leaves the TUI, runs a shell command, waits  */
    TUI_DT_TERMINAL,   /* leaves the TUI, spawns the ring-3 ush, waits */
};

typedef struct tui_dt_node tui_dt_node_t;

struct tui_dt_node {
    char id[TUI_DT_ID_MAX];           /* unique registry id             */
    char en[TUI_DT_LABEL_MAX];        /* English label                  */
    char zh[TUI_DT_LABEL_MAX];        /* Chinese label (UTF-8)          */
    char desc_en[TUI_DT_DESC_MAX];
    char desc_zh[TUI_DT_DESC_MAX];
    char icon[8];                     /* short ASCII glyph, e.g. "[F]"  */
    int  kind;                        /* TUI_DT_*                       */
    /* run the node (PAGE/APP/SHELL/TERMINAL); return 0 ok, <0 error */
    int  (*run)(tui_dt_node_t *self);
    /* lazily populate children before a MENU is entered (dynamic
     * device lists etc).  Return 0 ok; children added via
     * tui_dt_node_add(this_id, ...) survive for the session. */
    int  (*populate)(tui_dt_node_t *self);
    const char *shell_cmd;            /* SHELL kind: "name [args]"      */
    int  priority;                    /* lower sorts first within menu  */
    void *userptr;
    char owner[TUI_DT_OWNER_MAX];     /* who registered ("builtin"/L1)  */
    /* tree links (managed by the registry, do not fill in) */
    tui_dt_node_t *children[TUI_DT_CHILD_MAX];
    int  child_count;
    tui_dt_node_t *parent;
    int  level;                       /* root children = 1              */
};

/* Add a node under parent_id ("" or "root" = desktop root level).
 * The spec is copied into the registry.  Returns:
 *   0 ok | -1 bad args | -2 duplicate id | -3 registry or children full
 *   -4 parent not found | -5 would exceed TUI_DT_DEPTH_MAX             */
int  tui_dt_node_add(const char *parent_id, const tui_dt_node_t *spec);

/* Remove a node (and its whole subtree) by id. 0 ok, -1 bad/-2 missing.
 * Only the owner (or "builtin") may remove a node it does not own. */
int  tui_dt_node_remove(const char *owner, const char *id);

tui_dt_node_t *tui_dt_find(const char *id);
int  tui_dt_depth(const char *id);         /* 1..TUI_DT_DEPTH_MAX, -1 miss */

/* free every registered node (init/shutdown cycles) */
void tui_dt_reset(void);

/* ------------------------------------------------------------------ */
/* Feedback: status bar, loading, toasts                               */
/* ------------------------------------------------------------------ */

/* draw the resident bottom status line (per-language hotkey map) */
void tui_dt_status_bar(void);

/* transient result line above the status bar; stays until the next
 * key event.  ok!=0 renders in green ("OK"), 0 in red ("FAIL: ..."). */
void tui_dt_toast(int ok, const char *fmt, ...);
const char *tui_dt_toast_text(int *ok);   /* current toast ("" = none) */

/* loading spinner: begin prints "<msg> ..." with an animated glyph;
 * tui_dt_loading_tick() animates while the caller pumps the loop. */
void tui_dt_loading_begin(const char *msg);
void tui_dt_loading_tick(void);
void tui_dt_loading_end(int ok, const char *okmsg, const char *errmsg);

/* ------------------------------------------------------------------ */
/* Crash guard (exception isolation)                                   */
/* ------------------------------------------------------------------ */

/* Run fn(self) under a setjmp guard.  A longjmp back (tui_dt_panic)
 * or a negative return value is reported and contained: 0 ok,
 * negative = contained failure code. */
int  tui_dt_guard(int (*fn)(tui_dt_node_t *), tui_dt_node_t *self);
/* escape hatch for a page that hit an unrecoverable state; NESTS back
 * to the nearest tui_dt_guard with `code` (must be called from inside
 * a guarded runner). */
void tui_dt_panic(int code);

/* ------------------------------------------------------------------ */
/* Desktop runner                                                      */
/* ------------------------------------------------------------------ */

/* the unified `tui` entry: tui_init, bootstrap builtin applications
 * (idempotent), then run the menu tree loop until Esc/F10 at root.
 * Returns 0 on clean exit, <0 when the desktop itself failed and the
 * shell was restored (never panics the kernel). */
int  tui_desktop_main(void);

/* idempotent bootstrap of the builtin application tree (files,
 * processes, control panel, editor, terminal, command bridge, ...).
 * Safe to call repeatedly; only registers what is missing. */
void tui_dt_bootstrap(void);

/* number of registry nodes currently in use (selftest/diagnostics) */
int  tui_dt_count(void);

/* page helpers (full-screen pages opened by desktop applications) */
int  tui_dt_page_open(void);               /* 0 ok; sets focus            */
int  tui_dt_page_rows(void);
int  tui_dt_page_cols(void);
void tui_dt_page_flush(void);              /* repaint the page window     */
tui_window_t *tui_dt_page_window(void);    /* live page window (or NULL)  */
int  tui_dt_console_wait_key(void);        /* blocking key on the console */
int  tui_dt_reenter(void);                 /* re-init the desktop session */
void tui_dt_str_append(char *dst, int cap, const char *src);  /* bounded */
void tui_dt_page_wait_close(const char *hint);   /* hint + Esc/F10/q loop */

#endif /* OC_TUI_DESKTOP_H */
