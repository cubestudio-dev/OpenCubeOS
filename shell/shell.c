/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-03 / WP-05/WP-07
 * File: kernel/shell.c
 * Purpose: Shell command registration system + WP-05/WP-07 shell enhancements.
 *
 * WP-03 features (unchanged):
 *   - shell_register_command / shell_unregister_command / shell_execute
 *   - command table (max SHELL_MAX_COMMANDS)
 *
 * WP-05/WP-07 features (new):
 *   - environment variables ($VAR expansion + `export` builtin)
 *   - aliases (`alias` / `unalias` builtins)
 *   - current working directory (cd / pwd, used by path resolver)
 *   - full tokenizer (handles single and double quotes)
 *   - command chains (;, &&, ||)
 *   - I/O redirection (>, >>, <)
 *   - pipes (cmd1 | cmd2)
 *   - background (&)  (no-op since we have no shell subprocess model)
 *   - basic wildcards (* and ?)
 *   - shell_execute_line(): the full parser/executor
 *
 * Output capture works by temporarily replacing the console hook (which is
 * a single fn+ctx pair in ext.c). When capturing, our hook:
 *   - forwards the char to the previously-installed hook (so serial output
 *     keeps working),
 *   - appends the char to the capture buffer,
 *   - returns 1 so the framebuffer draw is skipped.
 */
#include "shell.h"
#include "screen_console.h"
#include "l1_ext.h"
#include "mem_heap.h"
#include "lib_string.h"
#include "fs_vfs.h"
#include "editor.h"

/* ================================================================== *
 * WP-03 command table (unchanged)
 * ================================================================== */

typedef struct {
    char name[32];
    shell_cmd_fn handler;
    char help[80];
    char wp[24];        /* WP-10d-fix2: work-package tag for `help -w` */
    int in_use;
} shell_cmd_t;

static shell_cmd_t g_commands[SHELL_MAX_COMMANDS];
static int g_command_count = 0; /* docs-sync FIX: live count for boot banner */

int shell_command_count(void) {
    return g_command_count;
}

int shell_register_command_ex(const char *name, shell_cmd_fn handler,
                              const char *help, const char *wp) {
    if (!name || !handler) return -1;
    /* Check if already registered (replace). */
    for (int i = 0; i < SHELL_MAX_COMMANDS; i++) {
        if (g_commands[i].in_use && strcmp(g_commands[i].name, name) == 0) {
            g_commands[i].handler = handler;
            if (help) strncpy(g_commands[i].help, help, sizeof(g_commands[i].help) - 1);
            if (wp) strncpy(g_commands[i].wp, wp, sizeof(g_commands[i].wp) - 1);
            return 0;
        }
    }
    /* Find a free slot. */
    for (int i = 0; i < SHELL_MAX_COMMANDS; i++) {
        if (!g_commands[i].in_use) {
            strncpy(g_commands[i].name, name, sizeof(g_commands[i].name) - 1);
            g_commands[i].name[sizeof(g_commands[i].name) - 1] = 0;
            g_commands[i].handler = handler;
            if (help) {
                strncpy(g_commands[i].help, help, sizeof(g_commands[i].help) - 1);
                g_commands[i].help[sizeof(g_commands[i].help) - 1] = 0;
            } else {
                g_commands[i].help[0] = 0;
            }
            if (wp) {
                strncpy(g_commands[i].wp, wp, sizeof(g_commands[i].wp) - 1);
                g_commands[i].wp[sizeof(g_commands[i].wp) - 1] = 0;
            } else {
                strcpy(g_commands[i].wp, "WP-03");
            }
            g_commands[i].in_use = 1;
            g_command_count++; /* docs-sync FIX: live count for boot banner */
            return 0;
        }
    }
    /* BUG-004 FIX: Print a warning when the command table is full,
     * so silent registration failures are visible. */
    screen_console_puts("[shell] WARNING: command table full, cannot register '");
    screen_console_puts(name);
    screen_console_puts("'\n");
    return -2;  /* table full */
}

int shell_register_command(const char *name, shell_cmd_fn handler, const char *help) {
    return shell_register_command_ex(name, handler, help, "WP-03");
}

int shell_unregister_command(const char *name) {
    if (!name) return -1;
    for (int i = 0; i < SHELL_MAX_COMMANDS; i++) {
        if (g_commands[i].in_use && strcmp(g_commands[i].name, name) == 0) {
            g_commands[i].in_use = 0;
            return 0;
        }
    }
    return -2;
}

/* Look up a command by name. Returns the handler or NULL. */
static shell_cmd_fn shell_lookup(const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < SHELL_MAX_COMMANDS; i++) {
        if (g_commands[i].in_use && strcmp(g_commands[i].name, name) == 0) {
            return g_commands[i].handler;
        }
    }
    return NULL;
}

/* Execute a single command word + args string. Returns 1 if found, 0 if not. */
static int shell_run_simple(const char *cmd, const char *args) {
    shell_cmd_fn fn = shell_lookup(cmd);
    if (!fn) {
        /* BUG-0092 FIX (A16-5): give L1-registered builtins (see
         * l1_wp8cd.c shell_register_builtin) a real dispatch path:
         * without this, names registered through the documented L1 API
         * were stored in an orphaned table and "not found" at oc>. */
        extern int l1_wp8cd_builtin_exec(const char *cmd, const char *args);
        if (l1_wp8cd_builtin_exec(cmd, args)) return 1;
        return 0;
    }
    fn(args);
    return 1;
}

int shell_execute(const char *line) {
    /* Extract the command word. */
    char cmd[32];
    int i = 0;
    while (line[i] && line[i] != ' ' && line[i] != '\t' && i < 31) {
        cmd[i] = line[i];
        i++;
    }
    cmd[i] = 0;

    /* Skip whitespace to get args. */
    const char *args = line + i;
    while (*args == ' ' || *args == '\t') args++;

    return shell_run_simple(cmd, args);
}

/* ------------------------------------------------------------------ *
 * WP-10d-fix2: structured help listings
 * ================================================================== */

/* Canonical work-package display order + descriptions.  Tags not in
 * this table (if an L1 driver registers its own) are appended after the
 * known ones, sorted A-Z, with no description. */
static const struct { const char *tag; const char *desc; } g_wp_order[] = {
    { "WP-01",           "boot, framebuffer, text" },
    { "WP-02",           "interrupts, timer, keyboard" },
    { "WP-03",           "shell core" },
    { "WP-04",           "memory, scheduler" },
    { "WP-05",           "VFS, FAT32, ATA" },
    { "WP-06",           "network stack" },
    { "WP-07",           "block layer" },
    { "WP-08a",          "ring-3 user mode" },
    { "WP-08b",          "ELF, dynamic linking" },
    { "WP-08cd",         "user-space shell + toolset" },
    { "WP-09",           "secure transport" },
    { "WP-09-fix5",      "config, update check" },
    { "WP-10a",          "storage drivers" },
    { "WP-10b",          "NIC drivers" },
    { "WP-10c",          "sound drivers" },
    { "WP-10d",          "USB host stack" },
    { "WP-10u",          "in-system update" },
    { "WP-10c-selfhost", "rule-9 self-hosting" },
    { "WP-10d-fix2",     "power management, help" },
};

/* Case-insensitive name compare (A-Z means alphabetical to a user). */
static int shell_wp_name_cmp(const char *a, const char *b) {
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return (int)(u8)ca - (int)(u8)cb;
        a++; b++;
    }
    return (int)(u8)*a - (int)(u8)*b;
}

/* Build an index of in-use commands sorted by name.  Returns the count. */
static int shell_wp_sorted_indices(int *idx, int cap) {
    int n = 0;
    for (int i = 0; i < SHELL_MAX_COMMANDS && n < cap; i++)
        if (g_commands[i].in_use) idx[n++] = i;
    /* insertion sort (n <= 192; keeps this simple and allocation-free) */
    for (int i = 1; i < n; i++) {
        int cur = idx[i];
        int j = i - 1;
        while (j >= 0 && shell_wp_name_cmp(g_commands[idx[j]].name,
                                     g_commands[cur].name) > 0) {
            idx[j + 1] = idx[j];
            j--;
        }
        idx[j + 1] = cur;
    }
    return n;
}

