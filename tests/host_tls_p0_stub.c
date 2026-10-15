/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - host link stubs for tests/host_tls_p0_test.c.
 *
 * The kernel-side definitions of these symbols live in net/net_core.c,
 * which is too heavy to compile into the host harness. The test only
 * exercises the TLS state machine, so:
 *   - net_tcp_established_or_close_wait() returns 1 (= established),
 *     matching the socket states the P0 cases drive.
 *   - oc_kernel_strchr_decl() is a sentinel from the test's own
 *     #define strchr oc_kernel_strchr_decl guard (host_tls_p0_test.c:29);
 *     it must never be called, so returning 0 is honest.
 */
#include <stddef.h>

int net_tcp_established_or_close_wait(int fd) { (void)fd; return 1; }
void *oc_kernel_strchr_decl(void) { return (void *)0; }
