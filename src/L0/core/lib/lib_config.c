/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-09-fix5
 * File: kernel/lib/lib_config.c
 * Purpose: /etc/opencube.conf - first user-editable system configuration.
 *
 * Storage layout:
 *   - Preferred: FAT32 volume attached as ata0, mounted at /etc (built by
 *     the Makefile from etc/opencube.conf) -> edits survive reboots.
 *   - Fallback:  ramfs /etc seeded from the compiled-in defaults (no disk
 *     attached) -> same behaviour within one boot, resets on reboot.
 *
 * Format (ASCII only, 0x20..0x7E):
 *     # comment line
 *     key=value
 * Blank lines and unknown lines are ignored; values are trimmed.
 * See kernel/config.h for the documented missing/empty/invalid semantics.
 */
#include "lib_config.h"
#include "mem_heap.h"
#include "lib_log.h"
#include "screen_console.h"
#include "lib_string.h"

#include <stdint.h>

/* Compiled-in default file content.  Must stay in sync with the
 * etc/opencube.conf file used to build the FAT32 /etc image. */
static const char g_default_config[] =
    "# SPDX-License-Identifier: Apache-2.0\n"
    "# Copyright 2026 cubestudio-dev <cubestudio@qq.com>\n"
    "# Open Cube OS configuration\n"
    "# Update check URL (HTTP or HTTPS)\n"
    "update_url=" OC_CONFIG_DEFAULT_URL "\n"
    "\n"
    "# Update package base URL (WP-10u, HTTP or HTTPS)\n"
    "package_url=" OC_CONFIG_DEFAULT_PACKAGE_URL "\n"
    "\n"
    "# Auto check on boot (yes / no)\n"
    "auto_check=" OC_CONFIG_DEFAULT_AUTOCHECK "\n"
    "\n"
    "# Enable online system update (yes / no)\n"
    "online_update=" OC_CONFIG_DEFAULT_ONLINE_UPDATE "\n"
    "\n"
    "# Shell output language (en / zh); switch at runtime with `lang en|zh`\n"
    "shell.lang=" OC_CONFIG_DEFAULT_SHELL_LANG "\n";

static int g_config_initialized = 0;
/* fix3: 1 when /etc sits on a FAT32 volume (edits survive reboots),
 * 0 on the ramfs fallback (in-memory only). */
static int g_config_backend_fat = 0;

/* ------------------------------------------------------------------ */
/* small helpers                                                       */
/* ------------------------------------------------------------------ */

static int is_blank(char c) { return c == ' ' || c == '\t'; }

static int is_printable_ascii(char c) { return c >= 0x20 && c <= 0x7E; }

/* Trim leading/trailing blanks in place; returns start pointer. */
static const char *trim_const(const char *s, int *len_out) {
    while (*s && is_blank(*s)) s++;
    int len = 0;
    while (s[len]) len++;
    while (len > 0 && is_blank(s[len - 1])) len--;
    *len_out = len;
    return s;
}

static int line_is_printable(const char *s) {
    for (; *s; s++) {
        if (*s == '\n' || *s == '\r') continue;
        if (!is_printable_ascii(*s)) return 0;
    }
    return 1;
}

/* Read the whole config file into a heap buffer (caller kfrees).
 * Returns buffer or NULL; *len_out receives file length. */
/* 3.0.0-WP03 G5: the STORAGE side (mount /etc, stat, read, write)
 * is a backend installed at init time (services/fs provides the
 * FAT32/ramfs one; boot may install anything).  core/lib keeps only
 * the key-value mechanism - no fs dependency. */
static const lib_config_backend_t *g_backend = 0;
static const lib_config_backend_t g_memory_backend = {
    "memory", 0,
    0,                       /* bring_up: nothing to mount  */
    0,                       /* read: no file (defaults only) */
    0,                       /* write: no persistence        */
};

static char *read_file(int *len_out) {
    if (!g_backend || !g_backend->read) return 0;
    return g_backend->read(len_out);
}

