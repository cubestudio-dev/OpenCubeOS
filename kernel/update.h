/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-09-fix5
 * File: kernel/update.h
 * Purpose: online update check (checkupdate) over HTTP or HTTPS.
 *
 * The update manifest is JSON (all fields are ASCII strings):
 *
 *     {
 *       "version": "WP-10",
 *       "time": "2026-10-15",
 *       "changes": "driver optimization + config file"
 *     }
 *
 * The URL comes from /etc/opencube.conf key "update_url".  The scheme
 * decides the transport: "https://" -> TLS 1.2 client, "http://" -> plain
 * TCP, anything else -> error.  Version comparison is an exact string
 * match against OC_UPDATE_CURRENT_VERSION (keep it in sync with the
 * version printed by the uname command).
 *
 * Return codes of oc_check_update():
 *
 *    0  up to date (server version == current version)
 *    1  new version available
 *   -1  config file missing / unreadable
 *   -2  invalid URL prefix (must be http:// or https://)
 *   -3  DNS resolution failed
 *   -4  connect failed
 *   -5  no response (timeout)
 *   -6  malformed HTTP response
 *   -7  JSON parse failed
 *   -8  response too large for buffer
 */
#ifndef OC_UPDATE_H
#define OC_UPDATE_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Current kernel version - must stay in sync with cmd_uname (kmain.c). */
#define OC_UPDATE_CURRENT_VERSION "WP-09"

/* Manifest fields as parsed from the JSON body. */
typedef struct {
    char version[32];
    char time[32];
    char changes[128];
} oc_update_info_t;

/* Error codes (see file header). */
#define OC_UPDATE_OK           0
#define OC_UPDATE_NEW          1
#define OC_UPDATE_E_CONFIG   (-1)
#define OC_UPDATE_E_PREFIX   (-2)
#define OC_UPDATE_E_DNS      (-3)
#define OC_UPDATE_E_CONNECT  (-4)
#define OC_UPDATE_E_TIMEOUT  (-5)
#define OC_UPDATE_E_HTTP     (-6)
#define OC_UPDATE_E_JSON     (-7)
#define OC_UPDATE_E_BUFSIZE  (-8)

/* Synchronous check: fetch the manifest and compare versions.  Fills
 * *out with the server-reported fields on success (rc 0 or 1) and leaves
 * out->version empty on transport/parse errors. */
int oc_check_update(oc_update_info_t *out);

/* Asynchronous check: spawn a kernel thread that waits (up to 10 s) for
 * the network link, then performs the check and logs/prints the result.
 * Never blocks the caller; designed for the boot-time auto check.
 * Returns the thread id or a negative error. */
int oc_check_update_async(void);

/* Human-readable text for an oc_check_update() return code. */
const char *oc_check_update_strerror(int rc);

/* Shell command handlers (registered in kmain.c). */
int cmd_checkupdate(const char *args);      /* checkupdate       */
int cmd_checkupdate_test(const char *args); /* checkupdate_test  */

#ifdef __cplusplus
}
#endif

#endif /* OC_UPDATE_H */