/* Longest in-use command name (for column alignment). */
static int shell_wp_max_name_len(const int *idx, int n) {
    int w = 12; /* spec sample width */
    for (int i = 0; i < n; i++) {
        int l = 0;
        while (g_commands[idx[i]].name[l]) l++;
        if (l > w) w = l;
    }
    return w;
}

static void shell_wp_print_entry(const shell_cmd_t *c, int width) {
    screen_console_puts("  ");
    screen_console_puts(c->name);
    int l = 0;
    while (c->name[l]) l++;
    for (int p = l; p < width + 1; p++) screen_console_putc(' ');
    screen_console_puts("- ");
    screen_console_puts(c->help[0] ? c->help : "(no description)");
    screen_console_putc('\n');
}

void shell_list_commands_a_z(void) {
    int idx[SHELL_MAX_COMMANDS];
    int n = shell_wp_sorted_indices(idx, (int)SHELL_MAX_COMMANDS);
    int w = shell_wp_max_name_len(idx, n);
    screen_console_puts("Available commands (A-Z):\n");
    for (int i = 0; i < n; i++)
        shell_wp_print_entry(&g_commands[idx[i]], w);
    screen_console_puts("Total: ");
    char num[12];
    u64_to_str((u64)n, num);
    screen_console_puts(num);
    screen_console_puts(" commands. Use 'help -w' for the work-package view.\n");
}

void shell_list_commands_by_wp(void) {
    int idx[SHELL_MAX_COMMANDS];
    int n = shell_wp_sorted_indices(idx, (int)SHELL_MAX_COMMANDS);
    int w = shell_wp_max_name_len(idx, n);
    screen_console_puts("Commands by work package:\n");
    int shown = 0;
    /* known tags in canonical order */
    for (int k = 0; k < (int)(sizeof(g_wp_order) / sizeof(g_wp_order[0])); k++) {
        int count = 0;
        for (int i = 0; i < n; i++)
            if (strcmp(g_commands[idx[i]].wp, g_wp_order[k].tag) == 0) count++;
        if (count == 0) continue;
        screen_console_puts("\n=== ");
        screen_console_puts(g_wp_order[k].tag);
        screen_console_puts(" (");
        screen_console_puts(g_wp_order[k].desc);
        screen_console_puts(") ===\n");
        for (int i = 0; i < n; i++)
            if (strcmp(g_commands[idx[i]].wp, g_wp_order[k].tag) == 0)
                shell_wp_print_entry(&g_commands[idx[i]], w);
        shown += count;
    }
    /* unknown tags (L1 drivers may register their own): append A-Z */
    for (int i = 0; i < n; i++) {
        int known = 0;
        for (int k = 0; k < (int)(sizeof(g_wp_order) / sizeof(g_wp_order[0])); k++)
            if (strcmp(g_commands[idx[i]].wp, g_wp_order[k].tag) == 0) { known = 1; break; }
        if (known) continue;
        if (shown == 0 || g_commands[idx[i - 1]].wp[0] == 0 ||
            strcmp(g_commands[idx[i - 1]].wp, g_commands[idx[i]].wp) != 0) {
            /* first command of an unknown tag: header once per tag */
            int first = 1;
            for (int j = 0; j < i; j++) {
                int kknown = 0;
                for (int k = 0; k < (int)(sizeof(g_wp_order) / sizeof(g_wp_order[0])); k++)
                    if (strcmp(g_commands[idx[j]].wp, g_wp_order[k].tag) == 0) { kknown = 1; break; }
                if (!kknown && strcmp(g_commands[idx[j]].wp,
                                         g_commands[idx[i]].wp) == 0) { first = 0; break; }
            }
            if (first) {
                screen_console_puts("\n=== ");
                screen_console_puts(g_commands[idx[i]].wp);
                screen_console_puts(" ===\n");
            }
        }
        shell_wp_print_entry(&g_commands[idx[i]], w);
        shown++;
    }
    screen_console_puts("\nTotal: ");
    char num[12];
    u64_to_str((u64)shown, num);
    screen_console_puts(num);
    screen_console_puts(" commands.");
    screen_console_putc('\n');
}

/* Retired WP-03 listing: now the A-Z view (WP-10d-fix2 rewrite). */
void shell_print_help(void) {
    shell_list_commands_a_z();
}

/* ================================================================== *
 * WP-05: environment variables
 * ================================================================== */

typedef struct {
    char name[SHELL_ENV_NAME_LEN];
    char value[SHELL_ENV_VALUE_LEN];
    int in_use;
} shell_env_t;

static shell_env_t g_env[SHELL_MAX_ENV];

int shell_setenv(const char *name, const char *value) {
    if (!name || !name[0]) return -1;
    /* Replace if exists. */
    for (int i = 0; i < SHELL_MAX_ENV; i++) {
        if (g_env[i].in_use && strcmp(g_env[i].name, name) == 0) {
            if (value) {
                strncpy(g_env[i].value, value, SHELL_ENV_VALUE_LEN - 1);
                g_env[i].value[SHELL_ENV_VALUE_LEN - 1] = 0;
            } else {
                g_env[i].value[0] = 0;
            }
            return 0;
        }
    }
    /* New slot. */
    for (int i = 0; i < SHELL_MAX_ENV; i++) {
        if (!g_env[i].in_use) {
            strncpy(g_env[i].name, name, SHELL_ENV_NAME_LEN - 1);
            g_env[i].name[SHELL_ENV_NAME_LEN - 1] = 0;
            if (value) {
                strncpy(g_env[i].value, value, SHELL_ENV_VALUE_LEN - 1);
                g_env[i].value[SHELL_ENV_VALUE_LEN - 1] = 0;
            } else {
                g_env[i].value[0] = 0;
            }
            g_env[i].in_use = 1;
            return 0;
        }
    }
    return -2;  /* table full */
}

const char *shell_getenv(const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < SHELL_MAX_ENV; i++) {
        if (g_env[i].in_use && strcmp(g_env[i].name, name) == 0) {
            return g_env[i].value;
        }
    }
    return NULL;
}

/* `export VAR=value` builtin. Also accepts `export VAR` (no value, sets to ""). */
static int shell_cmd_export(const char *args) {
    if (!args || !args[0]) {
        /* List all env vars. */
        screen_console_puts("Environment variables:\n");
        for (int i = 0; i < SHELL_MAX_ENV; i++) {
            if (g_env[i].in_use) {
                screen_console_puts("  ");
                screen_console_puts(g_env[i].name);
                screen_console_puts("=");
                screen_console_puts(g_env[i].value);
                screen_console_putc('\n');
            }
        }
        return 0;
    }
    /* Split on '='. */
    const char *eq = args;
    while (*eq && *eq != '=') eq++;
    char name[SHELL_ENV_NAME_LEN];
    int nlen = (int)(eq - args);
    if (nlen >= SHELL_ENV_NAME_LEN) nlen = SHELL_ENV_NAME_LEN - 1;
    memcpy(name, args, (usize)nlen);
    name[nlen] = 0;
    const char *value = (*eq == '=') ? eq + 1 : "";
    if (shell_setenv(name, value) < 0) {
        screen_console_puts("export: table full\n");
        return 1;
    }
    return 0;
}

/* `set` builtin - alias for `export` without args (list vars). */
static int shell_cmd_set(const char *args) {
    (void)args;
    return shell_cmd_export("");
}

/* ================================================================== *
 * WP-05: aliases
 * ================================================================== */

typedef struct {
    char name[SHELL_ALIAS_NAME_LEN];
    char target[SHELL_ALIAS_TARGET_LEN];
    int in_use;
} shell_alias_t;

static shell_alias_t g_aliases[SHELL_MAX_ALIASES];

int shell_register_alias(const char *alias, const char *target) {
    if (!alias || !alias[0] || !target) return -1;
    /* Replace if exists. */
    for (int i = 0; i < SHELL_MAX_ALIASES; i++) {
        if (g_aliases[i].in_use && strcmp(g_aliases[i].name, alias) == 0) {
            strncpy(g_aliases[i].target, target, SHELL_ALIAS_TARGET_LEN - 1);
            g_aliases[i].target[SHELL_ALIAS_TARGET_LEN - 1] = 0;
            return 0;
        }
    }
    for (int i = 0; i < SHELL_MAX_ALIASES; i++) {
        if (!g_aliases[i].in_use) {
            strncpy(g_aliases[i].name, alias, SHELL_ALIAS_NAME_LEN - 1);
            g_aliases[i].name[SHELL_ALIAS_NAME_LEN - 1] = 0;
            strncpy(g_aliases[i].target, target, SHELL_ALIAS_TARGET_LEN - 1);
            g_aliases[i].target[SHELL_ALIAS_TARGET_LEN - 1] = 0;
            g_aliases[i].in_use = 1;
            return 0;
        }
    }
    return -2;
}

