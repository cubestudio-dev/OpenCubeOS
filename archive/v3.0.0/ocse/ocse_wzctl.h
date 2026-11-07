/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP06c-installer_gui
 * File: ocse/ocse_wzctl.h
 * Purpose: the installer wizard's widget toolkit - label / button /
 * single-select list / single-line input / progress bar, a Tab focus
 * ring with Enter wired to the DEFAULT button (wz_set_default),
 * pointer hit-testing and a three-source pointer pump (PS/2 mouse +
 * USB mouse + I2C touchpad). The wizard page draws itself through
 * wz_draw(); every string renders UTF-8 (ASCII -> the 8x16 cell font,
 * CJK -> the 16x16 unicode tables), so en and zh need no separate
 * code paths.
 *
 * All state lives in the linker's .ocsework NOLOAD zone (zeroed by
 * the OCBL loader) - see linker_ocse.ld; nothing here may use
 * regular .bss (the boot-structure zone guard in tools/check_bss.py).
 */
#ifndef OCSE_WZCTL_H
#define OCSE_WZCTL_H

#include "types.h"

/* Widget kinds. */
#define WZ_LABEL     0     /* static text (never focusable)            */
#define WZ_BUTTON    1     /* returns its action on click              */
#define WZ_LIST      2     /* single-select; Up/Down/click             */
#define WZ_INPUT     3     /* single-line text; typing when focused    */
#define WZ_PROGRESS  4     /* 0..100 bar                               */

/* wz_poll_click / wz_handle_key return this when nothing fired. */
#define WZ_ACT_NONE  0

/* Pool sizing (the .ocsework budget is ~21 KiB for wzctl + wizard). */
#define WZ_MAX_WIDGETS 16
#define WZ_LIST_ROWS   10
#define WZ_ROW_LEN     96
#define WZ_INPUT_LEN   64

int   wz_add_label(int x, int y, int text_key, const char* raw);
int   wz_add_button(int x, int y, int w, int h,
                    int text_key, const char* raw, int action);
int   wz_add_list(int x, int y, int w, int h);
int   wz_add_input(int x, int y, int w, int text_key, const char* raw);
int   wz_add_progress(int x, int y, int w, int h);

void  wz_reset(void);
void  wz_set_raw(int id, const char* text);
void  wz_set_visible(int id, int vis);
void  wz_set_progress(int id, int pct);
void  wz_list_clear(int id);
void  wz_list_add(int id, const char* row);
void  wz_list_set_sel(int id, int row);
int   wz_list_sel(int id);
const char* wz_list_row(int id, int row);
int   wz_list_count(int id);
const char* wz_input_text(int id);
void  wz_input_set(int id, const char* text);
void  wz_input_mask(int id, int on);   /* password: render '*' */
void  wz_set_default(int id);          /* the button Enter activates */

void  wz_draw(void);
void  wz_repaint(void);                /* draw + clear the dirty flag */
void  wz_repaint_if_dirty(void);       /* WP06f: draw only when marked  */
void  wz_touch(void);                  /* request a redraw            */
int   wz_handle_key(int key);          /* Tab/Enter/arrows/typing    */
int   wz_poll_click(void);             /* pointer pumps + hit test   */
int   wz_focus_id(void);
void  wz_focus_set(int id);

#endif /* OCSE_WZCTL_H */
