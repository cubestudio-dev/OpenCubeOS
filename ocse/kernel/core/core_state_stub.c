/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-compat-and-security-hardening B8
 * File: ocse/kernel/core/core_state_stub.c
 * Purpose: the vendored sleep-window stubs.
 *
 * core_power.c (vendored from the WP-07-era tree) brackets the ACPI
 * S3 window with boot_state_sleep_arm/disarm - the full OCST monitor
 * area lives in the OCOS kernel (core_boot_state.c), which this
 * standalone installer kernel does not carry.  The OCSE installer
 * never suspends (S3 is an OCOS runtime feature), so the bracket is
 * honestly a no-op here: the calls stay (the vendored power path is
 * unmodified), the bodies are empty.  This is what finally lets the
 * OCSE image link again after the WP-07 vendoring introduced the
 * references - the tree had not been rebuilt since, and the dev
 * channel's ocse asset silently froze at the WP-l1apps-merge cut
 * (live-caught by the B8 four-asset audit). */
void boot_state_sleep_arm(void) {}
void boot_state_sleep_disarm(void) {}
