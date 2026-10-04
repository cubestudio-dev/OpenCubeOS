/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-10-wp08fix1
 * File: shell/shell_lineedit.h
 * Purpose: Full-featured line editor for the kernel shell (and for any
 *          second console via sessions).
 *
 * Features (both for the oc> prompt and for SYS_READLINE users):
 *   - command history with Up/Down paging (Up = older, Down = newer)
 *   - in-line cursor movement: Left/Right, Home/End, Ctrl+A / Ctrl+E
 *   - editing: Backspace, Delete, Ctrl+U (kill to line start),
 *     Ctrl+K (kill to line end), Ctrl+W (kill previous word)
 *   - Ctrl+C: cancel the current input line (prints ^C)
 *   - Tab completion via a pluggable completer callback (the default
 *     completer completes the first word from the shell command table and
 *     later words from the VFS directory contents)
 *   - every keystroke redraws the line in place using only \r / \b / space
 *     (the framebuffer text console has no ANSI escape support)
 *
 * Naming follows the project interface spec ([category]_[specific]):
 *   shell_lineedit_init / shell_lineedit_history_add /
 *   shell_lineedit_history_get / shell_lineedit_cursor_move /
 *   shell_lineedit_tab_complete / shell_lineedit_ctrlc
 */
#ifndef OC_SHELL_LINEEDIT_H
#define OC_SHELL_LINEEDIT_H

#include "types.h"

/* ---- Capacity limits ---- */
#define SHELL_LINEEDIT_BUF_MAX      256  /* line buffer size (incl NUL) */
#define SHELL_LINEEDIT_HISTORY_MAX  32   /* history entries per session */
#define SHELL_LINEEDIT_PROMPT_MAX   32   /* prompt string max length */

/* Cursor movement directions for shell_lineedit_cursor_move(). */
#define SHELL_LINEEDIT_LEFT    -1
#define SHELL_LINEEDIT_RIGHT    1
#define SHELL_LINEEDIT_HOME    -2
#define SHELL_LINEEDIT_END      2

/* Pluggable Tab-completion callback.
 * Input : line   = the current input buffer (NUL-terminated)
 *         cursor = cursor position inside line (0..len)
 * Output: out    = receives the text to INSERT at the cursor (may be a
 *                  replacement of the current word); NUL-terminated.
 * Return: 1 when a completion was produced, 0 when nothing matched. */
typedef int (*shell_lineedit_completer_fn)(const char *line, int cursor,
                                           char *out, int out_cap);

/* One line-editor session: buffer + cursor + history ring. */
typedef struct shell_lineedit_session {
    char    buf[SHELL_LINEEDIT_BUF_MAX];   /* current input line          */
    int     len;                           /* chars in buf                */
    int     cursor;                        /* cursor offset (0..len)      */
    char    prompt[SHELL_LINEEDIT_PROMPT_MAX];
    int     prompt_len;
    /* History (linear array, oldest first). */
    char    hist[SHELL_LINEEDIT_HISTORY_MAX][SHELL_LINEEDIT_BUF_MAX];
    int     hist_count;
    int     hist_nav;                      /* -1 = typing draft, else idx */
    char    hist_draft[SHELL_LINEEDIT_BUF_MAX];  /* draft saved on Up     */
    int     use_history;                   /* 0 = no history (plain read) */
    int     ctrlc_hit;                     /* set when Ctrl+C cancels     */
    int     reading;                       /* 1 while a readline is active */
    shell_lineedit_completer_fn completer; /* NULL = no completion       */
} shell_lineedit_t;

/* ------------------------------------------------------------------ *
 * WP-10-wp08fix1 spec interfaces (operate on the global oc> session)
 * ------------------------------------------------------------------ */

/* One-time init of the global (kernel-shell) session.  Called from
 * shell_init(); safe to call again (resets buffers and history). */
void shell_lineedit_init(void);

/* Add a command line to the global session's history (skips empty lines
 * and consecutive duplicates).  Called automatically by
 * shell_lineedit_readline() when the user presses ENTER. */
void shell_lineedit_history_add(const char *cmd);

/* Return history entry `idx` (0 = oldest).  Returns NULL when out of
 * range or when history is empty. */
const char *shell_lineedit_history_get(int idx);

/* Number of history entries currently stored (0..SHELL_LINEEDIT_HISTORY_MAX). */
int shell_lineedit_history_count(void);

/* Move the cursor of the most recent readline() context.
 * dir: SHELL_LINEEDIT_LEFT / RIGHT / HOME / END.
 * This is a no-op when no readline() is in progress. */
void shell_lineedit_cursor_move(int dir);

/* Run one Tab-completion round for the global session (uses the active
 * completer, which by default completes command names and file paths).
 * Returns 1 when text was completed, 0 otherwise. */
int shell_lineedit_tab_complete(void);

/* Ctrl+C status: returns 1 when the last (or currently cancelled) input
 * line was aborted with Ctrl+C, and clears the flag. */
int shell_lineedit_ctrlc(void);

/* ------------------------------------------------------------------ *
 * Global (kernel shell) session: interactive readline
 * ------------------------------------------------------------------ */

/* Read one line into buf (max cap bytes, NUL-terminated).  Blocks with
 * interrupts enabled until the user presses ENTER (or Ctrl+C, which
 * delivers an empty line).  Echoes and edits the line on the console.
 * Returns the line length (0 for a cancelled line). */
int shell_lineedit_readline(char *buf, int cap);

/* Install a custom completer for the global session (NULL disables
 * completion).  By default the kernel shell installs the builtin
 * command+path completer below. */
void shell_lineedit_set_completer(shell_lineedit_completer_fn fn);

/* Builtin completer: first word -> shell command table prefix match,
 * other words -> VFS path completion (uses fs_vfs_readdir).
 * Implemented in shell_lineedit.c; shell.c provides the command-table
 * helper shell_complete_command_prefix() it depends on. */
int shell_lineedit_default_completer(const char *line, int cursor,
                                     char *out, int out_cap);

/* Declared here so shell_lineedit.c is self-contained even when older
 * shell.h revisions (without this prototype) are in use. */
int shell_complete_command_prefix(const char *prefix, char *out, int out_cap);

/* ------------------------------------------------------------------ *
 * Session engine (used by SYS_READLINE and plain readers)
 * ------------------------------------------------------------------ */

/* Initialize a session.  prompt may be "" ; use_history 0 disables
 * history (plain line input).  Returns 0 on success. */
int shell_lineedit_session_init(shell_lineedit_t *s, const char *prompt,
                                int use_history);

/* Set the prompt shown while the session reads a line. */
void shell_lineedit_session_set_prompt(shell_lineedit_t *s, const char *prompt);

/* Session variant of shell_lineedit_readline(). */
int shell_lineedit_session_readline(shell_lineedit_t *s, char *buf, int cap);

#endif /* OC_SHELL_LINEEDIT_H */