int shell_unregister_alias(const char *alias) {
    if (!alias) return -1;
    for (int i = 0; i < SHELL_MAX_ALIASES; i++) {
        if (g_aliases[i].in_use && strcmp(g_aliases[i].name, alias) == 0) {
            g_aliases[i].in_use = 0;
            return 0;
        }
    }
    return -2;
}

static const char *shell_lookup_alias(const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < SHELL_MAX_ALIASES; i++) {
        if (g_aliases[i].in_use && strcmp(g_aliases[i].name, name) == 0) {
            return g_aliases[i].target;
        }
    }
    return NULL;
}

/* `alias` builtin.
 *   alias                - list all
 *   alias name='value'   - set
 *   alias name value     - set (alternative form) */
static int shell_cmd_alias(const char *args) {
    if (!args || !args[0]) {
        /* List all. */
        screen_console_puts("Aliases:\n");
        for (int i = 0; i < SHELL_MAX_ALIASES; i++) {
            if (g_aliases[i].in_use) {
                screen_console_puts("  ");
                screen_console_puts(g_aliases[i].name);
                screen_console_puts("='");
                screen_console_puts(g_aliases[i].target);
                screen_console_puts("'\n");
            }
        }
        return 0;
    }
    /* Parse "name='value'" or "name=value" or "name value". */
    char name[SHELL_ALIAS_NAME_LEN];
    int ni = 0;
    while (args[ni] && args[ni] != '=' && args[ni] != ' ' && ni < SHELL_ALIAS_NAME_LEN - 1) {
        name[ni] = args[ni];
        ni++;
    }
    name[ni] = 0;
    const char *p = args + ni;
    if (*p == '=') {
        p++;
        /* Strip surrounding single quotes if present. */
        if (*p == '\'') {
            p++;
            /* Find closing quote. */
            const char *end = p;
            while (*end && *end != '\'') end++;
            int tlen = (int)(end - p);
            char tmp[SHELL_ALIAS_TARGET_LEN];
            if (tlen >= SHELL_ALIAS_TARGET_LEN) tlen = SHELL_ALIAS_TARGET_LEN - 1;
            memcpy(tmp, p, (usize)tlen);
            tmp[tlen] = 0;
            return shell_register_alias(name, tmp) < 0 ? 1 : 0;
        } else {
            return shell_register_alias(name, p) < 0 ? 1 : 0;
        }
    } else if (*p == ' ') {
        while (*p == ' ') p++;
        if (!*p) {
            /* `alias name` with no value - print the alias if exists. */
            const char *t = shell_lookup_alias(name);
            if (t) {
                screen_console_puts(name);
                screen_console_puts("='");
                screen_console_puts(t);
                screen_console_puts("'\n");
                return 0;
            }
            screen_console_puts("alias: not found: ");
            screen_console_puts(name);
            screen_console_putc('\n');
            return 1;
        }
        return shell_register_alias(name, p) < 0 ? 1 : 0;
    } else {
        /* `alias name` - print. */
        const char *t = shell_lookup_alias(name);
        if (t) {
            screen_console_puts(name);
            screen_console_puts("='");
            screen_console_puts(t);
            screen_console_puts("'\n");
            return 0;
        }
        screen_console_puts("alias: not found: ");
        screen_console_puts(name);
        screen_console_putc('\n');
        return 1;
    }
}

/* `unalias name` builtin. */
static int shell_cmd_unalias(const char *args) {
    if (!args || !args[0]) {
        screen_console_puts("usage: unalias <name>\n");
        return 1;
    }
    if (shell_unregister_alias(args) < 0) {
        screen_console_puts("unalias: not found\n");
        return 1;
    }
    return 0;
}

/* ================================================================== *
 * WP-05: current working directory
 * ================================================================== */

static char g_cwd[SHELL_CWD_LEN] = "/";

const char *shell_get_cwd(void) {
    return g_cwd;
}

/* Normalize an absolute path in-place. Handles "." and ".." and "//".
 * Returns 0 on success, -1 on overflow. */
static int shell_normalize_path(char *path, int maxlen) {
    if (!path || maxlen < 2) return -1;
    if (path[0] != '/') return -1;

    /* In-place normalization using a write pointer. */
    char comps[SHELL_CWD_LEN / 2][VFS_NAME_LEN];
    int ncomp = 0;
    int i = 0;
    while (path[i]) {
        while (path[i] == '/') i++;
        if (!path[i]) break;
        char comp[VFS_NAME_LEN];
        int ci = 0;
        while (path[i] && path[i] != '/' && ci < VFS_NAME_LEN - 1) {
            comp[ci++] = path[i++];
        }
        comp[ci] = 0;
        if (strcmp(comp, ".") == 0) {
            /* skip */
        } else if (strcmp(comp, "..") == 0) {
            if (ncomp > 0) ncomp--;
        } else {
            if (ncomp >= (int)(sizeof(comps) / sizeof(comps[0]))) return -1;
            strncpy(comps[ncomp], comp, VFS_NAME_LEN - 1);
            comps[ncomp][VFS_NAME_LEN - 1] = 0;
            ncomp++;
        }
    }
    /* Rebuild. */
    int p = 0;
    path[p++] = '/';
    for (int c = 0; c < ncomp; c++) {
        int len = (int)strlen(comps[c]);
        if (p + len + 1 >= maxlen) return -1;
        memcpy(path + p, comps[c], (usize)len);
        p += len;
        if (c < ncomp - 1) path[p++] = '/';
    }
    path[p] = 0;
    return 0;
}

int shell_set_cwd(const char *path) {
    if (!path || !path[0]) return -1;
    char newcwd[SHELL_CWD_LEN];
    if (path[0] == '/') {
        /* Absolute. */
        strncpy(newcwd, path, SHELL_CWD_LEN - 1);
        newcwd[SHELL_CWD_LEN - 1] = 0;
    } else {
        /* Relative - prepend cwd. */
        int cwdlen = (int)strlen(g_cwd);
        if (cwdlen + 1 + (int)strlen(path) + 1 > SHELL_CWD_LEN) return -1;
        strcpy(newcwd, g_cwd);
        if (g_cwd[cwdlen - 1] != '/') {
            newcwd[cwdlen++] = '/';
            newcwd[cwdlen] = 0;
        }
        strcpy(newcwd + cwdlen, path);
    }
    if (shell_normalize_path(newcwd, SHELL_CWD_LEN) < 0) return -1;
    /* Verify the path exists in the VFS. */
    fs_vfs_stat_t st;
    if (fs_vfs_stat(newcwd, &st) < 0) return -2;
    if (st.type != VFS_TYPE_DIR) return -3;
    strcpy(g_cwd, newcwd);
    return 0;
}

int shell_resolve_path(const char *path, char *out, int out_len) {
    if (!path || !out || out_len < 2) return -1;
    if (path[0] == '/') {
        strncpy(out, path, out_len - 1);
        out[out_len - 1] = 0;
    } else {
        int cwdlen = (int)strlen(g_cwd);
        if (cwdlen + 1 + (int)strlen(path) + 1 > out_len) return -1;
        strcpy(out, g_cwd);
        if (g_cwd[cwdlen - 1] != '/') {
            out[cwdlen++] = '/';
            out[cwdlen] = 0;
        }
        strcpy(out + cwdlen, path);
    }
    return shell_normalize_path(out, out_len);
}

/* Static buffer for the convenience wrapper. */
/* P0-8 FIX: provide two static buffers so callers that resolve two paths
 * in one expression (e.g. shell_cmd_mv: rsrc = resolve(src); rdst = resolve(dst))
 * don't clobber each other. The caller picks slot 0 or 1. */
static char g_resolve_buf[2][SHELL_CWD_LEN];
static int g_resolve_slot = 0;