static int write_file_backend(const char *text, int len) {
    if (!g_backend || !g_backend->write) return -1;
    return g_backend->write(text, len);
}

#if 0 /* the old fs bodies moved to services/fs/fs_config_backend.c */
static char *read_file_old(int *len_out) {
    *len_out = 0;
    int fd = fs_vfs_open(OC_CONFIG_PATH, VFS_O_RDONLY);
    if (fd < 0) return NULL;
    char *buf = (char *)kmalloc(OC_CONFIG_FILE_MAX);
    if (!buf) { fs_vfs_close(fd); return NULL; }
    int total = 0, n;
    while (total < OC_CONFIG_FILE_MAX - 1 &&
           (n = fs_vfs_read(fd, buf + total, OC_CONFIG_FILE_MAX - 1 - total)) > 0) {
        total += n;
    }
    fs_vfs_close(fd);
    buf[total] = 0;
    *len_out = total;
    if (total == 0) { kfree(buf); return NULL; }
    return buf;
}

/* Write text to the config file (truncate + write). 0 ok, -1 fail. */
static int write_file_old(const char *text, int len) {
    int fd = fs_vfs_open(OC_CONFIG_PATH, VFS_O_WRONLY | VFS_O_CREAT | VFS_O_TRUNC);
    if (fd < 0) return -1;
    int w = fs_vfs_write(fd, text, len);
    fs_vfs_close(fd);
    return (w == len) ? 0 : -1;
}
#endif

/* Create the config file from the built-in defaults. 0 ok. */
static int create_default_file(void) {
    return write_file_backend(g_default_config,
                              (int)sizeof(g_default_config) - 1);
}

/* ------------------------------------------------------------------ */
/* line parsing                                                        */
/* ------------------------------------------------------------------ */

/* Parse one "key=value" line.
 * Returns 1 = pair extracted, 0 = skip (comment/blank/invalid).
 * Non-printable-ASCII lines are skipped (documented read behaviour). */
static int parse_line(const char *line, char *key, int keycap,
                      char *val, int valcap) {
    if (!line_is_printable(line)) return 0;

    int klen = 0, vlen = 0;
    const char *k = trim_const(line, &klen);
    if (klen == 0 || k[0] == '#') return 0;

    /* find '=' inside [k, k+klen) */
    int eq = -1;
    for (int i = 0; i < klen; i++) {
        if (k[i] == '=') { eq = i; break; }
    }
    if (eq <= 0) return 0;               /* no '=' or empty key -> skip   */

    /* key: trim both sides */
    while (eq > 0 && is_blank(k[eq - 1])) eq--;
    int kl = eq;
    if (kl == 0) return 0;

    /* value: trim both sides (may be empty -> caller treats as missing) */
    const char *v = k + eq + 1;
    int vrem = klen - eq - 1;
    const char *vt = trim_const(v, &vlen);
    if (vlen >= valcap) vlen = valcap - 1;

    if (kl >= keycap) kl = keycap - 1;
    for (int i = 0; i < kl; i++) key[i] = k[i];
    key[kl] = 0;
    for (int i = 0; i < vlen; i++) val[i] = vt[i];
    val[vlen] = 0;
    (void)vrem;
    return 1;
}

/* ------------------------------------------------------------------ */
/* public API                                                          */
/* ------------------------------------------------------------------ */

