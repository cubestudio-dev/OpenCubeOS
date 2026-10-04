/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
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
#define SHELL_MAX_COMMANDS    192   /* WP-10c: +19 sound commands, was 160 */
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

/* ------------------------------------------------------------------ *
 * WP-10d-fix2 API (extended registration + structured help)
 * ------------------------------------------------------------------ */

/* Register a command with a work-package tag shown by `help -w`.
 * Same table and semantics as shell_register_command(); the tag is a
 * short ASCII string such as "WP-10d" or "WP-10c-selfhost" (copied into
 * a 24-byte buffer, truncated to fit; an empty tag reads as "WP-03",
 * the shell-core generation this table came from).
 * Returns 0 on success, -1 on bad args, -2 if table full. */
int shell_register_command_ex(const char *name, shell_cmd_fn handler,
                              const char *help, const char *wp);

/* Print all registered commands sorted A-Z (case-insensitive), one per
 * line, formatted "  <name><pad> - <help>".  Used by `help` (default)
 * and `help -a`. */
void shell_list_commands_a_z(void);

/* Print all registered commands grouped by work-package tag in canonical
 * WP order (WP-01 .. WP-10d-fix2), each group sorted A-Z, one header line
 * per group: "=== <tag> (<description>) ===".  Groups with no commands
 * are skipped.  Used by `help -w`. */
void shell_list_commands_by_wp(void);

/* WP-10d-fix2 self-test helpers: 1 when the A-Z view is sorted
 * (case-insensitive, non-decreasing) / when every known work-package
 * group is itself sorted; 0 otherwise. */
int shell_verify_sorted_a_z(void);
int shell_verify_wp_groups(void);

/* Live count of registered shell commands (for the boot self-test banner). */
int shell_command_count(void);

/* Unregister a command. */
int shell_unregister_command(const char *name);

/* Look up and execute a command (no parsing - just the first word).
 * Called by the interactive loop.
 * Returns 1 if a command was found and executed, 0 if not found.
 * NOTE: with WP-05 the interactive loop should call shell_execute_line()
 * instead. This function is kept for backward compatibility / simple use. */
int shell_execute(const char *line);

/* Print all registered commands sorted A-Z (WP-10d-fix2 rewrite:
 * the old registration-order listing is retired).  Equivalent to
 * shell_list_commands_a_z(); kept because it was part of the WP-03 API. */
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

/* WP-09: execute one command line and capture its console output.
 * Uses the shell's internal capture hook (which chains to and restores the
 * boot-time user hook, e.g. the serial mirror). The captured output is
 * copied NUL-terminated into out (max out_cap bytes). Returns the command
 * exit code; returns the uncaptured exit code if capture is unavailable. */
int shell_execute_captured(const char *line, char *out, int out_cap);

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
