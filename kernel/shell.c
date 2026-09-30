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
#include "console.h"
#include "ext.h"
#include "heap.h"
#include "string.h"
#include "vfs.h"

/* ================================================================== *
 * WP-03 command table (unchanged)
 * ================================================================== */

typedef struct {
    char name[32];
    shell_cmd_fn handler;
    char help[80];
    int in_use;
} shell_cmd_t;

static shell_cmd_t g_commands[SHELL_MAX_COMMANDS];

int shell_register_command(const char *name, shell_cmd_fn handler, const char *help) {
    if (!name || !handler) return -1;
    /* Check if already registered (replace). */
    for (int i = 0; i < SHELL_MAX_COMMANDS; i++) {
        if (g_commands[i].in_use && oc_strcmp(g_commands[i].name, name) == 0) {
            g_commands[i].handler = handler;
            if (help) oc_strncpy(g_commands[i].help, help, sizeof(g_commands[i].help) - 1);
            return 0;
        }
    }
    /* Find a free slot. */
    for (int i = 0; i < SHELL_MAX_COMMANDS; i++) {
        if (!g_commands[i].in_use) {
            oc_strncpy(g_commands[i].name, name, sizeof(g_commands[i].name) - 1);
            g_commands[i].name[sizeof(g_commands[i].name) - 1] = 0;
            g_commands[i].handler = handler;
            if (help) {
                oc_strncpy(g_commands[i].help, help, sizeof(g_commands[i].help) - 1);
                g_commands[i].help[sizeof(g_commands[i].help) - 1] = 0;
            } else {
                g_commands[i].help[0] = 0;
            }
            g_commands[i].in_use = 1;
            return 0;
        }
    }
    /* BUG-004 FIX: Print a warning when the command table is full,
     * so silent registration failures are visible. */
    oc_console_puts("[shell] WARNING: command table full, cannot register '");
    oc_console_puts(name);
    oc_console_puts("'\n");
    return -2;  /* table full */
}

