/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS WP-08cd - User-space Shell (ush)
 *
 * A ring-3 shell that uses syscalls for all I/O:
 * - SYS_READLINE(73) for keyboard input (with line editing)
 * - SYS_WRITE2(72) for output (console/pipe/file)
 * - SYS_FORK(10) + SYS_EXECVE(11) + SYS_WAIT4(12) for command execution
 * - SYS_PIPE(20) + SYS_DUP2(51) for pipes
 * - SYS_OPEN(3) + SYS_CLOSE(4) for file redirect
 *
 * Features: prompt, fork+exec, pipe (|), redirect (> >> <),
 * environment variables ($VAR), built-in commands (cd, exit, export,
 * echo, alias, unalias, history).
 */

#define SYS_WRITE    1
#define SYS_EXIT2   16
#define SYS_FORK    10
#define SYS_EXECVE  11
#define SYS_WAIT4   12
#define SYS_PIPE    20
#define SYS_DUP     50
#define SYS_DUP2    51
#define SYS_CHDIR   60
#define SYS_GETCWD  61
#define SYS_OPEN     3
#define SYS_CLOSE    4
#define SYS_READ     70
#define SYS_WRITE2  72
#define SYS_READLINE 73

typedef unsigned long u64;
typedef long i64;
typedef int i32;
typedef unsigned int u32;
typedef char i8;
typedef unsigned char u8;

/* Inline syscall wrappers */
static inline long syscall0(long n) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(n) : "rcx","r11","memory");
    return r;
}
static inline long syscall1(long n, long a) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(n), "D"(a) : "rcx","r11","memory");
    return r;
}
static inline long syscall2(long n, long a, long b) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(n),"D"(a),"S"(b) : "rcx","r11","memory");
    return r;
}
static inline long syscall3(long n, long a, long b, long c) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(n),"D"(a),"S"(b),"d"(c) : "rcx","r11","memory");
    return r;
}

#define sys_write(fd, buf, len)  syscall3(SYS_WRITE2, (fd), (long)(buf), (len))
#define sys_read(fd, buf, len)   syscall3(SYS_READ, (fd), (long)(buf), (len))
#define sys_readline(buf, max)   syscall2(SYS_READLINE, (long)(buf), (max))
#define sys_fork()               syscall0(SYS_FORK)
#define sys_execve(path, argv, envp) syscall3(SYS_EXECVE, (long)(path), (long)(argv), (long)(envp))
#define sys_wait4(pid)           syscall1(SYS_WAIT4, (pid))
#define sys_exit2(code)          syscall1(SYS_EXIT2, (code))
#define sys_pipe(fds)            syscall1(SYS_PIPE, (long)(fds))
#define sys_dup2(old, new)       syscall2(SYS_DUP2, (old), (new))
#define sys_chdir(path)          syscall1(SYS_CHDIR, (long)(path))
#define sys_getcwd(buf, size)    syscall2(SYS_GETCWD, (long)(buf), (size))
#define sys_open(path, flags)    syscall2(SYS_OPEN, (long)(path), (flags))
#define sys_close(fd)            syscall1(SYS_CLOSE, (fd))

/* String helpers (no libc) */
static int strlen_(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}
static int streq(const char *a, const char *b) {
    int i = 0;
    while (a[i] && a[i] == b[i]) i++;
    return a[i] == 0 && b[i] == 0;
}
static int __attribute__((unused)) strncmp_(const char *a, const char *b, int n) {
    for (int i = 0; i < n; i++) {
        if (a[i] != b[i]) return a[i] - b[i];
        if (!a[i]) return 0;
    }
    return 0;
}
static void strcpy_(char *d, const char *s) {
    int i = 0;
    while (s[i]) { d[i] = s[i]; i++; }
    d[i] = 0;
}
static void strncpy_(char *d, const char *s, int n) {
    int i = 0;
    while (s[i] && i < n - 1) { d[i] = s[i]; i++; }
    d[i] = 0;
}
static void strcat_(char *d, const char *s) {
    int dl = strlen_(d);
    int i = 0;
    while (s[i]) { d[dl + i] = s[i]; i++; }
    d[dl + i] = 0;
}
static void __attribute__((unused)) memset_(void *d, int c, int n) {
    char *p = (char*)d;
    for (int i = 0; i < n; i++) p[i] = (char)c;
}
static int isspace_(char c) { return c == ' ' || c == '\t'; }

