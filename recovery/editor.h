/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-10-wp08fix1
 * File: shell/editor.h
 * Purpose: nano-style text editor for the kernel shell (oc>).
 *
 * The editor keeps the file as an array of lines (each line is a
 * heap-allocated buffer) and drives a per-line interactive editor on the
 * framebuffer console:
 *   - Up/Down select the current line, Left/Right/Home/End move inside it
 *   - typing inserts at the cursor, Backspace/Delete delete
 *   - ENTER splits the current line at the cursor (nano behaviour)
 *   - Ctrl+K deletes the current line
 *   - Ctrl+C shows the cursor position (nano's report-position binding)
 *   - Ctrl+O saves, Ctrl+X exits (asks before discarding changes),
 *     Ctrl+G shows the key guide
 *
 * Spec interfaces ([category]_[specific]):
 *   editor_open(file)  - load a file into the editor session
 *   editor_save()      - write the session back to the file
 *   editor_close()     - drop the session (frees the buffers)
 */
#ifndef OC_EDITOR_H
#define OC_EDITOR_H

#include "types.h"

/* Session limits. */
#define EDITOR_MAX_LINES   512
#define EDITOR_LINE_CAP    256
#define EDITOR_PATH_LEN    128

/* editor_open(): load `file` into the session (an empty new session is
 * started when the file does not exist yet).  Returns 0 on success,
 * negative on error (-1 bad args, -2 session already active, -3 out of
 * memory, -4 read error). */
int editor_open(const char *file);

/* editor_save(): write the current session content back to the file it
 * was opened with.  Returns 0 on success, negative on error (-1 no
 * active session, -2 open failed, -3 write failed, -5 REFUSED: the
 * session holds only PART of the file because the load hit the
 * 512-line/255-char session limits (BUG-0242 / A15-16) - the on-disk
 * file is left untouched; a read error at load time already aborts
 * editor_open with -4 instead of opening a partial view). */
int editor_save(void);

/* editor_close(): free the session.  Unsaved changes are DISCARDED —
 * the interactive loop asks for confirmation before calling this.
 * Returns 0 on success. */
int editor_close(void);

/* Session status.  Returns 1 while a session is open. */
int editor_is_active(void);

/* Pointer to the file the session was opened with ("" when inactive). */
const char *editor_file(void);

/* Interactive edit loop (nano-style).  Runs until the user exits with
 * Ctrl+X.  Returns 0 when the buffer was saved before exiting, 1 when
 * the user quit without saving. */
int editor_run(void);

/* BUG-0135 FIX: interactive edit loop in real modal vi style (NORMAL +
 * INSERT + ':' ex commands).  Runs on the same session state opened by
 * editor_open().  Returns 0 when the buffer was saved before exiting
 * (:w / :wq), 1 when the user quit without saving (:q! or :q on a clean
 * buffer). */
int editor_run_vi(void);

#endif /* OC_EDITOR_H */
