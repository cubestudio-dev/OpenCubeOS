/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP06a-ocse
 * File: ocse/ocse_gui.h
 * Purpose: the OCSE framebuffer installer front-end (buttons, mouse
 * pointer via USB mouse + I2C touchpad, live check/disk panels).
 */
#ifndef OCSE_GUI_H
#define OCSE_GUI_H

#include "types.h"

/* Actions returned by ocse_gui_loop(). */
enum {
    OCSE_ACT_NONE = 0,
    OCSE_ACT_GOTO_CL,       /* "command-line install" button   */
    OCSE_ACT_TOGGLE_LANG,   /* language button                */
    OCSE_ACT_RECHECK,       /* re-check button                */
    OCSE_ACT_GOTO_GUI,      /* CLI: `gui` command / hotkey     */
};

void ocse_gui_init(void);   /* pointer state reset + first draw */
void ocse_gui_draw(void);   /* full panel repaint              */
int  ocse_gui_loop(void);   /* pump inputs until an action     */
int  ocse_gui_hit_test(u32 x, u32 y);  /* button geometry self-check */

#endif /* OCSE_GUI_H */
