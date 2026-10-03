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
 *       "changes": "driver optimization + config file",
 *       "changes_v2_url": "https://host/OpenCubeOS/update-v2.json"
 *     }
 *
 * "changes_v2_url" is OPTIONAL (dual-manifest protocol, "Plan D"): the
 * base manifest carries a SHORT "changes" (<= 127 bytes) so old kernels
 * (WP-09: 128-byte buffer + hard error on overflow) keep parsing it,
 * while new kernels fetch the long-changelog manifest to upgrade the
 * display-only value.  Old kernels ignore the extra field; new kernels
 * degrade silently to the short value when the v2 fetch fails.
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

/* Current kernel version - must stay in sync with cmd_uname (kmain.c).
 * Overridable at build time: make CFLAGS+=-DOC_RELEASE_VERSION=\"...\"
 * (used by the WP-10u end-to-end test to build a "newer" kernel). */
#ifndef OC_RELEASE_VERSION
#define OC_RELEASE_VERSION "WP-10u"
#endif
#define OC_UPDATE_CURRENT_VERSION OC_RELEASE_VERSION

/* Manifest fields as parsed from the JSON body.  version/time are
 * structural: if the server sends more than 31 bytes the check fails
 * with OC_UPDATE_E_JSON.  changes is display-only: if the server sends
 * more than 255 bytes the value is TRUNCATED and the check still
 * succeeds (WP-10a-checkupdate-fix: a longer server-side changelog used
 * to fail the whole check with "JSON parse failed" because of the old
 * 128-byte buffer). */
typedef struct {
    char version[32];
    char time[32];
    char changes[256];
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
/* WP-10u additions (system update on A/B partitions). */
#define OC_UPDATE_E_DISABLED (-9)   /* online_update=no in the config    */
#define OC_UPDATE_E_NOAB    (-10)   /* no A/B disk present               */
#define OC_UPDATE_E_SHA     (-11)   /* SHA256 verification failed        */
#define OC_UPDATE_E_TARGZ   (-12)   /* package is not valid gzip/tar     */
#define OC_UPDATE_E_IO      (-13)   /* filesystem I/O error              */
#define OC_UPDATE_E_SLOT    (-14)   /* invalid slot name (use A/B)       */
#define OC_UPDATE_E_ARGS    (-15)   /* NULL / malformed arguments        */

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

/* ------------------------------------------------------------------ *
 * WP-10u: package manifest check + kernel-internal helpers
 *
 * update.json gains three package fields:
 *
 *     "package_url"     direct URL of the *.tar.gz update package
 *     "package_sha256"  SHA256 of that package (64 hex chars)
 *     "package_size"    package size in bytes (JSON number)
 *
 * All three are OPTIONAL for the plain checkupdate flow (older
 * manifests stay fully compatible) but REQUIRED for `update`.
 * ------------------------------------------------------------------ */

typedef struct {
    oc_update_info_t info;      /* version / time / changes             */
    char package_url[192];      /* "" when the manifest has no URL      */
    char package_sha256[72];    /* 64 hex chars + NUL + headroom        */
    u64  package_size;
    int  have_package;          /* 1 when all three package fields were
                                 * present in the manifest              */
} oc_update_pkg_info_t;

/* Fetch update.json and parse version/time/changes plus the package
 * fields.  Returns OC_UPDATE_OK / OC_UPDATE_NEW like oc_check_update()
 * (and caches the result for update --status via oc_update_cache_check),
 * or a negative OC_UPDATE_E_*. */
int oc_update_check_pkg(oc_update_pkg_info_t *out);

/* URL container + parser (scheme/host/port/path), shared with the
 * WP-10u downloader in kernel/ab_update.c. */
typedef struct {
    int   use_tls;
    char  host[128];
    int   port;
    char  path[1664];   /* long CDN redirect paths (~1.3 KB) */
} oc_update_url_t;

int oc_update_url_parse(const char *url, oc_update_url_t *out);

/* Flat-JSON field helpers (top-level "key": value).  oc_update_json_string
 * returns 0 ok / -1 not found / -8 value too large for out.
 * oc_update_json_uint parses a non-negative decimal number. */
int oc_update_json_string(const char *json, const char *key,
                          char *out, int outlen);
int oc_update_json_uint(const char *json, const char *key, u64 *out);

/* Dual-manifest protocol ("Plan D") - long changelog support:
 *
 * oc_update_changes_v2_url extracts the optional "changes_v2_url" field
 * from a parsed base manifest.  Returns 0 and fills url (truncated to
 * cap) when present and non-empty, -1 when missing/empty.
 *
 * oc_update_fetch_changes_v2 fetches the v2 manifest at v2url (absolute
 * http:// or https:// only) and copies its long "changes" into out
 * (truncated to fit outcap).  Returns 0 when out was upgraded, -1 when
 * the URL was rejected, or an OC_UPDATE_E_* transport code.  Both are
 * display-only helpers: callers treat every failure as "keep the short
 * value", never as a check failure. */
int oc_update_changes_v2_url(const char *body, char *url, int cap);
int oc_update_fetch_changes_v2(const char *v2url, char *out, int outcap);

/* Cache a finished check for update --status (called by
 * oc_update_check_pkg; rc is OC_UPDATE_OK / OC_UPDATE_NEW). */
void oc_update_cache_check(int rc, const char *version);

#ifdef __cplusplus
}
#endif

#endif /* OC_UPDATE_H */
