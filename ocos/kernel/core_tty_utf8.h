/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-tui-input-encoding-hardening 4.3
 * File: ocos/kernel/core_tty_utf8.h
 * Purpose: the UTF-8 TTY stream decoder (see core_tty_utf8.c).
 *
 * A byte stream fed through core_tty_utf8_write() renders one
 * codepoint per COMPLETE UTF-8 sequence via console_cn_putcw()
 * (the wide-cell console renderer), no matter how the stream is
 * split across calls - the echo path writes ONE BYTE per key event
 * (ush putc_), and the old per-call decoder painted every byte of
 * a multi-byte glyph as its own raw Latin-1 cell (three solid
 * blocks per hanzi on the frame buffer) until a full-line redraw
 * happened to fix it.
 */
#ifndef CORE_TTY_UTF8_H
#define CORE_TTY_UTF8_H

#include "types.h"

/* Feed `len` bytes of a UTF-8 console stream.  Any split of the
 * stream across calls is allowed: an incomplete trailing sequence
 * is carried in the TTY state and completed by the next call.  A
 * sequence interrupted by a foreign byte (new lead / ASCII) is
 * flushed as raw cells first - the same fallback the one-shot
 * decoder applied to invalid input.  Callers hold the same irqsave
 * discipline sys_write uses around console chunks. */
void core_tty_utf8_write(const u8 *s, u32 len);

/* Drop any pending incomplete sequence WITHOUT rendering it (used
 * by paths that must not leak state across unrelated streams). */
void core_tty_utf8_reset(void);

#endif /* CORE_TTY_UTF8_H */
