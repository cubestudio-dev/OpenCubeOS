/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-10-tui
 * File: tui/tui_panel.c
 * Purpose: Panel system: a Z-ordered stack of windows with show/hide,
 *          move and a whole-stack repaint (update_panels).
 */
#include "tui.h"

#define TUI_MAX_PANELS 8

struct tui_panel {
    tui_window_t *win;
    int visible;
    int in_use;
};

static tui_panel_t g_panels[TUI_MAX_PANELS];
/* Z-order = paint order (index 0 paints first = bottom).  Slots hold
 * pointers so a PANEL* handed to the caller stays valid across
 * top/bottom reordering (ncurses semantics). */
static tui_panel_t *g_zorder[TUI_MAX_PANELS];

static void zorder_compact(void) {
    int out = 0;
    for (int i = 0; i < TUI_MAX_PANELS; i++)
        if (g_zorder[i] && g_zorder[i]->in_use)
            g_zorder[out++] = g_zorder[i];
    for (int i = out; i < TUI_MAX_PANELS; i++) g_zorder[i] = (void *)0;
}

static void panels_paint(void) {
    int cols, rows;
    tui_term_size(&cols, &rows);
    for (int yy = 0; yy < rows; yy++)
        for (int xx = 0; xx < cols; xx++)
            tui_backend_putc_at(xx, yy, ' ', tui_default_attr());
    for (int i = 0; i < TUI_MAX_PANELS; i++) {
        tui_panel_t *pl = g_zorder[i];
        if (pl && pl->in_use && pl->visible && pl->win)
            tui_window_refresh(pl->win);
    }
    tui_backend_flush();
}

/* fix2: session reset - every panel's window was freed by
 * tui_windows_reset(), so leaving the panel table populated left
 * zombie panels pointing at freed windows for the next session. */
void tui_panels_reset(void) {
    for (int i = 0; i < TUI_MAX_PANELS; i++) {
        g_panels[i].in_use = 0;
        g_panels[i].visible = 0;
        g_panels[i].win = (void *)0;
        g_zorder[i] = (void *)0;
    }
}

tui_panel_t *tui_panel_new(tui_window_t *win) {
    if (!win) return (void *)0;
    for (int i = 0; i < TUI_MAX_PANELS; i++) {
        if (!g_panels[i].in_use) {
            g_panels[i].in_use = 1;
            g_panels[i].win = win;
            g_panels[i].visible = 1;
            zorder_compact();
            g_zorder[i] = &g_panels[i];   /* append on top */
            return &g_panels[i];
        }
    }
    return (void *)0;
}

void tui_panel_free(tui_panel_t *panel) {
    if (!panel || panel < g_panels || panel >= g_panels + TUI_MAX_PANELS) return;
    panel->in_use = 0;
    panel->win = (void *)0;
    panel->visible = 0;
    zorder_compact();
    tui_update_panels();
}

int tui_panel_top(tui_panel_t *panel) {
    if (!panel || !panel->in_use) return -1;
    zorder_compact();
    int idx = -1;
    for (int i = 0; i < TUI_MAX_PANELS; i++)
        if (g_zorder[i] == panel) { idx = i; break; }
    if (idx < 0) return -1;
    for (int i = idx; i < TUI_MAX_PANELS - 1; i++)
        g_zorder[i] = g_zorder[i + 1];
    g_zorder[TUI_MAX_PANELS - 1] = panel;   /* painted last = on top */
    tui_update_panels();
    return 0;
}

int tui_panel_bottom(tui_panel_t *panel) {
    if (!panel || !panel->in_use) return -1;
    zorder_compact();
    int idx = -1;
    for (int i = 0; i < TUI_MAX_PANELS; i++)
        if (g_zorder[i] == panel) { idx = i; break; }
    if (idx < 0) return -1;
    for (int i = idx; i > 0; i--)
        g_zorder[i] = g_zorder[i - 1];
    g_zorder[0] = panel;                    /* painted first = bottom */
    tui_update_panels();
    return 0;
}

int tui_panel_show(tui_panel_t *panel) {
    if (!panel || !panel->in_use) return -1;
    panel->visible = 1;
    if (panel->win) panel->win->visible = 1;
    tui_update_panels();
    return 0;
}

int tui_panel_hide(tui_panel_t *panel) {
    if (!panel || !panel->in_use) return -1;
    panel->visible = 0;
    if (panel->win) panel->win->visible = 0;
    tui_update_panels();
    return 0;
}

int tui_panel_move(tui_panel_t *panel, int y, int x) {
    if (!panel || !panel->in_use || !panel->win) return -1;
    if (tui_window_move(panel->win, y, x) != 0) return -1;
    tui_update_panels();
    return 0;
}

int tui_panel_hidden(tui_panel_t *panel) {
    if (!panel || !panel->in_use) return 1;
    return panel->visible ? 0 : 1;
}

tui_window_t *tui_panel_window(tui_panel_t *panel) {
    if (!panel || !panel->in_use) return (void *)0;
    return panel->win;
}

void tui_update_panels(void) {
    if (!tui_started()) return;
    panels_paint();
}