int lib_config_init_with(const lib_config_backend_t *backend) {
    if (g_config_initialized) return 0;
    g_backend = backend ? backend : &g_memory_backend;

    /* 1. storage bring-up (mount /etc + ensure the file exists) is
     * the backend's job; a backend without bring_up (the in-memory
     * default) just runs on the compiled defaults. */
    if (g_backend->bring_up) {
        if (g_backend->bring_up(g_default_config,
                                (int)sizeof(g_default_config) - 1) != 0) {
            lib_log_info("config: backend bring-up FAILED");
            return -1;
        }
    }
    g_config_backend_fat = g_backend->persistent;

    /* 2. nothing else to do here: reads are lazy (lib_config_read
     * re-reads and re-parses the file on every call, exactly like the
     * pre-G5 behaviour), and the backend's bring_up already ensured
     * the file exists (seeding the defaults when it was missing). */
    g_config_initialized = 1;
    char line2[80];
    strcpy(line2, "system config (" OC_CONFIG_PATH ", ");
    strcat(line2, g_backend->name);
    strcat(line2, ")");
    OC_LOG_OK(line2);
    return 0;
}

/* compat: plain init = the in-memory backend (compiled defaults) */
int lib_config_init(void) {
    return lib_config_init_with(0);
}

int lib_config_read(const char *key, char *val_out, int outlen) {
    if (!key || !val_out || outlen <= 0) return OC_CONFIG_E_ARGS;

    int flen = 0;
    char *buf = read_file(&flen);
    if (!buf) return OC_CONFIG_E_NOFILE;

    int rc = OC_CONFIG_E_NOKEY;
    char line[OC_CONFIG_LINE_MAX];
    int i = 0;
    while (i < flen) {
        int j = 0;
        while (i < flen && buf[i] != '\n' && j < OC_CONFIG_LINE_MAX - 1)
            line[j++] = buf[i++];
        while (i < flen && buf[i] != '\n') i++;   /* drop overlong tail */
        if (i < flen && buf[i] == '\n') i++;
        line[j] = 0;
        if (j > 0 && line[j - 1] == '\r') line[j - 1] = 0; /* tolerate CRLF */

        char k[OC_CONFIG_KEY_MAX], v[OC_CONFIG_VAL_MAX];
        if (parse_line(line, k, sizeof(k), v, sizeof(v)) &&
            strcmp(k, key) == 0) {
            if (v[0] == 0) { rc = OC_CONFIG_E_NOKEY; break; }
            int vl = 0;
            while (v[vl]) vl++;
            if (vl >= outlen) { rc = OC_CONFIG_E_TOOLONG; break; }
            for (int c = 0; c <= vl; c++) val_out[c] = v[c];
            rc = 0;
            break;
        }
    }
    kfree(buf);
    return rc;
}

int lib_config_read_default(const char *key, char *val_out, int outlen,
                           const char *default_value) {
    if (!key || !val_out || outlen <= 0 || !default_value)
        return OC_CONFIG_E_ARGS;
    int rc = lib_config_read(key, val_out, outlen);
    if (rc == OC_CONFIG_E_NOFILE || rc == OC_CONFIG_E_NOKEY ||
        rc == OC_CONFIG_E_TOOLONG) {
        int dl = 0;
        while (default_value[dl]) dl++;
        if (dl >= outlen) return OC_CONFIG_E_TOOLONG;
        for (int i = 0; i <= dl; i++) val_out[i] = default_value[i];
        return 0;
    }
    return rc;   /* 0 (already read) or E_ARGS / E_IO */
}