const char *shell_resolve_path_static(const char *path) {
    /* Alternate between the two buffers so consecutive calls return
     * distinct storage. */
    int slot = g_resolve_slot;
    g_resolve_slot = (g_resolve_slot + 1) % 2;
    if (shell_resolve_path(path, g_resolve_buf[slot], SHELL_CWD_LEN) < 0) {
        return path;  /* fall back to the raw input */
    }
    return g_resolve_buf[slot];
}

/* ================================================================== *
 * WP-05: tokenizer
 * ================================================================== */

typedef enum {
    TOK_WORD,    /* a command or argument */
    TOK_SEMI,    /* ; */
    TOK_PIPE,    /* | */
    TOK_AND,     /* && */
    TOK_OR,      /* || */
    TOK_AMP,     /* & */
    TOK_LT,      /* < */
    TOK_GT,      /* > */
    TOK_APPEND,  /* >> */
} tok_kind_t;

#define TOK_QUOTED_SINGLE 1   /* no env expansion, no wildcard */
#define TOK_QUOTED_DOUBLE 2   /* env expansion yes, wildcard no */

typedef struct {
    tok_kind_t kind;
    char text[VFS_PATH_LEN + 32];
    int flags;  /* for TOK_WORD: bitmask of TOK_QUOTED_* */
} token_t;

