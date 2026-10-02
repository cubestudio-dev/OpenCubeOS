/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-09-fix5
 * File: kernel/config.c
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
#include "config.h"
#include "vfs.h"
#include "fat32.h"
#include "heap.h"
#include "log.h"
#include "console.h"
#include "string.h"

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
    "online_update=" OC_CONFIG_DEFAULT_ONLINE_UPDATE "\n";

static int g_config_initialized = 0;

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
static char *read_file(int *len_out) {
    *len_out = 0;
    int fd = vfs_open(OC_CONFIG_PATH, VFS_O_RDONLY);
    if (fd < 0) return NULL;
    char *buf = (char *)kmalloc(OC_CONFIG_FILE_MAX);
    if (!buf) { vfs_close(fd); return NULL; }
    int total = 0, n;
    while (total < OC_CONFIG_FILE_MAX - 1 &&
           (n = vfs_read(fd, buf + total, OC_CONFIG_FILE_MAX - 1 - total)) > 0) {
        total += n;
    }
    vfs_close(fd);
    buf[total] = 0;
    *len_out = total;
    if (total == 0) { kfree(buf); return NULL; }
    return buf;
}

/* Write text to the config file (truncate + write). 0 ok, -1 fail. */
static int write_file(const char *text, int len) {
    int fd = vfs_open(OC_CONFIG_PATH, VFS_O_WRONLY | VFS_O_CREAT | VFS_O_TRUNC);
    if (fd < 0) return -1;
    int w = vfs_write(fd, text, len);
    vfs_close(fd);
    return (w == len) ? 0 : -1;
}

