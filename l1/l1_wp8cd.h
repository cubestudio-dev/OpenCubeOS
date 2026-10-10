/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS WP-08cd
 * File: kernel/ext_wp8cd.h
 * Purpose: L1 EXTENSION API - WP-08cd interfaces (items 51-57).
 *
 * WP-08cd adds 7 L1 extension interfaces for user-space shell,
 * tools, job control, and signal handling.
 *
 * Total L1 surface: 57 (WP-01..07: 32, WP-08a: 8, WP-08b: 10, WP-08cd: 7).
 */
#ifndef OC_EXT_WP8CD_H
#define OC_EXT_WP8CD_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Interface 51: shell_run ----
 * Launch the user-space shell (ush). Blocks until ush exits.
 * Returns 0 on normal exit, -1 on failure. */
int shell_run(void);

/* ---- Interface 52: shell_register_builtin ----
 * Register a built-in command in the user-space shell.
 * name: command name (e.g., "mycmd")
 * fn: function pointer to the command handler
 * help: help string
 * Returns 0 on success, -1 if table is full. */
typedef int (*shell_builtin_fn)(int argc, char **argv);
int shell_register_builtin(const char *name, shell_builtin_fn fn, const char *help);
/* BUG-0092 FIX completion: symmetric removal of a previously registered
 * builtin (used by the boot self-test to clean up its probe entry so
 * the live oc> command count stays stable). Returns 0 = removed. */
int shell_unregister_builtin(const char *name);

/* ---- Interface 53: tool_register ----
 * Register a user-space tool that can be exec'd by the shell.
 * name: tool name (e.g., "ls")
 * elf_data: pointer to embedded ELF binary
 * elf_size: size of ELF binary
 * Returns 0 on success, -1 if table is full. */
int tool_register(const char *name, const u8 *elf_data, u64 elf_size);

/* ---- Interface 54: tool_list ----
 * List all registered tools. Writes tool names to buf.
 * Returns the number of tools. */
int tool_list(char *buf, int bufsize);

/* ---- Interface 55: job_create ----
 * BUG-0249 FIX (A16-11): creates a background job by spawning the
 * named embedded program (resolved through the same shared lookup
 * sys_execve uses) and binds the job entry to the pid/task the spawn
 * actually returned - no "highest live pid" guessing.
 * Returns the job id (index for job_list/job_control),
 *   -1 on bad/unknown name or spawn failure, -2 if the table is full. */
int job_create(const char *cmd);

/* ---- Interface 56: job_list ----
 * List all background jobs whose bound process is still alive. Writes
 * job info ("[id] pid=<pid> <name>" per line) to buf.
 * Returns the number of jobs. */
int job_list(char *buf, int bufsize);

/* ---- Interface 57: job_control ----
 * Control a job: bring to foreground (fg), background (bg), or kill.
 * job_id: job index from job_list
 * action: 0=fg (waits until the job's process exits), 1=bg,
 *         2=kill (releases the bound process through the unified
 *         reaper, same accounting as sys_kill(SIGKILL))
 * Returns 0 on success, -1/-2/-3 on failure. */
int job_control(int job_id, int action);

#ifdef __cplusplus
}
#endif
#endif /* OC_EXT_WP8CD_H */
