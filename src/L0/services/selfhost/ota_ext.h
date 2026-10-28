/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS 3.0.0-WP03
 * File: services/selfhost/ota_ext.h
 * Purpose: the self-hosting root service's update-check + in-system
 * update + A/B slot API (the l1_ext_update_* / l1_ext_check_update*
 * family).
 *
 * Formerly sections 6-7 of the l1_ext.h grab-bag (services/l1/), split
 * into the owning selfhost root service in the WP-03 L0 restructure.
 * The implementations live in ota_update.c / ota_ab.c (same folder).
 * The A/B disk layout, boot-flag protocol and package format are
 * documented in ota_ab.h and docs/EXTENSIONS_WP10u.md.
 */
#ifndef OC_OTA_EXT_H
#define OC_OTA_EXT_H

#include "ota_ab.h"   /* ota_update_status_t + l1_ext_update_pkg_info_t */

#ifdef __cplusplus
extern "C" {
#endif

/* Update check result (mirrors ota_update_info_t in ota_update.h):
 * 0 = up to date, 1 = new version available, <0 = transport/parse error. */
typedef struct {
    char version[64];   /* BUG-0299 (A5-10): widened to mirror ota_update_info_t.version[64] - 32 truncated the official 44-char version string */
    char time[32];
    char changes[256];  /* mirrors ota_update_info_t (ota_update.h) */
} l1_ext_update_info_t;

int  l1_ext_check_update(l1_ext_update_info_t *out);
int  l1_ext_check_update_async(void);

/* In-system update + A/B slots (WP-10u): check the manifest (with
 * package fields), download/verify/install the package into an A/B
 * slot, switch the boot slot and roll back. */
int  l1_ext_update_check_pkg(l1_ext_update_pkg_info_t *out);
int  l1_ext_update_download(const char *url, const char *path);
int  l1_ext_update_verify(const char *path, const char *crypto_sha256_hex);
int  l1_ext_update_install(const char *pkg_path, const char *slot);
int  l1_ext_update_rollback(void);
int  l1_ext_update_set_boot(const char *slot);
int  l1_ext_update_get_status(ota_update_status_t *out);

#ifdef __cplusplus
}
#endif

#endif /* OC_OTA_EXT_H */
