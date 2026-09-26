/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Cube Studio <cubestudio@qq.com> */
/* Open Cube OS - WP-02
 * File: kernel/console_in.h
 * Purpose: Console input handling - reads keys from the keyboard queue,
 *          echoes them, does simple line editing (backspace/enter/Ctrl+C),
 *          and exposes a line buffer + L1 input hooks.
 */
#ifndef OC_CONSOLE_IN_H
#define OC_CONSOLE_IN_H

#include "types.h"

/* Initialize the console input subsystem (call after keyboard + console). */
void oc_console_in_init(void);

/* Pump one key from the keyboard queue through the line editor.
 * Returns 1 if a complete line is now available, 0 otherwise. */
int oc_console_in_pump(void);

/* Block (with interrupt-enabled HLT loop) until a line is ready, then
 * copy it into `buf` (max `size` bytes, null-terminated). Returns the
 * line length. */
int oc_console_in_readline(char *buf, int size);

/* ---- L1 extension: console input ---- */

/* L1 can inject a string as if it were typed. Useful for scripts / test
 * harnesses / macros. */
void oc_console_in_inject(const char *text);

/* L1 can register an input hook that receives every complete line before
 * it's returned to readline(). Returns 0 to let L0 process the line
 * normally, non-zero to consume it (L0 drops it). */
typedef int (*oc_console_in_hook_fn)(const char *line, int len);

int oc_console_in_register_hook(oc_console_in_hook_fn fn);
int oc_console_in_unregister_hook(oc_console_in_hook_fn fn);

#endif /* OC_CONSOLE_IN_H */
