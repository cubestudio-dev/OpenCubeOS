/* SPDX-License-Identifier: Apache-2.0 */
/* Open Cube OS - WP-03 / WP-05/WP-07
 * File: kernel/shell.h
 * Purpose: Shell command registration + WP-05/WP-07 shell enhancements:
 *   - environment variables ($VAR expansion + export)
 *   - aliases (alias / unalias)
 *   - command chains (;, &&, ||)
 *   - I/O redirection (>, >>, <)
 *   - pipes (cmd1 | cmd2)
 *   - background (&)
 *   - basic wildcards (* and ?)
 *   - current working directory (cd / pwd)
 *   - shell_execute_line(): the full parser/executor
 *
 * The original shell_execute() / shell_register_command() API from WP-03 is
 * preserved unchanged for backward compatibility.
 */
#ifndef OC_SHELL_H
#define OC_SHELL_H

#include "types.h"

/* Forward declaration of the console hook function pointer type. We define
 * our own typedef name here to avoid pulling in ext.h (which transitively
 * pulls in fb.h / font.h / console.h). The signature matches
 * oc_console_hook_fn in ext.h. */
typedef int (*shell_hook_fn_t)(void *ctx, u8 ch);

/* ---- Limits ---- */
#define SHELL_MAX_COMMANDS    64
#define SHELL_MAX_ENV         32
#define SHELL_ENV_NAME_LEN    32
#define SHELL_ENV_VALUE_LEN   256
#define SHELL_MAX_ALIASES     32
#define SHELL_ALIAS_NAME_LEN  32
#define SHELL_ALIAS_TARGET_LEN 256
#define SHELL_CWD_LEN         256
#define SHELL_MAX_TOKENS      64
#define SHELL_MAX_ARGS        32

/* Command handler: receives the argument string (everything after the command
 * name) and returns 0 on success, non-zero on failure. */
typedef int (*shell_cmd_fn)(const char *args);

/* ------------------------------------------------------------------ *
 * WP-03 API (unchanged)
 * ------------------------------------------------------------------ */

/* Register a command. name is the command word (e.g. "mem").
 * help is a short description shown by the `help` command.
 * Returns 0 on success, -1 on bad args, -2 if table full. */
int shell_register_command(const char *name, shell_cmd_fn handler, const char *help);

/* Unregister a command. */
int shell_unregister_command(const char *name);

/* Look up and execute a command (no parsing - just the first word).
 * Called by the interactive loop.
 * Returns 1 if a command was found and executed, 0 if not found.
 * NOTE: with WP-05 the interactive loop should call shell_execute_line()
 * instead. This function is kept for backward compatibility / simple use. */
int shell_execute(const char *line);

/* Print all registered commands (for the `help` command). */
void shell_print_help(void);

/* ------------------------------------------------------------------ *
 * WP-05 API
 * ------------------------------------------------------------------ */

/* One-time shell state init. Call once at boot before any shell_execute_line. */
void shell_init(void);

/* Install a console output hook that the shell is aware of. The shell
 * needs to track the installed hook so that, while it is capturing output
 * (for redirection / pipes), it can still forward characters to the
 * previously-installed hook (e.g. serial output). Use this in place of
 * oc_ext_set_console_hook() during boot. */
void shell_install_console_hook(shell_hook_fn_t hook, void *ctx);

/* Full parser/executor. Tokenizes the line, expands env vars, applies
 * aliases, splits on ; / && / ||, handles pipes, redirects, background,
 * and wildcards. Returns 0 normally, non-zero on parser error. */
int shell_execute_line(const char *line);

/* ---- Environment variables ---- */
int shell_setenv(const char *name, const char *value);
const char *shell_getenv(const char *name);

/* ---- Aliases ---- */
int shell_register_alias(const char *alias, const char *target);
int shell_unregister_alias(const char *alias);

/* ---- Current working directory ---- */
const char *shell_get_cwd(void);
int shell_set_cwd(const char *path);

/* Resolve a (possibly relative) path against the shell cwd into `out`
 * (size out_len). Absolute paths are copied as-is. Returns 0 on success,
 * -1 on overflow. */
int shell_resolve_path(const char *path, char *out, int out_len);

/* Convenience wrapper: returns a pointer to a static buffer with the
 * resolved path. The buffer is overwritten on each call. */
const char *shell_resolve_path_static(const char *path);

/* ---- Stdin access (for commands that consume piped input) ----
 *
 * When a command is the right-hand side of a pipe, or when its stdin is
 * redirected from a file with `<`, the shell sets up a stdin buffer. The
 * command can read from it via shell_read_stdin().
 *
 * Returns the number of bytes read (0 at EOF), or -1 if no stdin is
 * available (in which case the command should fall back to its normal
 * input handling). */
int shell_read_stdin(void *buf, int size);

/* Returns 1 if a stdin buffer is currently active (pipe or < redirect). */
int shell_has_stdin(void);

#endif /* OC_SHELL_H */