int lib_config_write(const char *key, const char *value) {
    if (!key || !value) return OC_CONFIG_E_ARGS;

    /* validate key */
    int kl = 0;
    while (key[kl]) kl++;
    if (kl == 0 || kl >= OC_CONFIG_KEY_MAX) return OC_CONFIG_E_ARGS;

    /* trim value + validate printable ASCII */
    int vl = 0;
    const char *v = trim_const(value, &vl);
    if (vl == 0 || vl >= OC_CONFIG_VAL_MAX) return OC_CONFIG_E_ARGS;
    for (int i = 0; i < vl; i++)
        if (!is_printable_ascii(v[i])) return OC_CONFIG_E_ARGS;

    /* known-key semantic validation (documented defaults policy) */
    char canon[OC_CONFIG_VAL_MAX];
    for (int i = 0; i < vl; i++) canon[i] = v[i];
    canon[vl] = 0;
    if (strcmp(key, OC_CONFIG_KEY_AUTOCHECK) == 0) {
        if (strcasecmp(canon, "yes") == 0)      { canon[0]='y'; canon[1]='e'; canon[2]='s'; canon[3]=0; }
        else if (strcasecmp(canon, "no") == 0)  { canon[0]='n'; canon[1]='o'; canon[2]=0; }
        else return OC_CONFIG_E_ARGS;   /* invalid auto_check -> rejected */
    }
    if (strcmp(key, OC_CONFIG_KEY_ONLINE_UPDATE) == 0) {
        if (strcasecmp(canon, "yes") == 0)      { canon[0]='y'; canon[1]='e'; canon[2]='s'; canon[3]=0; }
        else if (strcasecmp(canon, "no") == 0)  { canon[0]='n'; canon[1]='o'; canon[2]=0; }
        else return OC_CONFIG_E_ARGS;   /* invalid online_update -> rejected */
    }
    if (strcmp(key, OC_CONFIG_KEY_SHELL_LANG) == 0) {
        /* fix3: only en / zh are valid; canonical lowercase is stored */
        if (strcasecmp(canon, "en") == 0)       { canon[0]='e'; canon[1]='n'; canon[2]=0; }
        else if (strcasecmp(canon, "zh") == 0)  { canon[0]='z'; canon[1]='h'; canon[2]=0; }
        else return OC_CONFIG_E_ARGS;   /* invalid shell.lang -> rejected */
    }

    /* read current file, rebuild with the new value, write back */
    int flen = 0;
    char *buf = read_file(&flen);
    if (!buf) {
        /* file missing -> recreate from defaults then apply */
        create_default_file();
        buf = read_file(&flen);
        if (!buf) return OC_CONFIG_E_IO;
    }

    char *out = (char *)kmalloc(OC_CONFIG_FILE_MAX);
    if (!out) { kfree(buf); return OC_CONFIG_E_IO; }
    int olen = 0, replaced = 0;

    char line[OC_CONFIG_LINE_MAX];
    int i = 0;
    while (i < flen) {
        int j = 0;
        while (i < flen && buf[i] != '\n' && j < OC_CONFIG_LINE_MAX - 1)
            line[j++] = buf[i++];
        while (i < flen && buf[i] != '\n') i++;
        if (i < flen && buf[i] == '\n') i++;
        line[j] = 0;

        char k[OC_CONFIG_KEY_MAX], v[OC_CONFIG_VAL_MAX];
        if (parse_line(line, k, sizeof(k), v, sizeof(v)) &&
            strcmp(k, key) == 0) {
            /* replace this line with key=value.
             * P0fix2 BUG-0036 (A5-01): the replace and append paths wrote
             * key/canon with NO bounds check on out[kmalloc(4096)] — a file
             * with hundreds of duplicate keys (or one nearly full with
             * comments) overflowed the heap block by hundreds of bytes.
             * Only the first matching line is rewritten (duplicate keys are
             * dropped, which also bounds the output by the input size),
             * and every write is bounds-checked. */
            if (!replaced) {
                if (olen + kl + vl + 2 > OC_CONFIG_FILE_MAX - 1) {
                    kfree(out);
                    kfree(buf);
                    return OC_CONFIG_E_TOOLONG;
                }
                for (int c = 0; c < kl; c++) out[olen++] = key[c];
                out[olen++] = '=';
                for (int c = 0; c < vl; c++) out[olen++] = canon[c];
                out[olen++] = '\n';
                replaced = 1;
            }
            /* duplicate matching lines are simply dropped */
        } else {
            for (int c = 0; line[c]; c++)
                if (olen < OC_CONFIG_FILE_MAX - 2) out[olen++] = line[c];
            if (olen < OC_CONFIG_FILE_MAX - 2) out[olen++] = '\n';
        }
    }
    if (!replaced) {
        /* P0fix2 BUG-0036 (A5-01): append path was unchecked as well. */
        if (olen + kl + vl + 2 > OC_CONFIG_FILE_MAX - 1) {
            kfree(out);
            kfree(buf);
            return OC_CONFIG_E_TOOLONG;
        }
        for (int c = 0; c < kl; c++) out[olen++] = key[c];
        out[olen++] = '=';
        for (int c = 0; c < vl; c++) out[olen++] = canon[c];
        out[olen++] = '\n';
    }

    int rc = write_file_backend(out, olen);
    kfree(out);
    kfree(buf);
    return (rc == 0) ? 0 : OC_CONFIG_E_IO;
}