/* Skip whitespace (spaces and tabs). */
static const char *skip_ws(const char *p) {
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

/* Is c an operator char? */
static int is_op_char(char c) {
    return c == ';' || c == '|' || c == '&' || c == '<' || c == '>';
}

/* Tokenize the input line. Returns number of tokens (>=0) or -1 on overflow.
 * Operators are emitted as their own tokens; words are emitted as a single
 * token even when they contain quoted sections (quotes are stripped). */
static int shell_tokenize(const char *line, token_t *out, int max) {
    int n = 0;
    const char *p = line;
    while (*p) {
        p = skip_ws(p);
        if (!*p) break;
        if (n >= max) return -1;

        /* Operator? */
        if (*p == ';') { out[n].kind = TOK_SEMI; out[n].flags = 0; out[n].text[0] = 0; n++; p++; continue; }
        if (*p == '|') {
            if (p[1] == '|') { out[n].kind = TOK_OR;  out[n].flags = 0; out[n].text[0] = 0; n++; p += 2; }
            else             { out[n].kind = TOK_PIPE;out[n].flags = 0; out[n].text[0] = 0; n++; p++; }
            continue;
        }
        if (*p == '&') {
            if (p[1] == '&') { out[n].kind = TOK_AND; out[n].flags = 0; out[n].text[0] = 0; n++; p += 2; }
            else             { out[n].kind = TOK_AMP; out[n].flags = 0; out[n].text[0] = 0; n++; p++; }
            continue;
        }
        if (*p == '<') { out[n].kind = TOK_LT; out[n].flags = 0; out[n].text[0] = 0; n++; p++; continue; }
        if (*p == '>') {
            if (p[1] == '>') { out[n].kind = TOK_APPEND; out[n].flags = 0; out[n].text[0] = 0; n++; p += 2; }
            else             { out[n].kind = TOK_GT;     out[n].flags = 0; out[n].text[0] = 0; n++; p++; }
            continue;
        }

        /* Word. May contain quoted sections. */
        out[n].kind = TOK_WORD;
        out[n].flags = 0;
        int ti = 0;
        while (*p && !(*p == ' ' || *p == '\t') && !is_op_char(*p)) {
            if (*p == '\'') {
                out[n].flags |= TOK_QUOTED_SINGLE;
                p++;
                while (*p && *p != '\'') {
                    if (ti < (int)sizeof(out[n].text) - 1) out[n].text[ti++] = *p;
                    p++;
                }
                if (*p == '\'') p++;
            } else if (*p == '"') {
                out[n].flags |= TOK_QUOTED_DOUBLE;
                p++;
                while (*p && *p != '"') {
                    if (ti < (int)sizeof(out[n].text) - 1) out[n].text[ti++] = *p;
                    p++;
                }
                if (*p == '"') p++;
            } else {
                if (ti < (int)sizeof(out[n].text) - 1) out[n].text[ti++] = *p;
                p++;
            }
        }
        out[n].text[ti] = 0;
        n++;
    }
    return n;
}

/* ================================================================== *
 * WP-05: env var expansion in a single word token
 *
 * Replaces $VAR (and ${VAR}) with the value of VAR. Single-quoted tokens
 * are not expanded. The output is written back into the token's text.
 * ================================================================== */

static void shell_expand_env_token(token_t *tok) {
    if (tok->kind != TOK_WORD) return;
    if (tok->flags & TOK_QUOTED_SINGLE) return;  /* no expansion in single quotes */
    if (!tok->text[0]) return;

    char buf[VFS_PATH_LEN + 32];
    int bi = 0;
    int i = 0;
    int blen = (int)sizeof(buf);
    while (tok->text[i] && bi < blen - 1) {
        if (tok->text[i] == '$') {
            i++;
            int braced = 0;
            if (tok->text[i] == '{') { braced = 1; i++; }
            char name[SHELL_ENV_NAME_LEN];
            int ni = 0;
            while (tok->text[i] &&
                   ((tok->text[i] >= 'A' && tok->text[i] <= 'Z') ||
                    (tok->text[i] >= 'a' && tok->text[i] <= 'z') ||
                    (tok->text[i] >= '0' && tok->text[i] <= '9') ||
                    tok->text[i] == '_') &&
                   ni < SHELL_ENV_NAME_LEN - 1) {
                name[ni++] = tok->text[i++];
            }
            name[ni] = 0;
            if (braced && tok->text[i] == '}') i++;
            if (ni > 0) {
                const char *val = shell_getenv(name);
                if (val) {
                    while (*val && bi < blen - 1) buf[bi++] = *val++;
                }
            } else {
                /* Lone '$' - emit it literally. */
                if (bi < blen - 1) buf[bi++] = '$';
            }
        } else {
            buf[bi++] = tok->text[i++];
        }
    }
    buf[bi] = 0;
    strcpy(tok->text, buf);
}

/* ================================================================== *
 * WP-05: wildcard matching and expansion
 * ================================================================== */

/* Match a single path component (no '/') against the pattern. */
static int shell_glob_match(const char *pat, const char *str) {
    /* Iterative with backtracking on '*'. */
    const char *p = pat;
    const char *s = str;
    const char *star_p = NULL;
    const char *star_s = NULL;
    while (*s) {
        if (*p == '*') {
            star_p = p++;
            star_s = s;
        } else if (*p == '?' || *p == *s) {
            p++;
            s++;
        } else if (star_p) {
            p = star_p + 1;
            s = ++star_s;
        } else {
            return 0;
        }
    }
    while (*p == '*') p++;
    return *p == 0;
}

/* Expand wildcards in a word token. Returns the number of replacement tokens
 * written into out[] (which must be at least max_out entries). 0 means no
 * wildcard expansion happened (the token is left untouched in out[0]).
 * Negative on error. The first match always sorts first by readdir order. */
static int shell_expand_wildcards(const token_t *tok, token_t *out, int max_out) {
    if (tok->kind != TOK_WORD) return 0;
    if (tok->flags & (TOK_QUOTED_SINGLE | TOK_QUOTED_DOUBLE)) {
        /* WP-09-FIX BUG-005: quoted words are not wildcard-expanded but
         * MUST still be passed through. The contract comment below says
         * "the token is left untouched in out[0]" — the old code returned
         * 0 without filling out[0], and the caller copies ne tokens, so
         * quoted arguments silently vanished (echo 'ab' printed an empty
         * line; alias p='echo hi' never registered; export V1='a b'
         * failed). */
        if (max_out < 1) return -1;
        out[0] = *tok;
        return 1;
    }
    if (!tok->text[0]) return 0;

    /* Find first wildcard char. */
    int has_wc = 0;
    for (int i = 0; tok->text[i]; i++) {
        if (tok->text[i] == '*' || tok->text[i] == '?') { has_wc = 1; break; }
    }
    if (!has_wc) {
        if (max_out < 1) return -1;
        out[0] = *tok;
        return 1;
    }

    /* Split into dir + pattern. */
    char dirpath[VFS_PATH_LEN];
    char pat[VFS_PATH_LEN];
    int last_slash = -1;
    int tlen = (int)strlen(tok->text);
    for (int i = 0; i < tlen; i++) {
        if (tok->text[i] == '/') last_slash = i;
    }
    if (last_slash < 0) {
        /* No directory - use cwd. */
        strcpy(dirpath, shell_get_cwd());
        /* P0fix2 BUG-0028 (A15-1): env/alias expansion can grow a single
         * token to 287 bytes while pat holds VFS_PATH_LEN (256) — the old
         * unchecked strcpy smashed up to 31 stack bytes here. */
        if (tlen >= (int)sizeof(pat)) return -1;
        strcpy(pat, tok->text);
    } else {
        if (last_slash >= VFS_PATH_LEN - 1) return -1;
        memcpy(dirpath, tok->text, (usize)last_slash + 1);
        dirpath[last_slash + 1] = 0;
        /* If the path was like "/foo/x.c" with a wildcard, dirpath is the
         * parent dir (with trailing slash). Normalize it.
         * BUG-0084 FIX (A15-5): the old code called
         *   shell_resolve_path(dirpath, dirpath, ...)
         * with the SAME buffer as input and output. For RELATIVE
         * patterns such as "src" SLASH "*dot-c" the resolver first does
         * strcpy(out, g_cwd) - destroying the very input it is about
         * to read on the next line - so every relative wildcard
         * expanded to garbage (ls/mv/cp/cd with a "dir STAR" pattern
         * all broke). Resolve through a scratch copy instead. */
        {
            char dir_in[VFS_PATH_LEN];
            strcpy(dir_in, dirpath);
            if (shell_resolve_path(dir_in, dirpath, VFS_PATH_LEN) < 0) return -1;
        }
        /* P0fix2 BUG-0028 (A15-1): the same bound applies to the pattern
         * after the last slash. */
        if (tlen - last_slash - 1 >= (int)sizeof(pat)) return -1;
        strcpy(pat, tok->text + last_slash + 1);
    }

    /* Read dir entries and match. */
    int count = 0;
    for (int idx = 0; ; idx++) {
        fs_vfs_dirent_t e;
        if (fs_vfs_readdir(dirpath, idx, &e) < 0) break;
        if (shell_glob_match(pat, e.name)) {
            if (count >= max_out) return -1;
            /* Build the full path. */
            out[count].kind = TOK_WORD;
            out[count].flags = 0;  /* expanded - no further processing */
            int dl = (int)strlen(dirpath);
            int el = (int)strlen(e.name);
            if (dl + el + 1 >= (int)sizeof(out[count].text)) {
                /* Skip on overflow. */
                continue;
            }
            strcpy(out[count].text, dirpath);
            if (dl > 0 && dirpath[dl - 1] != '/') {
                out[count].text[dl++] = '/';
                out[count].text[dl] = 0;
            }
            strcpy(out[count].text + dl, e.name);
            (void)el;
            count++;
        }
    }
    if (count == 0) {
        /* No matches - leave the token as-is. */
        if (max_out < 1) return -1;
        out[0] = *tok;
        return 1;
    }
    return count;
}

/* ================================================================== *
 * WP-05: output capture (via console hook)
 * ================================================================== */

typedef struct {
    char *buf;
    int   size;
    int   cap;
    int   active;
    int   overflowed;  /* WP-09-FIX BUG-025: set when output was dropped */
} shell_capture_t;

/* BUG-0083 FIX (A15-4): capture used to be a SINGLE global buffer with
 * a "one capture at a time" rule enforced by returning -1 from
 * shell_capture_begin. A nested exec (e.g. an SSH remote command that
 * itself contains a pipe, or any capture inside a capture) then broke
 * in two ways: the inner begin failed and the inner stage never ran
 * (remote command fails), or an inner end/free UNINSTALLED the hook
 * and restored the user hook, so the OUTER capture silently stopped
 * collecting and its output leaked to the console (data pollution).
 * Captures are now a STACK (up to 4 levels). All existing readers use
 * `g_capture`, which aliases the innermost ACTIVE slot, so the exec
 * pipeline code keeps working unchanged; the hook only restores the
 * user hook when the LAST capture goes away. */
#define OC_CAPTURE_MAX 4
static shell_capture_t g_capture_stk[OC_CAPTURE_MAX];
static int g_capture_depth = 0;
#define g_capture (g_capture_stk[g_capture_depth > 0 ? g_capture_depth - 1 : 0])

/* Track the "user" hook (the one the rest of the kernel installs, e.g.
 * serial output). When we capture, we install our own hook on top and
 * forward to this one. */
static shell_hook_fn_t g_user_hook = NULL;
static void *g_user_hook_ctx = NULL;
static int g_capture_installed = 0;

/* Our capturing hook. Forwards to the previously-installed hook (so serial
 * output keeps working), then appends the char to the capture buffer, then
 * returns 1 to suppress the framebuffer draw. */
static int shell_capture_hook_body(void *ctx, u8 ch) {
    (void)ctx;
    /* BUG-016 FIX: During capture (pipe/redirect), do NOT forward to
     * g_user_hook (which sends to serial). The old code forwarded every
     * char to the serial port, causing intermediate output to leak
     * during pipe/redirect operations. Only capture into the buffer. */
    if (g_capture_depth > 0 && g_capture.buf) {
        if (g_capture.size + 1 < g_capture.cap) {
            g_capture.buf[g_capture.size++] = (char)ch;
            g_capture.buf[g_capture.size] = 0;
        } else {
            /* WP-09-FIX BUG-025: mark overflow so the caller can warn
             * (the old code silently dropped the excess). */
            g_capture.overflowed = 1;
        }
        return 1;  /* suppress framebuffer AND serial during capture */
    }
    /* Not capturing: forward to user hook (serial etc.) */
    if (g_user_hook) g_user_hook(g_user_hook_ctx, ch);
    return 0;  /* let framebuffer draw normally */
}

static int shell_capture_hook(void *ctx, u8 ch) {
    return shell_capture_hook_body(ctx, ch);
}

void shell_install_console_hook(shell_hook_fn_t hook, void *ctx) {
    g_user_hook = hook;
    g_user_hook_ctx = ctx;
    if (!g_capture_installed) {
        l1_ext_set_console_hook((screen_console_hook_fn)hook, ctx);
    }
    /* If we're currently capturing, the capture hook remains installed
     * (and forwards to g_user_hook). */
}

/* Re-install the capture hook on top of the user hook. */
static void shell_capture_install(void) {
    if (g_capture_installed) return;
    /* Install our wrapper. */
    l1_ext_set_console_hook(shell_capture_hook, NULL);
    g_capture_installed = 1;
}

static void shell_capture_uninstall(void) {
    if (!g_capture_installed) return;
    /* Restore the user hook. */
    l1_ext_set_console_hook((screen_console_hook_fn)g_user_hook, g_user_hook_ctx);
    g_capture_installed = 0;
}

static int shell_capture_begin(int cap) {
    /* BUG-0083 FIX (A15-4): push a new capture instead of refusing when
     * one is already active (see the comment above g_capture_stk). */
    if (g_capture_depth >= OC_CAPTURE_MAX) return -1;
    if (cap <= 0) cap = 8192;
    char *buf = (char *)kmalloc((u64)cap);
    if (!buf) return -1;
    g_capture_depth++;   /* push: g_capture now aliases the new slot */
    g_capture.buf = buf;
    g_capture.size = 0;
    g_capture.cap = cap;
    g_capture.overflowed = 0;  /* WP-09-FIX BUG-025 */
    g_capture.buf[0] = 0;
    g_capture.active = 1;
    shell_capture_install();
    return 0;
}

static void shell_capture_end(void) {
    /* BUG-0083 FIX (A15-4): mark only the TOP capture finished. The
     * buffer stays readable (shell_execute_captured copies it after
     * end) until shell_capture_free pops it; the hook is only torn
     * down when the LAST capture goes away. */
    if (g_capture_depth <= 0 || !g_capture.active) return;
    g_capture.active = 0;
}

static void shell_capture_free(void) {
    /* BUG-0083 FIX (A15-4): pop the top capture. */
    if (g_capture_depth <= 0) return;
    if (g_capture.buf) {
        kfree(g_capture.buf);
        g_capture.buf = NULL;
    }
    g_capture.size = 0;
    g_capture.cap = 0;
    g_capture.active = 0;
    g_capture_depth--;
    if (g_capture_depth == 0) shell_capture_uninstall();
}

/* ================================================================== *
 * WP-05: stdin buffer (for pipes and < redirection)
 * ================================================================== */

static char *g_stdin_buf = NULL;
static int   g_stdin_size = 0;
static int   g_stdin_pos = 0;

static void shell_stdin_set(const char *data, int size) {
    /* Free any previous buffer. */
    if (g_stdin_buf) {
        kfree(g_stdin_buf);
        g_stdin_buf = NULL;
    }
    g_stdin_size = 0;
    g_stdin_pos = 0;
    if (size <= 0 || !data) return;
    g_stdin_buf = (char *)kmalloc((u64)size);
    if (!g_stdin_buf) return;
    memcpy(g_stdin_buf, data, (usize)size);
    g_stdin_size = size;
    g_stdin_pos = 0;
}

static void shell_stdin_clear(void) {
    if (g_stdin_buf) {
        kfree(g_stdin_buf);
        g_stdin_buf = NULL;
    }
    g_stdin_size = 0;
    g_stdin_pos = 0;
}

int shell_read_stdin(void *buf, int size) {
    if (!g_stdin_buf || g_stdin_pos >= g_stdin_size) {
        return -1;  /* no stdin available */
    }
    int avail = g_stdin_size - g_stdin_pos;
    int n = (avail < size) ? avail : size;
    memcpy(buf, g_stdin_buf + g_stdin_pos, (usize)n);
    g_stdin_pos += n;
    return n;
}

int shell_has_stdin(void) {
    return (g_stdin_buf != NULL && g_stdin_pos < g_stdin_size) ? 1 : 0;
}

/* ================================================================== *
 * WP-05: command execution with redirects/pipes
 * ================================================================== */

/* Join tokens into a single args string (space-separated). Returns the
 * length written. `skip_first` skips the first token (the command word). */
static int shell_join_args(const token_t *toks, int ntoks, int skip_first,
                           char *out, int out_len) {
    int p = 0;
    for (int i = skip_first; i < ntoks; i++) {
        if (toks[i].kind != TOK_WORD) continue;
        if (p > 0) {
            if (p + 1 >= out_len) break;
            out[p++] = ' ';
        }
        int tl = (int)strlen(toks[i].text);
        if (p + tl >= out_len) tl = out_len - 1 - p;
        if (tl <= 0) break;
        memcpy(out + p, toks[i].text, (usize)tl);
        p += tl;
    }
    out[p] = 0;
    return p;
}

/* Execute a single piped stage. toks[0..ntoks-1] is the command + args (no
 * operators). stdin_data/stdin_size is the input from the previous stage
 * (or from a `<` redirect). capture_output is 1 if we should capture (for
 * pipe-to-next or > redirect). Returns the command's exit code (0 = success,
 * non-zero = failure or not-found).
 *
 * NOTE: toks[] must have at least SHELL_MAX_TOKENS slots because alias
 * expansion may grow the token list. */
static int shell_exec_stage(token_t *toks, int ntoks,
                            const char *stdin_data, int stdin_size,
                            int capture_output, int capture_cap) {
    if (ntoks <= 0) return 0;

    /* Set up stdin if provided. */
    if (stdin_data && stdin_size > 0) {
        shell_stdin_set(stdin_data, stdin_size);
    } else {
        shell_stdin_clear();
    }

    /* Set up capture if requested. */
    if (capture_output) {
        if (shell_capture_begin(capture_cap) < 0) {
            screen_console_puts("shell: out of memory for capture\n");
            return 1;
        }
    } else if (!g_capture_installed) {
        /* WP-09 fix: only clear a stale active flag when no outer capture is
         * running. The unconditional reset killed hierarchical capture (e.g.
         * sshd's shell_execute_captured): the inner segment deactivated the
         * outer capture, so sshd captured 0 bytes and the output leaked to
         * the local console instead of the SSH client. */
        g_capture.active = 0;  /* paranoid */
    }

    /* Apply alias to the first word. We tokenize the alias target on the
     * stack (small, max 8 tokens), and if it expands we need a heap buffer
     * to build the new token list. */
    if (toks[0].kind == TOK_WORD) {
        const char *al = shell_lookup_alias(toks[0].text);
        if (al) {
            token_t altoks[8];
            int an = shell_tokenize(al, altoks, 8);
            if (an > 0 && ntoks + an - 1 < SHELL_MAX_TOKENS) {
                /* Build new token list in a heap buffer (each token_t is
                 * ~300 bytes, so 32 of them is ~9 KiB - too big for the
                 * 16 KiB kernel stack). */
                token_t *newtoks = (token_t *)kmalloc(
                    sizeof(token_t) * (u64)SHELL_MAX_TOKENS);
                if (newtoks) {
                    int nn = 0;
                    for (int i = 0; i < an; i++) {
                        if (altoks[i].kind != TOK_WORD) break;
                        shell_expand_env_token(&altoks[i]);
                        newtoks[nn++] = altoks[i];
                    }
                    for (int i = 1; i < ntoks; i++) {
                        newtoks[nn++] = toks[i];
                    }
                    /* Copy back into toks[]. */
                    for (int i = 0; i < nn; i++) toks[i] = newtoks[i];
                    ntoks = nn;
                    kfree(newtoks);
                }
            }
        }
    }

    /* Get command word and args. */
    char cmd[64];
    strncpy(cmd, toks[0].text, sizeof(cmd) - 1);
    cmd[sizeof(cmd) - 1] = 0;
    char args[512];
    shell_join_args(toks, ntoks, 1, args, sizeof(args));

    /* Look up and execute. */
    int rc = 0;
    shell_cmd_fn fn = shell_lookup(cmd);
    if (fn) {
        rc = fn(args);
        if (rc < 0) rc = -rc;  /* normalize */
    } else {
        /* Don't print "unknown command" here - the parser might still be
         * mid-chain. Let the caller decide. */
        rc = -127;  /* "command not found" sentinel */
    }

    /* End capture. */
    if (capture_output) {
        shell_capture_end();
    }
    /* Clear stdin (we own it). */
    shell_stdin_clear();

    return rc;
}

/* Parse and execute one "command line segment" - a sequence of tokens
 * between ;, &&, || boundaries. The segment may contain pipes and
 * redirections. */
static int shell_exec_segment(token_t *toks, int ntoks) {
    if (ntoks <= 0) return 0;

    /* Detect trailing & (background). */
    int background = 0;
    if (toks[ntoks - 1].kind == TOK_AMP) {
        background = 1;
        ntoks--;
        if (ntoks <= 0) return 0;
    }

    /* Allocate the cleaned-token array and the wildcard-expansion scratch
     * on the heap (each token_t is ~300 bytes; SHELL_MAX_TOKENS of them is
     * ~9 KiB, which would overflow the 16 KiB kernel stack). */
    token_t *clean = (token_t *)kmalloc(sizeof(token_t) * (u64)SHELL_MAX_TOKENS);
    token_t *expanded = (token_t *)kmalloc(sizeof(token_t) * 64);
    if (!clean || !expanded) {
        if (clean) kfree(clean);
        if (expanded) kfree(expanded);
        screen_console_puts("shell: out of memory\n");
        return 1;
    }

    /* Split into pipe stages on TOK_PIPE. */
    int stage_start = 0;
    char *prev_output = NULL;   /* kmalloc'd buffer of prev stage's output */
    int   prev_size = 0;
    int   rc = 0;

    for (int i = 0; i <= ntoks; i++) {
        /* Find the next pipe or end. */
        if (i == ntoks || toks[i].kind == TOK_PIPE) {
            int stage_ntoks = i - stage_start;

            /* Parse redirections within this stage. */
            token_t *stage = &toks[stage_start];
            char redir_out[VFS_PATH_LEN];
            int  redir_out_append = 0;
            int  has_redir_out = 0;
            char redir_in[VFS_PATH_LEN];
            int  has_redir_in = 0;

            int nclean = 0;
            for (int j = 0; j < stage_ntoks; j++) {
                if (stage[j].kind == TOK_GT || stage[j].kind == TOK_APPEND) {
                    if (j + 1 < stage_ntoks && stage[j + 1].kind == TOK_WORD) {
                        /* BUG-026 FIX: Use strncpy with bounds check.
                         * Old code used strcpy which could overflow
                         * redir_out[VFS_PATH_LEN] if env expansion made
                         * the token longer than VFS_PATH_LEN. */
                        strncpy(redir_out, stage[j + 1].text, VFS_PATH_LEN - 1);
                        redir_out[VFS_PATH_LEN - 1] = 0;
                        redir_out_append = (stage[j].kind == TOK_APPEND);
                        has_redir_out = 1;
                        j++;  /* skip the filename */
                    }
                } else if (stage[j].kind == TOK_LT) {
                    if (j + 1 < stage_ntoks && stage[j + 1].kind == TOK_WORD) {
                        /* WP-09-FIX BUG-018: bounded copy, same as the
                         * redir_out path above. The old strcpy could
                         * overflow redir_in[256] with a long expanded
                         * token. */
                        strncpy(redir_in, stage[j + 1].text, VFS_PATH_LEN - 1);
                        redir_in[VFS_PATH_LEN - 1] = 0;
                        has_redir_in = 1;
                        j++;
                    }
                } else if (stage[j].kind == TOK_WORD) {
                    /* Expand wildcards. */
                    int ne = shell_expand_wildcards(&stage[j], expanded, 64);
                    if (ne < 0) {
                        /* On error, keep the original. */
                        if (nclean < SHELL_MAX_TOKENS) clean[nclean++] = stage[j];
                    } else {
                        for (int k = 0; k < ne && nclean < SHELL_MAX_TOKENS; k++) {
                            clean[nclean++] = expanded[k];
                        }
                    }
                }
            }

            /* Is this the last stage? */
            int is_last = (i == ntoks);

            /* If there's an input redirect, load the file into the stdin
             * buffer for this stage. (This overrides the pipe input.) */
            char *stage_stdin = NULL;
            int   stage_stdin_size = 0;
            char  file_stdin_buf[1024];
            if (has_redir_in) {
                const char *resolved = shell_resolve_path_static(redir_in);
                int fd = fs_vfs_open(resolved, VFS_O_RDONLY);
                if (fd >= 0) {
                    int n = fs_vfs_read(fd, file_stdin_buf, sizeof(file_stdin_buf) - 1);
                    fs_vfs_close(fd);
                    if (n > 0) {
                        file_stdin_buf[n] = 0;
                        stage_stdin = file_stdin_buf;
                        stage_stdin_size = n;
                    }
                } else {
                    screen_console_puts("shell: cannot open ");
                    screen_console_puts(resolved);
                    screen_console_puts(" for input\n");
                }
            } else if (prev_output) {
                stage_stdin = prev_output;
                stage_stdin_size = prev_size;
            }

            /* Decide whether to capture output. We capture if:
             *   - there's a redirect to a file, OR
             *   - there's a next pipe stage. */
            int capture = (has_redir_out || !is_last) ? 1 : 0;
            /* P4 fix: was 16384 (16 KiB) per pipe stage — wasteful heap
             * allocation that could fail on small heap pools. Reduced to
             * 4 KiB (one page, matches PIPE_BUF_SIZE). For redirect-to-file,
             * use 8 KiB (was 64 KiB — overkill for typical shell redirects).
             * Larger outputs are truncated with a warning. */
            int cap_size = 4096;
            if (has_redir_out) {
                cap_size = 8192;
            }

            (void)background;  /* we run synchronously; & is a no-op */

            rc = shell_exec_stage(clean, nclean,
                                  stage_stdin, stage_stdin_size,
                                  capture, cap_size);
            if (rc == -127) {
                /* FIX: report the command that was ACTUALLY not found (this
                 * stage's first word), not the segment's first word — the
                 * caller can only see the segment head, so `echo hi | wc`
                 * used to print "unknown command: echo" while the missing
                 * command was wc. Print here with the correct name, stop
                 * the pipeline, and return 1 so the caller doesn't print
                 * a second (wrong) message. */
                char bad[64];
                strncpy(bad, clean[0].text, sizeof(bad) - 1);
                bad[sizeof(bad) - 1] = 0;
                screen_console_puts("unknown command: ");
                screen_console_puts(bad);
                screen_console_puts(" (try 'help')\n");
                rc = 1;
                stage_start = i + 1;
                break;
            }

            /* Free the previous stage's output (we've consumed it). */
            if (prev_output) {
                kfree(prev_output);
                prev_output = NULL;
                prev_size = 0;
            }

            /* If we captured, hand the output to the next stage or write it
             * to the redirect file. */
            if (capture && g_capture.buf) {
                if (g_capture.overflowed) {
                    /* WP-09-FIX BUG-025: the code above promised a warning
                     * for truncated captures — actually emit it now. */
                    screen_console_puts("shell: warning: output exceeded the capture buffer and was truncated\n");
                }
                if (has_redir_out) {
                    /* Write to file. */
                    const char *resolved = shell_resolve_path_static(redir_out);
                    int flags = VFS_O_WRONLY | VFS_O_CREAT;
                    if (redir_out_append) flags |= VFS_O_APPEND;
                    else flags |= VFS_O_TRUNC;  /* P2-15: `>` truncates */
                    int fd = fs_vfs_open(resolved, flags);
                    if (fd >= 0) {
                        fs_vfs_write(fd, g_capture.buf, g_capture.size);
                        fs_vfs_close(fd);
                    } else {
                        screen_console_puts("shell: cannot open ");
                        screen_console_puts(resolved);
                        screen_console_puts(" for output\n");
                    }
                } else if (!is_last) {
                    /* Hand to next stage. */
                    prev_output = (char *)kmalloc((u64)g_capture.size + 1);
                    if (prev_output) {
                        memcpy(prev_output, g_capture.buf, (usize)g_capture.size);
                        prev_output[g_capture.size] = 0;
                        prev_size = g_capture.size;
                    }
                }
                shell_capture_free();
            }

            stage_start = i + 1;
        }
    }

    if (prev_output) {
        kfree(prev_output);
    }

    kfree(clean);
    kfree(expanded);
    return rc;
}

/* ================================================================== *
 * WP-05: shell_execute_line - the full parser/executor
 * ================================================================== */

int shell_execute_line(const char *line) {
    if (!line || !line[0]) return 0;

    /* Strip leading whitespace. */
    while (*line == ' ' || *line == '\t') line++;
    if (!*line) return 0;

    /* Tokenize into a heap buffer (the token array is too big for the
     * 16 KiB kernel stack). */
    token_t *toks = (token_t *)kmalloc(sizeof(token_t) * (u64)SHELL_MAX_TOKENS);
    if (!toks) {
        screen_console_puts("shell: out of memory\n");
        return 1;
    }
    int ntoks = shell_tokenize(line, toks, SHELL_MAX_TOKENS);
    if (ntoks < 0) {
        screen_console_puts("shell: too many tokens\n");
        kfree(toks);
        return 1;
    }
    if (ntoks == 0) {
        kfree(toks);
        return 0;
    }

    /* Expand env vars in each word token. */
    for (int i = 0; i < ntoks; i++) {
        shell_expand_env_token(&toks[i]);
    }

    /* Walk tokens, splitting on ;, &&, ||. Pass &toks[seg_start] directly
     * (no copy). */
    int seg_start = 0;
    int last_rc = 0;

    for (int i = 0; i <= ntoks; i++) {
        tok_kind_t kind;
        if (i == ntoks) {
            kind = TOK_SEMI;  /* synthetic end */
        } else {
            kind = toks[i].kind;
        }

        if (kind == TOK_SEMI || kind == TOK_AND || kind == TOK_OR) {
            int seg_len = i - seg_start;
            int should_run = 1;
            if (kind == TOK_AND) {
                /* Run only if previous succeeded. */
                should_run = (last_rc == 0);
            } else if (kind == TOK_OR) {
                /* Run only if previous failed. */
                should_run = (last_rc != 0);
            }

            if (should_run && seg_len > 0) {
                last_rc = shell_exec_segment(&toks[seg_start], seg_len);
                if (last_rc == -127) {
                    /* Command not found. */
                    char cmd[64];
                    strncpy(cmd, toks[seg_start].text, sizeof(cmd) - 1);
                    cmd[sizeof(cmd) - 1] = 0;
                    screen_console_puts("unknown command: ");
                    screen_console_puts(cmd);
                    screen_console_puts(" (try 'help')\n");
                    last_rc = 1;
                }
            }

            seg_start = i + 1;
        }
    }

    kfree(toks);
    return last_rc;
}

/* ================================================================== *
 * WP-09: shell_execute_captured - run a line, capture console output
 * ================================================================== */

int shell_execute_captured(const char *line, char *out, int out_cap) {
    if (out && out_cap > 0) out[0] = 0;

    int rc;
    if (shell_capture_begin(out_cap > 0 ? out_cap : 8192) == 0) {
        rc = shell_execute_line(line);
        shell_capture_end();
        if (out && out_cap > 0 && g_capture.buf) {
            int n = g_capture.size;
            if (n > out_cap - 1) n = out_cap - 1;
            if (n > 0) memcpy(out, g_capture.buf, (usize)n);
            out[n] = 0;
        }
        shell_capture_free();
    } else {
        /* Capture busy (nested capture): run un-captured rather than fail. */
        rc = shell_execute_line(line);
    }
    return rc;
}

/* ================================================================== *
 * WP-10-wp08fix1: Tab completion + nano/vi editor command
 * ================================================================== */

/* Longest-common-prefix completion over the registered command table.
 * `prefix` is the word being completed (NUL-terminated). On match,
 * `out` receives the longest common prefix of every candidate whose
 * name starts with `prefix` (NUL-terminated, at most out_cap-1 bytes).
 * Returns the number of candidates (0 = no match, 1 = unique match,
 * >1 = several share the returned prefix). */
int shell_complete_command_prefix(const char *prefix, char *out, int out_cap) {
    if (!prefix || !out || out_cap <= 0) return 0;
    out[0] = 0;
    int plen = (int)strlen(prefix);
    if (plen == 0) return 0;

    int found = 0;
    int common = 0;   /* length of the common prefix so far */
    char cpl[32];
    cpl[0] = 0;

    for (int i = 0; i < SHELL_MAX_COMMANDS; i++) {
        if (!g_commands[i].in_use) continue;
        const char *name = g_commands[i].name;
        /* Candidate must start with the typed prefix. */
        int matches = 1;
        for (int j = 0; j < plen; j++) {
            if (name[j] != prefix[j]) { matches = 0; break; }
        }
        if (!matches) continue;
        if (found == 0) {
            strncpy(cpl, name, sizeof(cpl) - 1);
            cpl[sizeof(cpl) - 1] = 0;
            common = (int)strlen(cpl);
        } else {
            int j = 0;
            while (j < common && cpl[j] && name[j] && cpl[j] == name[j]) j++;
            common = j;
            cpl[common] = 0;
        }
        found++;
    }
    if (found == 0) return 0;
    if (common > out_cap - 1) common = out_cap - 1;
    memcpy(out, cpl, (usize)common);
    out[common] = 0;
    return found;
}

/* nano/vi <file>: the kernel-side editor (same engine for both names,
 * like the ush pairing). The `edit` line editor from WP-05 stays
 * available unchanged. */
static int shell_cmd_nano(const char *args) {
    if (!args || !args[0]) {
        screen_console_puts("usage: nano <file>   (^O save, ^X exit)\n");
        return 1;
    }
    /* First token = file name. */
    char file[128];
    int i = 0;
    while (args[i] && args[i] != ' ' && i < (int)sizeof(file) - 1) {
        file[i] = args[i];
        i++;
    }
    file[i] = 0;
    /* Resolve against the shell cwd (nano resolves like every other
     * file command). */
    const char *resolved = shell_resolve_path_static(file);
    if (editor_open(resolved ? resolved : file) != 0) {
        screen_console_puts("nano: cannot open ");
        screen_console_puts(file);
        screen_console_putc('\n');
        return 1;
    }
    int rc = editor_run();   /* editor_close() is done inside editor_run */
    return rc;
}

/* BUG-0135 FIX: `vi` used to be a plain alias of shell_cmd_nano, so the
 * "vi editor" the docs promised did not exist — every key, including
 * "ithree-fox" and ":wq", was literal text in the same nano buffer.
 * vi now runs the real modal engine (editor_run_vi): NORMAL/INSERT,
 * hjkl/0/$/G/gg/x/dd/i a A I o O and a `:` ex command line. */
static int shell_cmd_vi(const char *args) {
    if (!args || !args[0]) {
        screen_console_puts("usage: vi <file>   (hjkl move, i insert, :w :q :wq :q!)\n");
        return 1;
    }
    char file[128];
    int i = 0;
    while (args[i] && args[i] != ' ' && i < (int)sizeof(file) - 1) {
        file[i] = args[i];
        i++;
    }
    file[i] = 0;
    const char *resolved = shell_resolve_path_static(file);
    if (editor_open(resolved ? resolved : file) != 0) {
        screen_console_puts("vi: cannot open ");
        screen_console_puts(file);
        screen_console_putc('\n');
        return 1;
    }
    int rc = editor_run_vi();   /* editor_close() is done inside editor_run_vi */
    return rc;
}

/* ================================================================== *
 * WP-05: shell_init
 * ================================================================== */

void shell_init(void) {
    /* Clear tables. */
    memset(g_env, 0, sizeof(g_env));
    memset(g_aliases, 0, sizeof(g_aliases));
    memset(g_commands, 0, sizeof(g_commands));
    strcpy(g_cwd, "/");

    /* Register builtins. */
    shell_register_command_ex("export", shell_cmd_export, "set env var (export NAME=value)", "WP-05");
    shell_register_command_ex("set", shell_cmd_set, "list env vars", "WP-05");
    shell_register_command_ex("alias", shell_cmd_alias, "set/show aliases", "WP-07");
    shell_register_command_ex("unalias", shell_cmd_unalias, "remove an alias", "WP-07");
    /* WP-10-wp08fix1: nano editor; BUG-0135 FIX: vi is now a real
     * two-mode engine of its own (was a nano alias). */
    shell_register_command_ex("nano", shell_cmd_nano, "nano-style editor (nano <file>; ^O save ^X exit)", "WP-10-wp08fix1");
    shell_register_command_ex("vi", shell_cmd_vi, "vi editor, two-mode (hjkl move, i/a/o insert, ESC, :w :q :wq :q!)", "WP-AUDIT-01-p1fix3");

    /* Set some default env vars. */
    shell_setenv("SHELL", "/bin/ocsh");
    shell_setenv("PS1", "oc> ");
    shell_setenv("PATH", "/bin:/sbin");
}

/* ------------------------------------------------------------------ *
 * WP-10d-fix2: self-test helpers + L1 extension surface (kernel/ext.h)
 * ------------------------------------------------------------------ */

/* 1 when the A-Z view is strictly non-decreasing by name
 * (case-insensitive) and at least one command is registered. */
int shell_verify_sorted_a_z(void) {
    int idx[SHELL_MAX_COMMANDS];
    int n = shell_wp_sorted_indices(idx, (int)SHELL_MAX_COMMANDS);
    for (int i = 1; i < n; i++)
        if (shell_wp_name_cmp(g_commands[idx[i - 1]].name,
                        g_commands[idx[i]].name) > 0)
            return 0;
    return n > 0;
}

/* 1 when every known work-package group is itself sorted A-Z. */
int shell_verify_wp_groups(void) {
    int idx[SHELL_MAX_COMMANDS];
    int n = shell_wp_sorted_indices(idx, (int)SHELL_MAX_COMMANDS);
    for (int k = 0; k < (int)(sizeof(g_wp_order) / sizeof(g_wp_order[0])); k++) {
        int prev = -1;
        for (int i = 0; i < n; i++) {
            if (strcmp(g_commands[idx[i]].wp, g_wp_order[k].tag) != 0)
                continue;
            if (prev >= 0 &&
                shell_wp_name_cmp(g_commands[idx[prev]].name,
                            g_commands[idx[i]].name) > 0)
                return 0;
            prev = i;
        }
    }
    return 1;
}

int l1_ext_shell_register_command_ex(const char *name, int (*fn)(const char *),
                                     const char *help, const char *wp) {
    return shell_register_command_ex(name, fn, help, wp);
}

void l1_ext_shell_list_commands_a_z(void) {
    shell_list_commands_a_z();
}

void l1_ext_shell_list_commands_by_wp(void) {
    shell_list_commands_by_wp();
}