/* Output helpers */
static void puts_(const char *s) {
    sys_write(1, s, strlen_(s));
}
static void putc_(char c) {
    sys_write(1, &c, 1);
}
static void putu_(u64 v) {
    char buf[20];
    int i = 0;
    if (v == 0) { putc_('0'); return; }
    while (v > 0 && i < 19) { buf[i++] = '0' + (v % 10); v /= 10; }
    while (i > 0) putc_(buf[--i]);
}

/* Environment variables (simple: fixed-size array) */
#define MAX_ENV 32
#define ENV_NAME_LEN 32
#define ENV_VAL_LEN  256
static struct { char name[ENV_NAME_LEN]; char val[ENV_VAL_LEN]; } g_env[MAX_ENV];
static int g_env_count = 0;

static const char *env_get(const char *name) {
    for (int i = 0; i < g_env_count; i++) {
        if (streq(g_env[i].name, name)) return g_env[i].val;
    }
    return "";
}
static void env_set(const char *name, const char *val) {
    for (int i = 0; i < g_env_count; i++) {
        if (streq(g_env[i].name, name)) {
            strncpy_(g_env[i].val, val, ENV_VAL_LEN);
            return;
        }
    }
    if (g_env_count < MAX_ENV) {
        strncpy_(g_env[g_env_count].name, name, ENV_NAME_LEN);
        strncpy_(g_env[g_env_count].val, val, ENV_VAL_LEN);
        g_env_count++;
    }
}

/* Aliases */
#define MAX_ALIAS 16
static struct { char name[32]; char val[64]; } g_alias[MAX_ALIAS];
static int g_alias_count = 0;

static const char *alias_get(const char *name) {
    for (int i = 0; i < g_alias_count; i++) {
        if (streq(g_alias[i].name, name)) return g_alias[i].val;
    }
    return 0;
}
static void alias_set(const char *name, const char *val) {
    for (int i = 0; i < g_alias_count; i++) {
        if (streq(g_alias[i].name, name)) {
            strncpy_(g_alias[i].val, val, 64);
            return;
        }
    }
    if (g_alias_count < MAX_ALIAS) {
        strncpy_(g_alias[g_alias_count].name, name, 32);
        strncpy_(g_alias[g_alias_count].val, val, 64);
        g_alias_count++;
    }
}

/* Command history */
#define MAX_HISTORY 16
static char g_history[MAX_HISTORY][256];
static int g_history_count = 0;

static void history_add(const char *line) {
    if (g_history_count < MAX_HISTORY) {
        strncpy_(g_history[g_history_count], line, 256);
        g_history_count++;
    } else {
        /* Shift left */
        for (int i = 0; i < MAX_HISTORY - 1; i++) {
            strcpy_(g_history[i], g_history[i + 1]);
        }
        strncpy_(g_history[MAX_HISTORY - 1], line, 256);
    }
}

/* Expand $VAR in a line. Result written to `out`. */
static void expand_env(const char *in, char *out, int outlen) {
    int oi = 0;
    int i = 0;
    while (in[i] && oi < outlen - 1) {
        if (in[i] == '$') {
            i++;
            char name[32];
            int ni = 0;
            while (in[i] && ((in[i] >= 'A' && in[i] <= 'Z') ||
                             (in[i] >= 'a' && in[i] <= 'z') ||
                             (in[i] >= '0' && in[i] <= '9') ||
                             in[i] == '_') && ni < 31) {
                name[ni++] = in[i++];
            }
            name[ni] = 0;
            if (ni > 0) {
                const char *val = env_get(name);
                int vl = strlen_(val);
                for (int v = 0; v < vl && oi < outlen - 1; v++)
                    out[oi++] = val[v];
            } else {
                out[oi++] = '$';
            }
        } else {
            out[oi++] = in[i++];
        }
    }
    out[oi] = 0;
}

/* Split a command line into argv (by spaces). Returns argc.
 * Handles quotes ("..." and '...'). */