int lib_config_get_all(char *buf, int buflen) {
    if (!buf || buflen <= 0) return OC_CONFIG_E_ARGS;
    int flen = 0;
    char *tmp = read_file(&flen);
    if (!tmp) {
        buf[0] = 0;
        return OC_CONFIG_E_NOFILE;
    }
    int copy = flen;
    if (copy > buflen - 1) copy = buflen - 1;
    for (int i = 0; i < copy; i++) buf[i] = tmp[i];
    buf[copy] = 0;
    kfree(tmp);
    return copy;
}

int lib_config_restore_defaults(void) {
    /* 3.0.0-WP03 G5: the unlink goes through the backend's write
     * path (an empty rewrite is equivalent to removal for the store;
     * the fs backend file keeps the real unlink).  When the backend
     * cannot write (in-memory), restoring = re-seeding the defaults
     * in memory, which create_default_file already covers. */
    if (create_default_file() == 0) return 0;
    return OC_CONFIG_E_IO;
}

const char *lib_config_default_url(void) {
    return OC_CONFIG_DEFAULT_URL;
}

const char *lib_config_default_autocheck(void) {
    return OC_CONFIG_DEFAULT_AUTOCHECK;
}

/* ------------------------------------------------------------------ */
/* boot auto-check helper                                              */
/* ------------------------------------------------------------------ */

/* Documented policy: auto_check=yes -> 1, no -> 0, anything else -> 0
 * (safe default) with a warning logged.  Missing file/key -> default
 * ("no") without a warning. */
int lib_config_autocheck_enabled(void) {
    char v[16];
    int rc = lib_config_read_default(OC_CONFIG_KEY_AUTOCHECK, v, sizeof(v),
                                    lib_config_default_autocheck());
    if (rc != 0) return 0;
    if (strcasecmp(v, "yes") == 0) return 1;
    if (strcasecmp(v, "no") == 0) return 0;
    lib_log_info("config: invalid auto_check value - using no (safe default)");
    return 0;
}

/* fix3: see lib_config.h. */
int lib_config_backend_fat(void) {
    return g_config_backend_fat;
}

/* Documented policy: online_update=yes -> 1, no -> 0, anything else
 * -> the default (yes) with a warning logged.  Missing file/key ->
 * default (yes) without a warning. */
int lib_config_online_update_enabled(void) {
    char v[16];
    int rc = lib_config_read_default(OC_CONFIG_KEY_ONLINE_UPDATE, v, sizeof(v),
                                    OC_CONFIG_DEFAULT_ONLINE_UPDATE);
    if (rc != 0) return 1;
    if (strcasecmp(v, "yes") == 0) return 1;
    if (strcasecmp(v, "no") == 0) return 0;
    lib_log_info("config: invalid online_update value - using yes (default)");
    return 1;
}

/* ------------------------------------------------------------------ */
/* L1 extension surface (kernel/ext.h)                                 */
/* ------------------------------------------------------------------ */

int l1_ext_config_read(const char *key, char *val_out, int outlen) {
    return lib_config_read(key, val_out, outlen);
}

int l1_ext_config_write(const char *key, const char *value) {
    return lib_config_write(key, value);
}

int l1_ext_config_get_all(char *buf, int buflen) {
    return lib_config_get_all(buf, buflen);
}
