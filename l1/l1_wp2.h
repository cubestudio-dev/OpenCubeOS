/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-02
 * File: kernel/ext_wp2.h
 * Purpose: WP-02 extension API summary. This file just re-exports the
 *          WP-02 extension functions from their respective headers so L1
 *          has one place to look.
 *
 * WP-02 adds FOUR new extension points (on top of WP-01's four):
 *
 *   5. IRQ handler registration        - irq.h
 *   6. Timer callback registration     - timer.h
 *   7. Keyboard input handler          - keyboard.h
 *   8. Exception handler registration  - exceptions.h
 *
 * Plus two console-input helpers:
 *
 *   9. Console input injection         - console_in.h
 *  10. Console input hook              - console_in.h
 *
 * All WP-01 interfaces (kernel/ext.h) remain unchanged.
 */
#ifndef OC_EXT_WP2_H
#define OC_EXT_WP2_H

#include "arch_irq.h"          /* arch_irq_register_handler / unregister */
#include "core_timer.h"        /* core_timer_register_periodic / oneshot / cancel */
#include "driver_input_keyboard.h"     /* driver_input_keyboard_register_handler / unregister */
#include "arch_exceptions.h"   /* arch_exc_register_handler / unregister */
#include "screen_console_in.h"   /* screen_console_in_inject / register_hook */

#endif /* OC_EXT_WP2_H */