static int parse_args(char *line, char *argv[], int max_argv) {
    int argc = 0;
    int i = 0;
    while (line[i] && argc < max_argv - 1) {
        /* Skip leading spaces */
        while (line[i] && isspace_(line[i])) i++;
        if (!line[i]) break;
        argv[argc++] = &line[i];
        /* Find end of this token */
        while (line[i] && !isspace_(line[i])) i++;
        if (line[i]) { line[i] = 0; i++; }
    }
    argv[argc] = 0;
    return argc;
}

/* Built-in commands. Return 1 if handled, 0 if not. */
static int builtin_cmd(int argc, char *argv[]) {
    if (argc == 0) return 1;
    if (streq(argv[0], "cd")) {
        if (argc > 1) sys_chdir(argv[1]);
        else sys_chdir("/");
        return 1;
    }
    if (streq(argv[0], "exit")) {
        sys_exit2(argc > 1 ? 0 : 0);
        return 1;
    }
    if (streq(argv[0], "export")) {
        if (argc > 1) {
            char *eq = argv[1];
            while (*eq && *eq != '=') eq++;
            if (*eq == '=') {
                *eq = 0;
                env_set(argv[1], eq + 1);
            } else {
                env_set(argv[1], "");
            }
        }
        return 1;
    }
    if (streq(argv[0], "echo")) {
        for (int i = 1; i < argc; i++) {
            if (i > 1) putc_(' ');
            puts_(argv[i]);
        }
        putc_('\n');
        return 1;
    }
    if (streq(argv[0], "alias")) {
        if (argc < 2) {
            for (int i = 0; i < g_alias_count; i++) {
                puts_(g_alias[i].name);
                puts_("='");
                puts_(g_alias[i].val);
                puts_("'\n");
            }
            return 1;
        }
        char *eq = argv[1];
        while (*eq && *eq != '=') eq++;
        if (*eq == '=') {
            *eq = 0;
            alias_set(argv[1], eq + 1);
        }
        return 1;
    }
    if (streq(argv[0], "unalias")) {
        /* Simple: just overwrite with empty */
        for (int i = 0; i < g_alias_count; i++) {
            if (streq(g_alias[i].name, argv[1])) {
                g_alias[i].name[0] = 0;
                return 1;
            }
        }
        return 1;
    }
    if (streq(argv[0], "history")) {
        for (int i = 0; i < g_history_count; i++) {
            putu_(i + 1);
            putc_(' ');
            puts_(g_history[i]);
            putc_('\n');
        }
        return 1;
    }
    if (streq(argv[0], "pwd")) {
        char buf[256];
        sys_getcwd((long)buf, 256);
        puts_(buf);
        putc_('\n');
        return 1;
    }
    if (streq(argv[0], "set")) {
        if (argc > 1) {
            puts_(argv[1]);
            puts_("=");
            puts_(env_get(argv[1]));
            putc_('\n');
        }
        return 1;
    }
    return 0;
}

/* Execute a single command (no pipes).
 * Handles redirect (> >> <).
 * Returns exit status. */
