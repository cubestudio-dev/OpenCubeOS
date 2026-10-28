/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP03 - services/ota: config op bodies, rescued
 * verbatim out of core/lib/lib_config.c (renamed shell_cmd_* ->
 * ota_cmd_*) when the oc> shell died - these are the L1SVC "ota"
 * ops' bodies (L1SVC_OTA_CONFIG / L1SVC_OTA_CONFIG_TEST) and the
 * /etc/opencube.conf mechanism itself stays in core/lib/lib_config.c.
 */
#include "lib_config.h"
#include "lib_string.h"
#include "screen_console.h"
#include "ota_cmds.h"
#include "disp_lang.h"   /* config set shell.lang syncs the live console language */
#include "fs_vfs.h"      /* config_test round-trips a temp file through the VFS */

/* ------------------------------------------------------------------ */
/* shell: config command                                               */
/* ------------------------------------------------------------------ */

/* config list | config get <key> | config set <key> <value> |
 * config restore | config path */
int ota_cmd_config(const char *args) {
    if (!args || !args[0]) {
        screen_console_puts("usage: config list | get <key> | set <key> <value> | restore | path\n");
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

    if (strcmp(sub, "list") == 0) {
        char buf[OC_CONFIG_FILE_MAX];
        int n = lib_config_get_all(buf, sizeof(buf));
        if (n < 0) {
            screen_console_puts("config: cannot read /etc/opencube.conf\n");
            return 1;
        }
        screen_console_puts(buf);
        if (n > 0 && buf[n - 1] != '\n') screen_console_putc('\n');
        return 0;
    }
    if (strcmp(sub, "get") == 0) {
        if (!rest[0]) { screen_console_puts("usage: config get <key>\n"); return 1; }
        char val[OC_CONFIG_VAL_MAX];
        int rc = lib_config_read(rest, val, sizeof(val));
        if (rc == 0) {
            screen_console_puts(val);
            screen_console_putc('\n');
            return 0;
        }
        screen_console_puts("config: ");
        screen_console_puts(rest);
        if (rc == OC_CONFIG_E_NOFILE) screen_console_puts(": config file missing\n");
        else if (rc == OC_CONFIG_E_NOKEY) screen_console_puts(": key not found\n");
        else screen_console_puts(": read error\n");
        return 1;
    }
    if (strcmp(sub, "set") == 0) {
        /* split rest into key value */
        int k = 0;
        while (rest[k] && rest[k] != ' ') k++;
        char key[OC_CONFIG_KEY_MAX];
        int kl = k; if (kl >= OC_CONFIG_KEY_MAX) kl = OC_CONFIG_KEY_MAX - 1;
        for (int c = 0; c < kl; c++) key[c] = rest[c];
        key[kl] = 0;
        while (rest[k] == ' ') k++;
        if (kl == 0 || !rest[k]) {
            screen_console_puts("usage: config set <key> <value>\n");
            return 1;
        }
        int rc = lib_config_write(key, rest + k);
        if (rc == 0) {
            /* fix3: keep the live shell language in sync with the
             * persisted value (same effect as `lang en|zh`).  The write
             * above already validated the value, so set_str cannot fall
             * back to en behind the user's back here. */
            if (strcmp(key, OC_CONFIG_KEY_SHELL_LANG) == 0)
                disp_lang_set_str(rest + k);
            screen_console_puts("config: saved\n");
            return 0;
        }
        screen_console_puts("config: write rejected (empty, non-ASCII, too long, or invalid value)\n");
        return 1;
    }
    if (strcmp(sub, "restore") == 0) {
        if (lib_config_restore_defaults() == 0) {
            screen_console_puts("config: restored defaults\n");
            return 0;
        }
        screen_console_puts("config: restore failed\n");
        return 1;
    }
    if (strcmp(sub, "path") == 0) {
        screen_console_puts(OC_CONFIG_PATH "\n");
        return 0;
    }
    screen_console_puts("usage: config list | get <key> | set <key> <value> | restore | path\n");
    return 1;
}

/* ------------------------------------------------------------------ */
/* shell: lib_config_test                                                  */
/* ------------------------------------------------------------------ */

/* Exercises the config subsystem end to end inside the running system:
 * write -> read back -> validation rejection -> get_all -> delete ->
 * restore defaults.  Leaves the system with the default configuration. */
int ota_cmd_config_test(const char *args) {
    (void)args;
    char val[OC_CONFIG_VAL_MAX];
    int pass = 0, total = 0;
    const char *ok;

    /* 1. write a custom key + read back */
    total++;
    ok = "FAIL";
    if (lib_config_write("test_key", "hello world") == 0 &&
        lib_config_read("test_key", val, sizeof(val)) == 0 &&
        strcmp(val, "hello world") == 0) { ok = "PASS"; pass++; }
    screen_console_puts("[config_test] write+read custom key: "); screen_console_puts(ok); screen_console_puts("\n");

    /* 2. write known key auto_check=yes + read back */
    total++;
    ok = "FAIL";
    if (lib_config_write(OC_CONFIG_KEY_AUTOCHECK, "YES") == 0 &&
        lib_config_read(OC_CONFIG_KEY_AUTOCHECK, val, sizeof(val)) == 0 &&
        strcmp(val, "yes") == 0 &&
        lib_config_autocheck_enabled() == 1) { ok = "PASS"; pass++; }
    screen_console_puts("[config_test] write auto_check=YES (normalized to yes): "); screen_console_puts(ok); screen_console_puts("\n");

    /* 3. reject invalid auto_check */
    total++;
    ok = "FAIL";
    if (lib_config_write(OC_CONFIG_KEY_AUTOCHECK, "maybe") == OC_CONFIG_E_ARGS)
        { ok = "PASS"; pass++; }
    screen_console_puts("[config_test] reject invalid auto_check=maybe: "); screen_console_puts(ok); screen_console_puts("\n");

    /* 4. reject non-ASCII value */
    total++;
    ok = "FAIL";
    if (lib_config_write("test_key", "caf\xe9") == OC_CONFIG_E_ARGS)
        { ok = "PASS"; pass++; }
    screen_console_puts("[config_test] reject non-ASCII value: "); screen_console_puts(ok); screen_console_puts("\n");

    /* 5. get_all contains the custom key and the known URL key */
    total++;
    ok = "FAIL";
    {
        char all[OC_CONFIG_FILE_MAX];
        int n = lib_config_get_all(all, sizeof(all));
        int has_custom = 0, has_url = 0;
        if (n > 0) {
            if (strchr(all, 't') != NULL) {
                /* simple containment checks */
                const char *p = all;
                has_custom = 0; has_url = 0;
                while (*p) {
                    if (strncmp(p, "test_key=hello world", 20) == 0) has_custom = 1;
                    if (strncmp(p, "update_url=", 11) == 0) has_url = 1;
                    p++;
                }
            }
        }
        if (has_custom && has_url) { ok = "PASS"; pass++; }
    }
    screen_console_puts("[config_test] get_all contains test_key and update_url: "); screen_console_puts(ok); screen_console_puts("\n");

    /* 6. delete the file -> read reports missing */
    total++;
    ok = "FAIL";
    if (fs_vfs_unlink(OC_CONFIG_PATH) == 0 &&
        lib_config_read(OC_CONFIG_KEY_URL, val, sizeof(val)) == OC_CONFIG_E_NOFILE)
        { ok = "PASS"; pass++; }
    screen_console_puts("[config_test] delete file -> read reports missing: "); screen_console_puts(ok); screen_console_puts("\n");

    /* 7. WP-10u: all four well-known keys exist after restore */
    total++;
    ok = "FAIL";
    if (lib_config_restore_defaults() == 0 &&
        lib_config_read(OC_CONFIG_KEY_URL, val, sizeof(val)) == 0 &&
        strcmp(val, lib_config_default_url()) == 0 &&
        lib_config_read(OC_CONFIG_KEY_AUTOCHECK, val, sizeof(val)) == 0 &&
        strcmp(val, "no") == 0 &&
        lib_config_read(OC_CONFIG_KEY_PACKAGE_URL, val, sizeof(val)) == 0 &&
        strcmp(val, OC_CONFIG_DEFAULT_PACKAGE_URL) == 0 &&
        lib_config_read(OC_CONFIG_KEY_ONLINE_UPDATE, val, sizeof(val)) == 0 &&
        strcmp(val, "yes") == 0) { ok = "PASS"; pass++; }
    screen_console_puts("[config_test] restore defaults (4 keys): "); screen_console_puts(ok); screen_console_puts("\n");

    /* 8. WP-10u: package_url write/read roundtrip */
    total++;
    ok = "FAIL";
    if (lib_config_write(OC_CONFIG_KEY_PACKAGE_URL,
                        "http://10.0.2.2:8008/packages") == 0 &&
        lib_config_read(OC_CONFIG_KEY_PACKAGE_URL, val, sizeof(val)) == 0 &&
        strcmp(val, "http://10.0.2.2:8008/packages") == 0)
        { ok = "PASS"; pass++; }
    screen_console_puts("[config_test] package_url write/read: "); screen_console_puts(ok); screen_console_puts("\n");

    /* 9. WP-10u: online_update yes/no roundtrip + invalid rejected */
    total++;
    ok = "FAIL";
    if (lib_config_write(OC_CONFIG_KEY_ONLINE_UPDATE, "no") == 0 &&
        lib_config_read(OC_CONFIG_KEY_ONLINE_UPDATE, val, sizeof(val)) == 0 &&
        strcmp(val, "no") == 0 &&
        lib_config_online_update_enabled() == 0 &&
        lib_config_write(OC_CONFIG_KEY_ONLINE_UPDATE, "YES") == 0 &&
        lib_config_online_update_enabled() == 1 &&
        lib_config_write(OC_CONFIG_KEY_ONLINE_UPDATE, "maybe") == OC_CONFIG_E_ARGS)
        { ok = "PASS"; pass++; }
    screen_console_puts("[config_test] online_update yes/no/invalid: "); screen_console_puts(ok); screen_console_puts("\n");

    /* 10. restore defaults again (leave the system in a clean state) */
    total++;
    ok = "FAIL";
    if (lib_config_restore_defaults() == 0 &&
        lib_config_read(OC_CONFIG_KEY_ONLINE_UPDATE, val, sizeof(val)) == 0 &&
        strcmp(val, "yes") == 0) { ok = "PASS"; pass++; }
    screen_console_puts("[config_test] final restore: "); screen_console_puts(ok); screen_console_puts("\n");

    char line[64]; char num[12];
    strcpy(line, "[config_test] ");
    u64_to_str((u64)pass, num); strcat(line, num);
    strcat(line, "/");
    u64_to_str((u64)total, num); strcat(line, num);
    strcat(line, pass == total ? " PASS" : " FAIL");
    screen_console_puts(line);
    screen_console_puts("\n");
    return (pass == total) ? 0 : 1;
}
