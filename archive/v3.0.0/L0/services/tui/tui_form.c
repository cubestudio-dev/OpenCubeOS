/* SPDX-License-Identifier: Apache-2.0
 * [3.0.0-WP04 G11c: revived verbatim from v2.2.0 tui/tui_form.c; include
 *  paths flattened to the WP-03 -I layout, shell_lang->disp_lang and
 *  shell_execute*->tui_exec* renamed (see tui_exec.h).]
 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-10-tui
 * File: tui/tui_form.c
 * Purpose: Form system: labeled fields with in-place editing, tab/
 *          backtab navigation, Enter submits, Esc leaves.  Field
 *          buffers are caller-owned tui_field_t entries.
 */
#include "tui.h"
#include "lib_string.h"

tui_form_t *tui_form_new(tui_window_t *win, tui_field_t *fields, int count) {
    if (!win || !fields || count <= 0) return (void *)0;
    static tui_form_t forms[4];
    for (int i = 0; i < 4; i++) {
        if (forms[i].win == (void *)0) {
            forms[i].win = win;
            forms[i].fields = fields;
            forms[i].count = count;
            forms[i].current = 0;
            return &forms[i];
        }
    }
    return (void *)0;
}

void tui_form_free(tui_form_t *form) {
    if (!form) return;
    form->win = (void *)0;
    form->fields = (void *)0;
    form->count = 0;
}

int tui_form_set_field(tui_form_t *form, int idx, const char *value) {
    if (!form || idx < 0 || idx >= form->count || !form->fields) return -1;
    tui_field_t *f = &form->fields[idx];
    int i = 0;
    for (; value && value[i] && i < TUI_FIELD_MAX; i++) f->buffer[i] = value[i];
    f->buffer[i] = 0;
    f->len = i;
    f->cursor = i;
    return 0;
}

const char *tui_form_field_buffer(tui_form_t *form, int idx) {
    if (!form || idx < 0 || idx >= form->count || !form->fields) return (void *)0;
    return form->fields[idx].buffer;
}

static void paint_field(tui_form_t *form, int idx) {
    tui_field_t *f = &form->fields[idx];
    tui_window_t *w = form->win;
    tui_attr_t base = (idx == form->current)
        ? (tui_attr_t)(TUI_MAKE_ATTR(TUI_COLOR_WHITE, TUI_COLOR_GREEN))
        : (tui_attr_t)(TUI_MAKE_ATTR(TUI_COLOR_SILVER, TUI_COLOR_GRAY));
    tui_attr_t save = w->attr;
    w->attr = base;
    tui_window_focus_set(w);
    if (f->label) {
        /* fix4: right-align the label so it ENDS at x-1. The old
         * left-aligned paint (starting at x-1) let long labels run
         * over the field cells and they fought with the value. */
        w->attr = (tui_attr_t)TUI_MAKE_ATTR(TUI_COLOR_YELLOW, TUI_COLOR_BLACK) |
                  TUI_ATTR_BOLD;
        int lx = f->x - (int)strlen(f->label);
        if (lx < 1) lx = 1;
        tui_move(f->y, lx);
        tui_print(f->label);
    }
    w->attr = base;
    tui_move(f->y, f->x);
    for (int i = 0; i < f->width; i++) {
        char c = (i < f->len) ? f->buffer[i] : '_';
        tui_addch((u32)(u8)c);
    }
    w->attr = save;
}

int tui_form_post(tui_form_t *form) {
    if (!form || !form->win || !form->fields) return -1;
    tui_window_clear(form->win);
    for (int i = 0; i < form->count; i++) paint_field(form, i);
    tui_status_line("Tab next field | Enter submit | Esc cancel");
    tui_window_refresh(form->win);
    return 0;
}

int tui_form_unpost(tui_form_t *form) {
    if (!form || !form->win) return -1;
    tui_window_clear(form->win);
    tui_window_refresh(form->win);
    return 0;
}

int tui_form_driver(tui_form_t *form, int key) {
    if (!form || !form->fields) return -1;
    tui_field_t *f = &form->fields[form->current];
    switch (key) {
    case TUI_KEY_TAB:
    case TUI_KEY_CTRL_N:
    case TUI_KEY_DOWN:
        form->current = (form->current + 1) % form->count;
        tui_form_post(form);
        return 0;
    case TUI_KEY_PGUP:
    case TUI_KEY_UP:
    case TUI_KEY_CTRL_P:
        form->current = (form->current + form->count - 1) % form->count;
        tui_form_post(form);
        return 0;
    case TUI_KEY_ESC:
        return -1;
    case TUI_KEY_ENTER:
        if (form->current == form->count - 1) return 1;   /* submit */
        form->current++;
        tui_form_post(form);
        return 0;
    case TUI_KEY_BACKSPACE:
        if (f->cursor > 0) {
            for (int i = f->cursor - 1; i < f->len; i++)
                f->buffer[i] = f->buffer[i + 1];
            f->len--;
            f->cursor--;
            f->buffer[f->len] = 0;
            paint_field(form, form->current);
            tui_window_refresh(form->win);
        }
        return 0;
    default:
        if (key >= 0x20 && key < 0x7F && f->editable && f->len < TUI_FIELD_MAX &&
            f->cursor < f->width) {
            for (int i = f->len; i > f->cursor; i--)
                f->buffer[i] = f->buffer[i - 1];
            f->buffer[f->cursor++] = (char)key;
            f->len++;
            f->buffer[f->len] = 0;
            paint_field(form, form->current);
            tui_window_refresh(form->win);
        }
        return 0;
    }
}