static int exec_single(char *cmd) {
    /* Parse redirect operators */
    char *redir_out = 0;
    int redir_append = 0;
    char *redir_in = 0;

    /* Find > and < in cmd */
    char *p = cmd;
    char *out_start = cmd;
    while (*p) {
        if (*p == '>') {
            *p = 0;
            p++;
            if (*p == '>') { redir_append = 1; p++; }
            while (*p == ' ') p++;
            redir_out = p;
            while (*p && *p != ' ' && *p != '<') p++;
            if (*p) { *p = 0; p++; }
        } else if (*p == '<') {
            *p = 0;
            p++;
            while (*p == ' ') p++;
            redir_in = p;
            while (*p && *p != ' ' && *p != '>') p++;
            if (*p) { *p = 0; p++; }
        } else {
            p++;
        }
    }

    char *argv[32];
    int argc = parse_args(out_start, argv, 32);
    if (argc == 0) return 0;

    /* Check built-in */
    if (builtin_cmd(argc, argv)) return 0;

    /* Check alias */
    const char *al = alias_get(argv[0]);
    if (al) {
        /* Simple alias: replace argv[0] with the alias value */
        char newcmd[512];
        strcpy_(newcmd, al);
        for (int i = 1; i < argc; i++) {
            strcat_(newcmd, " ");
            strcat_(newcmd, argv[i]);
        }
        /* Re-parse and re-execute (but without redirect, which we already extracted) */
        argc = parse_args(newcmd, argv, 32);
        if (builtin_cmd(argc, argv)) return 0;
    }

    /* Fork and exec */
    long pid = sys_fork();
    if (pid < 0) {
        puts_("ush: fork failed\n");
        return -1;
    }
    if (pid == 0) {
        /* Child: set up redirect */
        if (redir_out) {
            int fd = sys_open(redir_out, redir_append ? 2 : 1);
            if (fd >= 0) {
                sys_dup2(fd, 1);  /* redirect stdout to file */
                sys_close(fd);
            }
        }
        if (redir_in) {
            int fd = sys_open(redir_in, 0);
            if (fd >= 0) {
                sys_dup2(fd, 0);  /* redirect stdin from file */
                sys_close(fd);
            }
        }
        /* Exec */
        long ret = sys_execve((long)argv[0], (long)argv, 0);
        /* If exec failed, try with /bin/ prefix */
        if (ret < 0) {
            char path[64];
            strcpy_(path, "/bin/");
            strcat_(path, argv[0]);
            ret = sys_execve((long)path, (long)argv, 0);
        }
        if (ret < 0) {
            puts_("ush: command not found: ");
            puts_(argv[0]);
            putc_('\n');
        }
        sys_exit2(1);
    }
    /* Parent: wait for child */
    sys_wait4(pid);
    return 0;
}

/* Execute a pipeline: cmd1 | cmd2 | ... | cmdN.
 * Splits by |, creates pipes, forks for each segment. */
static void exec_pipeline(char *line) {
    /* Split by | */
    char *segments[8];
    int nseg = 0;
    char *p = line;
    segments[nseg++] = p;
    while (*p) {
        if (*p == '|') {
            *p = 0;
            p++;
            if (nseg < 8) segments[nseg++] = p;
        } else {
            p++;
        }
    }

    if (nseg == 1) {
        /* No pipe: just exec_single */
        exec_single(segments[0]);
        return;
    }

    /* Create pipes */
    int prev_read = -1;
    for (int i = 0; i < nseg; i++) {
        int pipefds[2] = {0, 0};
        int has_next = (i < nseg - 1);
        if (has_next) {
            sys_pipe((long)pipefds);
        }

        long pid = sys_fork();
        if (pid == 0) {
            /* Child */
            if (prev_read >= 0) {
                sys_dup2(prev_read, 0);
                sys_close(prev_read);
            }
            if (has_next) {
                sys_close(pipefds[0]);  /* close read end */
                sys_dup2(pipefds[1], 1);  /* stdout → pipe write */
                sys_close(pipefds[1]);
            }
            exec_single(segments[i]);
            sys_exit2(0);
        }
        /* Parent */
        if (prev_read >= 0) sys_close(prev_read);
        if (has_next) {
            sys_close(pipefds[1]);  /* close write end */
            prev_read = pipefds[0];
        } else {
            prev_read = -1;
        }
        sys_wait4(pid);
    }
}

/* Main shell loop */
void _start(void) {
    /* Initialize environment */
    env_set("PATH", "/bin:/");
    env_set("HOME", "/");
    env_set("PS1", "ush> ");

    puts_("\nOpen Cube OS User-space Shell (WP-08cd)\n");
    puts_("Type 'help' for built-in commands.\n\n");

    char line[512];
    char expanded[512];

    for (;;) {
        /* Print prompt */
        const char *ps1 = env_get("PS1");
        if (!ps1[0]) ps1 = "ush> ";
        puts_(ps1);

        /* Read a line */
        long len = sys_readline((long)line, 511);
        if (len <= 0) continue;
        /* Remove trailing newline */
        if (len > 0 && line[len - 1] == '\n') line[len - 1] = 0;
        if (len > 0 && line[len - 1] == '\r') line[len - 1] = 0;

        /* Skip empty lines */
        if (!line[0]) continue;

        /* Add to history */
        history_add(line);

        /* Expand $VAR */
        expand_env(line, expanded, 512);

        /* Execute */
        exec_pipeline(expanded);
    }
}
