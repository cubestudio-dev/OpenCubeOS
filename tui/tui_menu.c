/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-10-tui
 * File: tui/tui_menu.c
 * Purpose: Menu system: item list inside a window, scroll window,
 *          driver-based navigation (up/down/pgup/pgdn/home/end/enter/
 *          esc), foreground/background attributes.
 */
#include "tui.h"

tui_menu_t *tui_menu_new(tui_window_t *win, tui_menu_item_t *items, int count) {
    if (!win || !items || count <= 0) return (void *)0;
    static tui_menu_t menus[4];
    for (int i = 0; i < 4; i++) {
        if (menus[i].win == (void *)0) {
            menus[i].win = win;
            menus[i].items = items;
            menus[i].count = count;
            menus[i].current = 0;
            menus[i].top_row = 0;
            menus[i].fore = (tui_attr_t)TUI_MAKE_ATTR(TUI_COLOR_SILVER, TUI_COLOR_BLACK);
            menus[i].back = (tui_attr_t)TUI_MAKE_ATTR(TUI_COLOR_BLACK, TUI_COLOR_TEAL) |
                            TUI_ATTR_BOLD;
            return &menus[i];
        }
    }
    return (void *)0;
}

void tui_menu_free(tui_menu_t *menu) {
    if (!menu) return;
    menu->win = (void *)0;
    menu->items = (void *)0;
    menu->count = 0;
}

void tui_menu_set_colors(tui_menu_t *menu, tui_attr_t fore, tui_attr_t back) {
    if (!menu) return;
    menu->fore = fore;
    menu->back = back;
}

int tui_menu_current(tui_menu_t *menu) {
    return menu ? menu->current : -1;
}

int tui_menu_set_current(tui_menu_t *menu, int idx) {
    if (!menu || idx < 0 || idx >= menu->count) return -1;
    menu->current = idx;
    return tui_menu_post(menu);
}

/* paint the visible slice of items */
int tui_menu_post(tui_menu_t *menu) {
    if (!menu || !menu->win) return -1;
    tui_window_t *w = menu->win;
    tui_window_clear(w);
    int rows = w->h;
    if (menu->current < menu->top_row) menu->top_row = menu->current;
    if (menu->current >= menu->top_row + rows)
        menu->top_row = menu->current - rows + 1;
    for (int r = 0; r < rows; r++) {
        int idx = menu->top_row + r;
        if (idx >= menu->count) break;
        tui_attr_t a = (idx == menu->current) ? menu->back : menu->fore;
        tui_attr_t save = w->attr;
        w->attr = a;
        tui_window_focus_set(w);
        tui_move(r, 1);
        char line[128];
        const char *name = menu->items[idx].name;
        int i = 0;
        for (; name && name[i] && i < 120; i++) line[i] = name[i];
        line[i] = 0;
        tui_print(line);
        /* pad the highlighted row so the bar covers the width */
        if (idx == menu->current)
            for (; i < w->w - 2; i++) tui_addch(' ');
        w->attr = save;
    }
    if (menu->items[menu->current].desc) {
        tui_status_line("%s", menu->items[menu->current].desc);
    }
    tui_window_refresh(w);
    return 0;
}

int tui_menu_unpost(tui_menu_t *menu) {
    if (!menu || !menu->win) return -1;
    tui_window_clear(menu->win);
    tui_window_refresh(menu->win);
    return 0;
}

int tui_menu_driver(tui_menu_t *menu, int key) {
    if (!menu) return -1;
    int visible_rows = menu->win ? menu->win->h : 24;
    /* fix2: mouse support - click an item to select it, click the
     * already-selected item to activate it, wheel to scroll. This is
     * the mainstream menu contract and it required no app changes:
     * every menu loop already forwards TUI_KEY_MOUSE here. */
    if (key == TUI_KEY_MOUSE) {
        tui_mouse_event_t ev;
        if (tui_mouse_get(&ev) != 0) return 0;
        tui_window_t *w = menu->win;
        if (!w) return 0;
        if (ev.button == 3) {   /* wheel */
            if (ev.action == TUI_BUTTON_WHEEL_UP && menu->current > 0)
                menu->current--;
            else if (ev.action == TUI_BUTTON_WHEEL_DOWN &&
                     menu->current < menu->count - 1)
                menu->current++;
            return tui_menu_post(menu) < 0 ? -2 : 0;
        }
        if (ev.action != TUI_BUTTON_PRESSED && ev.action != TUI_BUTTON_CLICKED)
            return 0;
        int my = ev.y - w->y;
        int mx = ev.x - w->x;
        if (my < 0 || my >= w->h || mx < 0 || mx >= w->w) return 0;
        int idx = menu->top_row + my;
        if (idx < 0 || idx >= menu->count) return 0;
        if (idx == menu->current) return 1;   /* click on selection = Enter */
        menu->current = idx;
        return tui_menu_post(menu) < 0 ? -2 : 0;
    }
    switch (key) {
    case TUI_KEY_UP:
    case TUI_KEY_CTRL_P:
        if (menu->current > 0) menu->current--;
        break;
    case TUI_KEY_DOWN:
    case TUI_KEY_CTRL_N:
        if (menu->current < menu->count - 1) menu->current++;
        break;
    case TUI_KEY_PGUP:
        menu->current -= visible_rows;
        if (menu->current < 0) menu->current = 0;
        break;
    case TUI_KEY_PGDN:
        menu->current += visible_rows;
        if (menu->current > menu->count - 1) menu->current = menu->count - 1;
        break;
    case TUI_KEY_HOME:
        menu->current = 0;
        break;
    case TUI_KEY_END:
        menu->current = menu->count - 1;
        break;
    case TUI_KEY_ENTER:
        return 1;
    case TUI_KEY_ESC:
        return -1;
    case TUI_KEY_NONE:
    default:
        return 0;                        /* ignore, keep menu open */
    }
    return tui_menu_post(menu) < 0 ? -2 : 0;
}
