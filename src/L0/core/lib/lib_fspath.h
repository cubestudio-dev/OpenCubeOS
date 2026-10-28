/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP03 - core/lib: kernel path resolution.
 *
 * Verbatim rescue of the path-resolution mechanism out of the oc>
 * shell (recovery/shell.c, WP-05 era).  It stopped being shell
 * plumbing long ago: the syscall layer (SYS_CHDIR / SYS_GETCWD /
 * every path-taking fs syscall) and the fs service (L1SVC_FS_*)
 * resolve every user path through it, against the PER-PROCESS cwd
 * (BUG-0278) or the kernel fallback cwd.  Lives in core/lib because
 * it is pure string/state mechanics with a single VFS callback
 * (existence check on chdir).
 *
 * Carried-over fix history: BUG-0236 (component table on the heap -
 * never an 8 KiB stack array), BUG-0278 (caller-supplied cwd),
 * P0-8 (two alternating static resolve buffers).
 */
#ifndef OC_LIB_FSPATH_H
#define OC_LIB_FSPATH_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FSPATH_CWD_LEN 256   /* was SHELL_CWD_LEN; ABI-compatible size */

/* The kernel-fallback working directory ("/" until something chdirs).
 * Ring-3 processes carry their own per-process cwd; this one serves
 * kernel-side callers and processes that never chdir'd. */
const char *fspath_kernel_cwd(void);

/* Change the kernel-fallback cwd.  Resolves relative to the current
 * one, normalizes, and verifies the result is an existing directory.
 * Returns 0, -1 (bad path / overflow), -2 (does not exist),
 * -3 (not a directory). */
int fspath_set_kernel_cwd(const char *path);

/* Normalize an ABSOLUTE path in-place: collapses "//", "." and "..".
 * Returns 0, or -1 on overflow / non-absolute input. */
int fspath_normalize(char *path, int maxlen);

/* Resolve `path` against the CALLER-SUPPLIED cwd (BUG-0278 rules:
 * absolute kept, relative prepended), then normalize.  Returns 0 or
 * -1.  This is the resolution the syscall layer uses with the
 * per-process cwd. */
int fspath_resolve_base(const char *cwd, const char *path,
                        char *out, int out_len);

/* Resolve against the kernel-fallback cwd. */
int fspath_resolve(const char *path, char *out, int out_len);

/* Convenience wrapper: resolve into one of two alternating static
 * buffers (P0-8: two slots so `src = resolve(a), dst = resolve(b)`
 * works).  Falls back to the raw input on failure. */
const char *fspath_resolve_static(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* OC_LIB_FSPATH_H */
