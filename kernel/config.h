/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-09-fix5
 * File: kernel/config.h
 * Purpose: system configuration file subsystem.
 *
 * /etc/opencube.conf is the FIRST user-editable configuration file of the
 * system.  It is plain ASCII text ("key=value" lines, '#' comments, blank
 * lines ignored) and lives on the FAT32 /etc volume so it survives reboots;
 * when no disk is present the kernel falls back to a ramfs /etc and seeds
 * it with the compiled-in defaults (non-persistent, still fully functional).
 *
 * Well-known keys (WP-09-fix5):
 *
 *   update_url  - HTTP or HTTPS URL of the update manifest (JSON).
 *                 default: https://cubestudio-dev.github.io/OpenCubeOS/update.json
 *   auto_check  - "yes" / "no": run the update check automatically after
 *                 boot completes.  default: "no".
 *
 * Defined behaviour for missing / empty / invalid values:
 *
 *   - file missing            -> oc_config_init() recreates it from the
 *                                built-in defaults at boot;
 *                                oc_config_read() returns OC_CONFIG_E_NOFILE
 *                                (callers decide; oc_config_read_default()
 *                                substitutes the key's default value);
 *   - key missing             -> oc_config_read() returns
 *                                OC_CONFIG_E_NOKEY; read_default() fills
 *                                in the default value;
 *   - empty value ("key=")    -> same as key missing;
 *   - auto_check invalid      -> treated as "no" (safe default), a warning
 *                                is logged;
 *   - update_url invalid      -> accepted by config_write (prefix is only
 *                                validated at check time so the user can
 *                                pre-stage http or https URLs);
 *   - non-ASCII bytes         -> rejected on write, ignored on read
 *                                (line skipped; the system is ASCII-only).
 *
 * The public L1 extension surface is oc_ext_config_read / _write / _get_all
 * (kernel/ext.h).  The oc_config_* names here are the kernel-internal
 * implementation.
 */
#ifndef OC_CONFIG_H
#define OC_CONFIG_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OC_CONFIG_PATH        "/etc/opencube.conf"
#define OC_CONFIG_PATH_DIR    "/etc"
#define OC_CONFIG_KEY_URL     "update_url"
#define OC_CONFIG_KEY_AUTOCHECK "auto_check"

#define OC_CONFIG_DEFAULT_URL       "https://cubestudio-dev.github.io/OpenCubeOS/update.json"
#define OC_CONFIG_DEFAULT_AUTOCHECK "no"

/* Limits (ASCII only; values are trimmed of surrounding spaces). */
#define OC_CONFIG_KEY_MAX     64
#define OC_CONFIG_VAL_MAX     256
#define OC_CONFIG_LINE_MAX    320   /* key '=' value '\n' */
#define OC_CONFIG_FILE_MAX    4096  /* config files stay small */

/* oc_config_read() error codes (all negative). */
#define OC_CONFIG_E_ARGS    (-1)  /* NULL / bad arguments                */
#define OC_CONFIG_E_NOFILE  (-2)  /* /etc/opencube.conf missing or unreadable */
#define OC_CONFIG_E_NOKEY   (-3)  /* key not found (or empty value)      */
#define OC_CONFIG_E_TOOLONG (-4)  /* value does not fit the output buffer */
#define OC_CONFIG_E_IO      (-5)  /* VFS read/write error                */

/* ---- lifecycle ------------------------------------------------------- */

/* Boot-time setup: try to mount the FAT32 /etc volume (ata0), fall back to
 * a ramfs /etc, and make sure /etc/opencube.conf exists (recreate from the
 * built-in defaults when missing).  Returns 0 on success, negative on
 * unrecoverable error.  Safe to call once; later calls are no-ops. */
int oc_config_init(void);

/* Delete the config file and recreate it from the built-in defaults.
 * Returns 0 on success. */
int oc_config_restore_defaults(void);

/* ---- key/value access ------------------------------------------------ */

/* Strict read: 0 on success (val_out NUL-terminated),
 * OC_CONFIG_E_* on error.  Whitespace around the value is trimmed. */
int oc_config_read(const char *key, char *val_out, int outlen);

/* Lenient read: like oc_config_read() but a missing file / missing key /
 * empty value yields default_value instead of an error (returns 0,
 * val_out = default_value).  Returns OC_CONFIG_E_* only for bad args or
 * I/O errors. */
int oc_config_read_default(const char *key, char *val_out, int outlen,
                           const char *default_value);

/* Write (create or replace) a key with the given value.  The rest of the
 * file (comments, other keys, blank lines) is preserved.  The value must
 * be printable ASCII (0x20..0x7E) after trimming.  Returns 0 on success.
 * auto_check additionally validates to yes/no (case-insensitive). */
int oc_config_write(const char *key, const char *value);

/* Read the whole configuration file into buf (NUL-terminated).
 * Returns the number of bytes (excluding the NUL) or OC_CONFIG_E_*. */
int oc_config_get_all(char *buf, int buflen);

/* Built-in defaults (also used to seed the file). */
const char *oc_config_default_url(void);
const char *oc_config_default_autocheck(void);

/* Boot-time policy helper: 1 when auto_check=yes, else 0 (invalid values
 * fall back to "no" with a logged warning - documented defaults policy). */
int oc_config_autocheck_enabled(void);

/* Shell command handlers (registered in kmain.c). */
int cmd_config(const char *args);       /* config list|get|set|restore|path */
int cmd_config_test(const char *args);  /* config subsystem self-test       */

#ifdef __cplusplus
}
#endif

#endif /* OC_CONFIG_H */
