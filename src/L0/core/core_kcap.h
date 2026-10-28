/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP03 - core/kcap: kernel output capture.
 *
 * The captured-run infrastructure of the old oc> shell (WP-05/WP-09
 * machinery in recovery/shell.c), rescued into core/ as first-class
 * kernel mechanics: it is the load-bearing path of the whole L1SVC
 * ring-3 surface (every service op body reaches ring 3 through
 * SYS_L1SVC -> core_registry_svc_dispatch -> <cat>_l1_dispatch ->
 * core_kcap_run_fn), and of SYS_NETCMD.
 *
 * How it works: while a capture is active the capture hook is THE
 * console hook (the console driver holds exactly one hook slot,
 * drivers/display/screen_console.c).  The hook collects the characters
 * into a per-capture buffer and suppresses both the framebuffer draw
 * and the base tee (serial).  Captures STACK (up to 4 levels - a
 * captured body that itself triggers a nested capture, e.g. a service
 * op that calls another captured op, keeps the OUTER capture
 * collecting); the hook is torn down only when the LAST capture goes
 * away, at which point the base hook (the boot serial tee, installed
 * once by kmain via core_kcap_set_base_hook) is reinstalled.
 *
 * Nothing here knows about commands or shells: it runs caller-supplied
 * kernel functions and captures their console output.  3.0.0 rule
 * preserved from the BUG-0083/BUG-016/BUG-025 fix chain:
 *   - only the innermost ACTIVE capture accepts data;
 *   - during a capture neither serial nor framebuffer see the chars;
 *   - overflow is reported by marking, never by truncating silently.
 */
#ifndef OC_CORE_KCAP_H
#define OC_CORE_KCAP_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Console hook function shape (the driver-owned hook ABI). */
typedef int (*core_kcap_hook_fn_t)(void *ctx, u8 ch);

/* Install the BASE console hook - the always-on tee the kernel itself
 * wants (kmain installs the serial console here).  While captures are
 * active the capture hook sits on top of it; when the last capture
 * ends the base hook is restored automatically.  Called once at boot. */
void core_kcap_set_base_hook(core_kcap_hook_fn_t fn, void *ctx);

/* Run `fn(arg)` with its screen-console output captured into `out`
 * (capacity out_cap).  Returns 0 when the function ran (its own exit
 * code is reflected in the captured text - honest usage/FAIL lines),
 * -1 on argument/capture failure.  Re-entrant: a captured body that
 * itself starts a capture nests (up to 4 deep). */
int core_kcap_run_fn(int (*fn)(const char *), const char *arg,
                     char *out, int out_cap);

/* Non-zero while any capture is active (used by wide-glyph writers to
 * route their bytes through the hook unconditionally). */
int core_kcap_capturing(void);

#ifdef __cplusplus
}
#endif

#endif /* OC_CORE_KCAP_H */