int shell_unregister_command(const char *name) {
    if (!name) return -1;
    for (int i = 0; i < SHELL_MAX_COMMANDS; i++) {
        if (g_commands[i].in_use && oc_strcmp(g_commands[i].name, name) == 0) {
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
        if (g_commands[i].in_use && oc_strcmp(g_commands[i].name, name) == 0) {
            return g_commands[i].handler;
        }
    }
    return NULL;
}

/* Execute a single command word + args string. Returns 1 if found, 0 if not. */
static int shell_run_simple(const char *cmd, const char *args) {
    shell_cmd_fn fn = shell_lookup(cmd);
    if (!fn) return 0;
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

void shell_print_help(void) {
    for (int i = 0; i < SHELL_MAX_COMMANDS; i++) {
        if (g_commands[i].in_use) {
            oc_console_puts("  ");
            oc_console_puts(g_commands[i].name);
            oc_console_puts(" - ");
            oc_console_puts(g_commands[i].help);
            oc_console_putc('\n');
        }
    }
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
        if (g_env[i].in_use && oc_strcmp(g_env[i].name, name) == 0) {
            if (value) {
                oc_strncpy(g_env[i].value, value, SHELL_ENV_VALUE_LEN - 1);
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
            oc_strncpy(g_env[i].name, name, SHELL_ENV_NAME_LEN - 1);
            g_env[i].name[SHELL_ENV_NAME_LEN - 1] = 0;
            if (value) {
                oc_strncpy(g_env[i].value, value, SHELL_ENV_VALUE_LEN - 1);
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
        if (g_env[i].in_use && oc_strcmp(g_env[i].name, name) == 0) {
            return g_env[i].value;
        }
    }
    return NULL;
}

/* `export VAR=value` builtin. Also accepts `export VAR` (no value, sets to ""). */
static int cmd_export(const char *args) {
    if (!args || !args[0]) {
        /* List all env vars. */
        oc_console_puts("Environment variables:\n");
        for (int i = 0; i < SHELL_MAX_ENV; i++) {
            if (g_env[i].in_use) {
                oc_console_puts("  ");
                oc_console_puts(g_env[i].name);
                oc_console_puts("=");
                oc_console_puts(g_env[i].value);
                oc_console_putc('\n');
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
    oc_memcpy(name, args, (usize)nlen);
    name[nlen] = 0;
    const char *value = (*eq == '=') ? eq + 1 : "";
    if (shell_setenv(name, value) < 0) {
        oc_console_puts("export: table full\n");
        return 1;
    }
    return 0;
}

/* `set` builtin - alias for `export` without args (list vars). */
static int cmd_set(const char *args) {
    (void)args;
    return cmd_export("");
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
        if (g_aliases[i].in_use && oc_strcmp(g_aliases[i].name, alias) == 0) {
            oc_strncpy(g_aliases[i].target, target, SHELL_ALIAS_TARGET_LEN - 1);
            g_aliases[i].target[SHELL_ALIAS_TARGET_LEN - 1] = 0;
            return 0;
        }
    }
    for (int i = 0; i < SHELL_MAX_ALIASES; i++) {
        if (!g_aliases[i].in_use) {
            oc_strncpy(g_aliases[i].name, alias, SHELL_ALIAS_NAME_LEN - 1);
            g_aliases[i].name[SHELL_ALIAS_NAME_LEN - 1] = 0;
            oc_strncpy(g_aliases[i].target, target, SHELL_ALIAS_TARGET_LEN - 1);
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
        if (g_aliases[i].in_use && oc_strcmp(g_aliases[i].name, alias) == 0) {
            g_aliases[i].in_use = 0;
            return 0;
        }
    }
    return -2;
}

static const char *shell_lookup_alias(const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < SHELL_MAX_ALIASES; i++) {
        if (g_aliases[i].in_use && oc_strcmp(g_aliases[i].name, name) == 0) {
            return g_aliases[i].target;
        }
    }
    return NULL;
}

/* `alias` builtin.
 *   alias                - list all
 *   alias name='value'   - set
 *   alias name value     - set (alternative form) */
static int cmd_alias(const char *args) {
    if (!args || !args[0]) {
        /* List all. */
        oc_console_puts("Aliases:\n");
        for (int i = 0; i < SHELL_MAX_ALIASES; i++) {
            if (g_aliases[i].in_use) {
                oc_console_puts("  ");
                oc_console_puts(g_aliases[i].name);
                oc_console_puts("='");
                oc_console_puts(g_aliases[i].target);
                oc_console_puts("'\n");
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
            oc_memcpy(tmp, p, (usize)tlen);
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
                oc_console_puts(name);
                oc_console_puts("='");
                oc_console_puts(t);
                oc_console_puts("'\n");
                return 0;
            }
            oc_console_puts("alias: not found: ");
            oc_console_puts(name);
            oc_console_putc('\n');
            return 1;
        }
        return shell_register_alias(name, p) < 0 ? 1 : 0;
    } else {
        /* `alias name` - print. */
        const char *t = shell_lookup_alias(name);
        if (t) {
            oc_console_puts(name);
            oc_console_puts("='");
            oc_console_puts(t);
            oc_console_puts("'\n");
            return 0;
        }
        oc_console_puts("alias: not found: ");
        oc_console_puts(name);
        oc_console_putc('\n');
        return 1;
    }
}

/* `unalias name` builtin. */
static int cmd_unalias(const char *args) {
    if (!args || !args[0]) {
        oc_console_puts("usage: unalias <name>\n");
        return 1;
    }
    if (shell_unregister_alias(args) < 0) {
        oc_console_puts("unalias: not found\n");
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
        if (oc_strcmp(comp, ".") == 0) {
            /* skip */
        } else if (oc_strcmp(comp, "..") == 0) {
            if (ncomp > 0) ncomp--;
        } else {
            if (ncomp >= (int)(sizeof(comps) / sizeof(comps[0]))) return -1;
            oc_strncpy(comps[ncomp], comp, VFS_NAME_LEN - 1);
            comps[ncomp][VFS_NAME_LEN - 1] = 0;
            ncomp++;
        }
    }
    /* Rebuild. */
    int p = 0;
    path[p++] = '/';
    for (int c = 0; c < ncomp; c++) {
        int len = (int)oc_strlen(comps[c]);
        if (p + len + 1 >= maxlen) return -1;
        oc_memcpy(path + p, comps[c], (usize)len);
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
        oc_strncpy(newcwd, path, SHELL_CWD_LEN - 1);
        newcwd[SHELL_CWD_LEN - 1] = 0;
    } else {
        /* Relative - prepend cwd. */
        int cwdlen = (int)oc_strlen(g_cwd);
        if (cwdlen + 1 + (int)oc_strlen(path) + 1 > SHELL_CWD_LEN) return -1;
        oc_strcpy(newcwd, g_cwd);
        if (g_cwd[cwdlen - 1] != '/') {
            newcwd[cwdlen++] = '/';
            newcwd[cwdlen] = 0;
        }
        oc_strcpy(newcwd + cwdlen, path);
    }
    if (shell_normalize_path(newcwd, SHELL_CWD_LEN) < 0) return -1;
    /* Verify the path exists in the VFS. */
    vfs_stat_t st;
    if (vfs_stat(newcwd, &st) < 0) return -2;
    if (st.type != VFS_TYPE_DIR) return -3;
    oc_strcpy(g_cwd, newcwd);
    return 0;
}

int shell_resolve_path(const char *path, char *out, int out_len) {
    if (!path || !out || out_len < 2) return -1;
    if (path[0] == '/') {
        oc_strncpy(out, path, out_len - 1);
        out[out_len - 1] = 0;
    } else {
        int cwdlen = (int)oc_strlen(g_cwd);
        if (cwdlen + 1 + (int)oc_strlen(path) + 1 > out_len) return -1;
        oc_strcpy(out, g_cwd);
        if (g_cwd[cwdlen - 1] != '/') {
            out[cwdlen++] = '/';
            out[cwdlen] = 0;
        }
        oc_strcpy(out + cwdlen, path);
    }
    return shell_normalize_path(out, out_len);
}

/* Static buffer for the convenience wrapper. */
/* P0-8 FIX: provide two static buffers so callers that resolve two paths
 * in one expression (e.g. cmd_mv: rsrc = resolve(src); rdst = resolve(dst))
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
    oc_strcpy(tok->text, buf);
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
    if (tok->flags & (TOK_QUOTED_SINGLE | TOK_QUOTED_DOUBLE)) return 0;
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
    int tlen = (int)oc_strlen(tok->text);
    for (int i = 0; i < tlen; i++) {
        if (tok->text[i] == '/') last_slash = i;
    }
    if (last_slash < 0) {
        /* No directory - use cwd. */
        oc_strcpy(dirpath, shell_get_cwd());
        oc_strcpy(pat, tok->text);
    } else {
        if (last_slash >= VFS_PATH_LEN - 1) return -1;
        oc_memcpy(dirpath, tok->text, (usize)last_slash + 1);
        dirpath[last_slash + 1] = 0;
        /* If the path was like "/foo/x.c" with a wildcard, dirpath is the
         * parent dir (with trailing slash). Normalize it. */
        if (shell_resolve_path(dirpath, dirpath, VFS_PATH_LEN) < 0) return -1;
        oc_strcpy(pat, tok->text + last_slash + 1);
    }

    /* Read dir entries and match. */
    int count = 0;
    for (int idx = 0; ; idx++) {
        vfs_dirent_t e;
        if (vfs_readdir(dirpath, idx, &e) < 0) break;
        if (shell_glob_match(pat, e.name)) {
            if (count >= max_out) return -1;
            /* Build the full path. */
            out[count].kind = TOK_WORD;
            out[count].flags = 0;  /* expanded - no further processing */
            int dl = (int)oc_strlen(dirpath);
            int el = (int)oc_strlen(e.name);
            if (dl + el + 1 >= (int)sizeof(out[count].text)) {
                /* Skip on overflow. */
                continue;
            }
            oc_strcpy(out[count].text, dirpath);
            if (dl > 0 && dirpath[dl - 1] != '/') {
                out[count].text[dl++] = '/';
                out[count].text[dl] = 0;
            }
            oc_strcpy(out[count].text + dl, e.name);
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
} shell_capture_t;

static shell_capture_t g_capture = {0};

/* Track the "user" hook (the one the rest of the kernel installs, e.g.
 * serial output). When we capture, we install our own hook on top and
 * forward to this one. */
static shell_hook_fn_t g_user_hook = NULL;
static void *g_user_hook_ctx = NULL;
static int g_capture_installed = 0;

/* Our capturing hook. Forwards to the previously-installed hook (so serial
 * output keeps working), then appends the char to the capture buffer, then
 * returns 1 to suppress the framebuffer draw. */
static int shell_capture_hook(void *ctx, u8 ch) {
    (void)ctx;
    /* BUG-016 FIX: During capture (pipe/redirect), do NOT forward to
     * g_user_hook (which sends to serial). The old code forwarded every
     * char to the serial port, causing intermediate output to leak
     * during pipe/redirect operations. Only capture into the buffer. */
    if (g_capture.active && g_capture.buf) {
        if (g_capture.size + 1 < g_capture.cap) {
            g_capture.buf[g_capture.size++] = (char)ch;
            g_capture.buf[g_capture.size] = 0;
        }
        return 1;  /* suppress framebuffer AND serial during capture */
    }
    /* Not capturing: forward to user hook (serial etc.) */
    if (g_user_hook) g_user_hook(g_user_hook_ctx, ch);
    return 0;  /* let framebuffer draw normally */
}

void shell_install_console_hook(shell_hook_fn_t hook, void *ctx) {
    g_user_hook = hook;
    g_user_hook_ctx = ctx;
    if (!g_capture_installed) {
        oc_ext_set_console_hook((oc_console_hook_fn)hook, ctx);
    }
    /* If we're currently capturing, the capture hook remains installed
     * (and forwards to g_user_hook). */
}

/* Re-install the capture hook on top of the user hook. */
static void shell_capture_install(void) {
    if (g_capture_installed) return;
    /* Install our wrapper. */
    oc_ext_set_console_hook(shell_capture_hook, NULL);
    g_capture_installed = 1;
}

static void shell_capture_uninstall(void) {
    if (!g_capture_installed) return;
    /* Restore the user hook. */
    oc_ext_set_console_hook((oc_console_hook_fn)g_user_hook, g_user_hook_ctx);
    g_capture_installed = 0;
}

static int shell_capture_begin(int cap) {
    if (g_capture.active) return -1;
    if (cap <= 0) cap = 8192;
    g_capture.buf = (char *)kmalloc((u64)cap);
    if (!g_capture.buf) return -1;
    g_capture.size = 0;
    g_capture.cap = cap;
    g_capture.buf[0] = 0;
    g_capture.active = 1;
    shell_capture_install();
    return 0;
}

static void shell_capture_end(void) {
    if (!g_capture.active) return;
    g_capture.active = 0;
    shell_capture_uninstall();
}

static void shell_capture_free(void) {
    if (g_capture.buf) {
        kfree(g_capture.buf);
        g_capture.buf = NULL;
    }
    g_capture.size = 0;
    g_capture.cap = 0;
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
    oc_memcpy(g_stdin_buf, data, (usize)size);
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
    oc_memcpy(buf, g_stdin_buf + g_stdin_pos, (usize)n);
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
        int tl = (int)oc_strlen(toks[i].text);
        if (p + tl >= out_len) tl = out_len - 1 - p;
        if (tl <= 0) break;
        oc_memcpy(out + p, toks[i].text, (usize)tl);
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
            oc_console_puts("shell: out of memory for capture\n");
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
    oc_strncpy(cmd, toks[0].text, sizeof(cmd) - 1);
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
        oc_console_puts("shell: out of memory\n");
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
                        /* BUG-026 FIX: Use oc_strncpy with bounds check.
                         * Old code used oc_strcpy which could overflow
                         * redir_out[VFS_PATH_LEN] if env expansion made
                         * the token longer than VFS_PATH_LEN. */
                        oc_strncpy(redir_out, stage[j + 1].text, VFS_PATH_LEN - 1);
                        redir_out[VFS_PATH_LEN - 1] = 0;
                        redir_out_append = (stage[j].kind == TOK_APPEND);
                        has_redir_out = 1;
                        j++;  /* skip the filename */
                    }
                } else if (stage[j].kind == TOK_LT) {
                    if (j + 1 < stage_ntoks && stage[j + 1].kind == TOK_WORD) {
                        oc_strcpy(redir_in, stage[j + 1].text);
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
                int fd = vfs_open(resolved, VFS_O_RDONLY);
                if (fd >= 0) {
                    int n = vfs_read(fd, file_stdin_buf, sizeof(file_stdin_buf) - 1);
                    vfs_close(fd);
                    if (n > 0) {
                        file_stdin_buf[n] = 0;
                        stage_stdin = file_stdin_buf;
                        stage_stdin_size = n;
                    }
                } else {
                    oc_console_puts("shell: cannot open ");
                    oc_console_puts(resolved);
                    oc_console_puts(" for input\n");
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

            /* Free the previous stage's output (we've consumed it). */
            if (prev_output) {
                kfree(prev_output);
                prev_output = NULL;
                prev_size = 0;
            }

            /* If we captured, hand the output to the next stage or write it
             * to the redirect file. */
            if (capture && g_capture.buf) {
                if (has_redir_out) {
                    /* Write to file. */
                    const char *resolved = shell_resolve_path_static(redir_out);
                    int flags = VFS_O_WRONLY | VFS_O_CREAT;
                    if (redir_out_append) flags |= VFS_O_APPEND;
                    else flags |= VFS_O_TRUNC;  /* P2-15: `>` truncates */
                    int fd = vfs_open(resolved, flags);
                    if (fd >= 0) {
                        vfs_write(fd, g_capture.buf, g_capture.size);
                        vfs_close(fd);
                    } else {
                        oc_console_puts("shell: cannot open ");
                        oc_console_puts(resolved);
                        oc_console_puts(" for output\n");
                    }
                } else if (!is_last) {
                    /* Hand to next stage. */
                    prev_output = (char *)kmalloc((u64)g_capture.size + 1);
                    if (prev_output) {
                        oc_memcpy(prev_output, g_capture.buf, (usize)g_capture.size);
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
        oc_console_puts("shell: out of memory\n");
        return 1;
    }
    int ntoks = shell_tokenize(line, toks, SHELL_MAX_TOKENS);
    if (ntoks < 0) {
        oc_console_puts("shell: too many tokens\n");
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
                    oc_strncpy(cmd, toks[seg_start].text, sizeof(cmd) - 1);
                    cmd[sizeof(cmd) - 1] = 0;
                    oc_console_puts("unknown command: ");
                    oc_console_puts(cmd);
                    oc_console_puts(" (try 'help')\n");
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
            if (n > 0) oc_memcpy(out, g_capture.buf, (usize)n);
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
 * WP-05: shell_init
 * ================================================================== */

void shell_init(void) {
    /* Clear tables. */
    oc_memset(g_env, 0, sizeof(g_env));
    oc_memset(g_aliases, 0, sizeof(g_aliases));
    oc_memset(g_commands, 0, sizeof(g_commands));
    oc_strcpy(g_cwd, "/");

    /* Register builtins. */
    shell_register_command("export", cmd_export, "set env var (export NAME=value)");
    shell_register_command("set",    cmd_set,    "list env vars");
    shell_register_command("alias",  cmd_alias,  "set/show aliases");
    shell_register_command("unalias",cmd_unalias,"remove an alias");

    /* Set some default env vars. */
    shell_setenv("SHELL", "/bin/ocsh");
    shell_setenv("PS1", "oc> ");
    shell_setenv("PATH", "/bin:/sbin");
}