/* Create the config file from the built-in defaults. 0 ok. */
static int create_default_file(void) {
    return write_file(g_default_config, (int)sizeof(g_default_config) - 1);
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

int oc_config_init(void) {
    if (g_config_initialized) return 0;

    /* 1. storage: FAT32 /etc volume when present, ramfs fallback. */
    const char *backend = "ramfs";
    if (fat32_mount("ata0", "/etc") == 0) {
        backend = "fat32";
    } else {
        /* create ramfs /etc so config paths resolve */
        if (vfs_mkdir(OC_CONFIG_PATH_DIR) < 0) {
            /* /etc may already exist in ramfs - that is fine */
        }
    }

    /* 2. ensure the file exists (seed with defaults when missing). */
    vfs_stat_t st;
    if (vfs_stat(OC_CONFIG_PATH, &st) < 0) {
        if (create_default_file() == 0) {
            char line[80];
            oc_strcpy(line, "config: created default /etc/opencube.conf (");
            oc_strcat(line, backend);
            oc_strcat(line, ")");
            oc_log_info(line);
        } else {
            oc_log_info("config: FAILED to create /etc/opencube.conf");
            return -1;
        }
    }

    g_config_initialized = 1;
    char line2[80];
    oc_strcpy(line2, "system config (" OC_CONFIG_PATH ", ");
    oc_strcat(line2, backend);
    oc_strcat(line2, ")");
    OC_LOG_OK(line2);
    return 0;
}

int oc_config_read(const char *key, char *val_out, int outlen) {
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
            oc_strcmp(k, key) == 0) {
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

int oc_config_read_default(const char *key, char *val_out, int outlen,
                           const char *default_value) {
    if (!key || !val_out || outlen <= 0 || !default_value)
        return OC_CONFIG_E_ARGS;
    int rc = oc_config_read(key, val_out, outlen);
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

int oc_config_write(const char *key, const char *value) {
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
    if (oc_strcmp(key, OC_CONFIG_KEY_AUTOCHECK) == 0) {
        if (oc_strcasecmp(canon, "yes") == 0)      { canon[0]='y'; canon[1]='e'; canon[2]='s'; canon[3]=0; }
        else if (oc_strcasecmp(canon, "no") == 0)  { canon[0]='n'; canon[1]='o'; canon[2]=0; }
        else return OC_CONFIG_E_ARGS;   /* invalid auto_check -> rejected */
    }
    if (oc_strcmp(key, OC_CONFIG_KEY_ONLINE_UPDATE) == 0) {
        if (oc_strcasecmp(canon, "yes") == 0)      { canon[0]='y'; canon[1]='e'; canon[2]='s'; canon[3]=0; }
        else if (oc_strcasecmp(canon, "no") == 0)  { canon[0]='n'; canon[1]='o'; canon[2]=0; }
        else return OC_CONFIG_E_ARGS;   /* invalid online_update -> rejected */
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
            oc_strcmp(k, key) == 0) {
            /* replace this line with key=value */
            for (int c = 0; c < kl; c++) out[olen++] = key[c];
            out[olen++] = '=';
            for (int c = 0; c < vl; c++) out[olen++] = canon[c];
            out[olen++] = '\n';
            replaced = 1;
        } else {
            for (int c = 0; line[c]; c++)
                if (olen < OC_CONFIG_FILE_MAX - 2) out[olen++] = line[c];
            if (olen < OC_CONFIG_FILE_MAX - 2) out[olen++] = '\n';
        }
    }
    if (!replaced) {
        for (int c = 0; c < kl; c++) out[olen++] = key[c];
        out[olen++] = '=';
        for (int c = 0; c < vl; c++) out[olen++] = canon[c];
        out[olen++] = '\n';
    }

    int rc = write_file(out, olen);
    kfree(out);
    kfree(buf);
    return (rc == 0) ? 0 : OC_CONFIG_E_IO;
}

int oc_config_get_all(char *buf, int buflen) {
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

int oc_config_restore_defaults(void) {
    vfs_unlink(OC_CONFIG_PATH);
    if (create_default_file() == 0) return 0;
    return OC_CONFIG_E_IO;
}

const char *oc_config_default_url(void) {
    return OC_CONFIG_DEFAULT_URL;
}

const char *oc_config_default_autocheck(void) {
    return OC_CONFIG_DEFAULT_AUTOCHECK;
}

/* ------------------------------------------------------------------ */
/* boot auto-check helper                                              */
/* ------------------------------------------------------------------ */

/* Documented policy: auto_check=yes -> 1, no -> 0, anything else -> 0
 * (safe default) with a warning logged.  Missing file/key -> default
 * ("no") without a warning. */
int oc_config_autocheck_enabled(void) {
    char v[16];
    int rc = oc_config_read_default(OC_CONFIG_KEY_AUTOCHECK, v, sizeof(v),
                                    oc_config_default_autocheck());
    if (rc != 0) return 0;
    if (oc_strcasecmp(v, "yes") == 0) return 1;
    if (oc_strcasecmp(v, "no") == 0) return 0;
    oc_log_info("config: invalid auto_check value - using no (safe default)");
    return 0;
}

/* Documented policy: online_update=yes -> 1, no -> 0, anything else
 * -> the default (yes) with a warning logged.  Missing file/key ->
 * default (yes) without a warning. */
int oc_config_online_update_enabled(void) {
    char v[16];
    int rc = oc_config_read_default(OC_CONFIG_KEY_ONLINE_UPDATE, v, sizeof(v),
                                    OC_CONFIG_DEFAULT_ONLINE_UPDATE);
    if (rc != 0) return 1;
    if (oc_strcasecmp(v, "yes") == 0) return 1;
    if (oc_strcasecmp(v, "no") == 0) return 0;
    oc_log_info("config: invalid online_update value - using yes (default)");
    return 1;
}

/* ------------------------------------------------------------------ */
/* shell: config command                                               */
/* ------------------------------------------------------------------ */

/* config list | config get <key> | config set <key> <value> |
 * config restore | config path */
int cmd_config(const char *args) {
    if (!args || !args[0]) {
        oc_console_puts("usage: config list | get <key> | set <key> <value> | restore | path\n");
        return 1;
    }
    char sub[16], rest[OC_CONFIG_LINE_MAX];
    int i = 0, j = 0;
    while (args[i] && args[i] != ' ' && i < 15) sub[j++] = args[i++];
    sub[j] = 0;
    while (args[i] == ' ') i++;
    j = 0;
    while (args[i] && j < OC_CONFIG_LINE_MAX - 1) rest[j++] = args[i++];
    rest[j] = 0;

    if (oc_strcmp(sub, "list") == 0) {
        char buf[OC_CONFIG_FILE_MAX];
        int n = oc_config_get_all(buf, sizeof(buf));
        if (n < 0) {
            oc_console_puts("config: cannot read /etc/opencube.conf\n");
            return 1;
        }
        oc_console_puts(buf);
        if (n > 0 && buf[n - 1] != '\n') oc_console_putc('\n');
        return 0;
    }
    if (oc_strcmp(sub, "get") == 0) {
        if (!rest[0]) { oc_console_puts("usage: config get <key>\n"); return 1; }
        char val[OC_CONFIG_VAL_MAX];
        int rc = oc_config_read(rest, val, sizeof(val));
        if (rc == 0) {
            oc_console_puts(val);
            oc_console_putc('\n');
            return 0;
        }
        oc_console_puts("config: ");
        oc_console_puts(rest);
        if (rc == OC_CONFIG_E_NOFILE) oc_console_puts(": config file missing\n");
        else if (rc == OC_CONFIG_E_NOKEY) oc_console_puts(": key not found\n");
        else oc_console_puts(": read error\n");
        return 1;
    }
    if (oc_strcmp(sub, "set") == 0) {
        /* split rest into key value */
        int k = 0;
        while (rest[k] && rest[k] != ' ') k++;
        char key[OC_CONFIG_KEY_MAX];
        int kl = k; if (kl >= OC_CONFIG_KEY_MAX) kl = OC_CONFIG_KEY_MAX - 1;
        for (int c = 0; c < kl; c++) key[c] = rest[c];
        key[kl] = 0;
        while (rest[k] == ' ') k++;
        if (kl == 0 || !rest[k]) {
            oc_console_puts("usage: config set <key> <value>\n");
            return 1;
        }
        int rc = oc_config_write(key, rest + k);
        if (rc == 0) {
            oc_console_puts("config: saved\n");
            return 0;
        }
        oc_console_puts("config: write rejected (empty, non-ASCII, too long, or invalid value)\n");
        return 1;
    }
    if (oc_strcmp(sub, "restore") == 0) {
        if (oc_config_restore_defaults() == 0) {
            oc_console_puts("config: restored defaults\n");
            return 0;
        }
        oc_console_puts("config: restore failed\n");
        return 1;
    }
    if (oc_strcmp(sub, "path") == 0) {
        oc_console_puts(OC_CONFIG_PATH "\n");
        return 0;
    }
    oc_console_puts("usage: config list | get <key> | set <key> <value> | restore | path\n");
    return 1;
}

/* ------------------------------------------------------------------ */
/* shell: config_test                                                  */
/* ------------------------------------------------------------------ */

/* Exercises the config subsystem end to end inside the running system:
 * write -> read back -> validation rejection -> get_all -> delete ->
 * restore defaults.  Leaves the system with the default configuration. */
int cmd_config_test(const char *args) {
    (void)args;
    char val[OC_CONFIG_VAL_MAX];
    int pass = 0, total = 0;
    const char *ok;

    /* 1. write a custom key + read back */
    total++;
    ok = "FAIL";
    if (oc_config_write("test_key", "hello world") == 0 &&
        oc_config_read("test_key", val, sizeof(val)) == 0 &&
        oc_strcmp(val, "hello world") == 0) { ok = "PASS"; pass++; }
    oc_console_puts("[config_test] write+read custom key: "); oc_console_puts(ok); oc_console_puts("\n");

    /* 2. write known key auto_check=yes + read back */
    total++;
    ok = "FAIL";
    if (oc_config_write(OC_CONFIG_KEY_AUTOCHECK, "YES") == 0 &&
        oc_config_read(OC_CONFIG_KEY_AUTOCHECK, val, sizeof(val)) == 0 &&
        oc_strcmp(val, "yes") == 0 &&
        oc_config_autocheck_enabled() == 1) { ok = "PASS"; pass++; }
    oc_console_puts("[config_test] write auto_check=YES (normalized to yes): "); oc_console_puts(ok); oc_console_puts("\n");

    /* 3. reject invalid auto_check */
    total++;
    ok = "FAIL";
    if (oc_config_write(OC_CONFIG_KEY_AUTOCHECK, "maybe") == OC_CONFIG_E_ARGS)
        { ok = "PASS"; pass++; }
    oc_console_puts("[config_test] reject invalid auto_check=maybe: "); oc_console_puts(ok); oc_console_puts("\n");

    /* 4. reject non-ASCII value */
    total++;
    ok = "FAIL";
    if (oc_config_write("test_key", "caf\xe9") == OC_CONFIG_E_ARGS)
        { ok = "PASS"; pass++; }
    oc_console_puts("[config_test] reject non-ASCII value: "); oc_console_puts(ok); oc_console_puts("\n");

    /* 5. get_all contains the custom key and the known URL key */
    total++;
    ok = "FAIL";
    {
        char all[OC_CONFIG_FILE_MAX];
        int n = oc_config_get_all(all, sizeof(all));
        int has_custom = 0, has_url = 0;
        if (n > 0) {
            if (oc_strchr(all, 't') != NULL) {
                /* simple containment checks */
                const char *p = all;
                has_custom = 0; has_url = 0;
                while (*p) {
                    if (oc_strncmp(p, "test_key=hello world", 20) == 0) has_custom = 1;
                    if (oc_strncmp(p, "update_url=", 11) == 0) has_url = 1;
                    p++;
                }
            }
        }
        if (has_custom && has_url) { ok = "PASS"; pass++; }
    }
    oc_console_puts("[config_test] get_all contains test_key and update_url: "); oc_console_puts(ok); oc_console_puts("\n");

    /* 6. delete the file -> read reports missing */
    total++;
    ok = "FAIL";
    if (vfs_unlink(OC_CONFIG_PATH) == 0 &&
        oc_config_read(OC_CONFIG_KEY_URL, val, sizeof(val)) == OC_CONFIG_E_NOFILE)
        { ok = "PASS"; pass++; }
    oc_console_puts("[config_test] delete file -> read reports missing: "); oc_console_puts(ok); oc_console_puts("\n");

    /* 7. WP-10u: all four well-known keys exist after restore */
    total++;
    ok = "FAIL";
    if (oc_config_restore_defaults() == 0 &&
        oc_config_read(OC_CONFIG_KEY_URL, val, sizeof(val)) == 0 &&
        oc_strcmp(val, oc_config_default_url()) == 0 &&
        oc_config_read(OC_CONFIG_KEY_AUTOCHECK, val, sizeof(val)) == 0 &&
        oc_strcmp(val, "no") == 0 &&
        oc_config_read(OC_CONFIG_KEY_PACKAGE_URL, val, sizeof(val)) == 0 &&
        oc_strcmp(val, OC_CONFIG_DEFAULT_PACKAGE_URL) == 0 &&
        oc_config_read(OC_CONFIG_KEY_ONLINE_UPDATE, val, sizeof(val)) == 0 &&
        oc_strcmp(val, "yes") == 0) { ok = "PASS"; pass++; }
    oc_console_puts("[config_test] restore defaults (4 keys): "); oc_console_puts(ok); oc_console_puts("\n");

    /* 8. WP-10u: package_url write/read roundtrip */
    total++;
    ok = "FAIL";
    if (oc_config_write(OC_CONFIG_KEY_PACKAGE_URL,
                        "http://10.0.2.2:8008/packages") == 0 &&
        oc_config_read(OC_CONFIG_KEY_PACKAGE_URL, val, sizeof(val)) == 0 &&
        oc_strcmp(val, "http://10.0.2.2:8008/packages") == 0)
        { ok = "PASS"; pass++; }
    oc_console_puts("[config_test] package_url write/read: "); oc_console_puts(ok); oc_console_puts("\n");

    /* 9. WP-10u: online_update yes/no roundtrip + invalid rejected */
    total++;
    ok = "FAIL";
    if (oc_config_write(OC_CONFIG_KEY_ONLINE_UPDATE, "no") == 0 &&
        oc_config_read(OC_CONFIG_KEY_ONLINE_UPDATE, val, sizeof(val)) == 0 &&
        oc_strcmp(val, "no") == 0 &&
        oc_config_online_update_enabled() == 0 &&
        oc_config_write(OC_CONFIG_KEY_ONLINE_UPDATE, "YES") == 0 &&
        oc_config_online_update_enabled() == 1 &&
        oc_config_write(OC_CONFIG_KEY_ONLINE_UPDATE, "maybe") == OC_CONFIG_E_ARGS)
        { ok = "PASS"; pass++; }
    oc_console_puts("[config_test] online_update yes/no/invalid: "); oc_console_puts(ok); oc_console_puts("\n");

    /* 10. restore defaults again (leave the system in a clean state) */
    total++;
    ok = "FAIL";
    if (oc_config_restore_defaults() == 0 &&
        oc_config_read(OC_CONFIG_KEY_ONLINE_UPDATE, val, sizeof(val)) == 0 &&
        oc_strcmp(val, "yes") == 0) { ok = "PASS"; pass++; }
    oc_console_puts("[config_test] final restore: "); oc_console_puts(ok); oc_console_puts("\n");

    char line[64]; char num[12];
    oc_strcpy(line, "[config_test] ");
    oc_u64_to_str((u64)pass, num); oc_strcat(line, num);
    oc_strcat(line, "/");
    oc_u64_to_str((u64)total, num); oc_strcat(line, num);
    oc_strcat(line, pass == total ? " PASS" : " FAIL");
    oc_console_puts(line);
    oc_console_puts("\n");
    return (pass == total) ? 0 : 1;
}

/* ------------------------------------------------------------------ */
/* L1 extension surface (kernel/ext.h)                                 */
/* ------------------------------------------------------------------ */

int oc_ext_config_read(const char *key, char *val_out, int outlen) {
    return oc_config_read(key, val_out, outlen);
}

int oc_ext_config_write(const char *key, const char *value) {
    return oc_config_write(key, value);
}

int oc_ext_config_get_all(char *buf, int buflen) {
    return oc_config_get_all(buf, buflen);
}
