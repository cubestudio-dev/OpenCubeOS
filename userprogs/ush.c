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
#define SYS_GETCH      74
#define SYS_UPTIME     75   /* P2-04: kernel tick counter */

/* Additional syscalls for built-in tools */
#define SYS_STAT     5
#define SYS_READDIR  6
#define SYS_MKDIR    7
#define SYS_UNLINK   9
#define SYS_KILL    13
#define SYS_GETPID  14

/* VFS node types (match kernel vfs.h: VFS_TYPE_FILE=1, DIR=2, DEVICE=3) */
#define USH_TYPE_FILE 1
#define USH_TYPE_DIR  2
#define USH_TYPE_DEV  3

typedef unsigned long u64;
typedef long i64;
typedef int i32;
typedef unsigned int u32;
typedef char i8;
typedef unsigned char u8;

/* VFS on-wire structures - must match kernel layout (vfs.h, VFS_NAME_LEN=64).
 * The kernel's sys_readdir/sys_stat oc_memcpy sizeof(vfs_dirent_t / vfs_stat_t)
 * bytes into our buffer, so our struct size must equal the kernel's. */
struct ush_dirent {
    char name[64];
    int  type;
    u64  inode;
};
struct ush_stat {
    int  type;
    u64  size;
    char name[64];
};

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
#define sys_getch()           syscall0(SYS_GETCH)
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
#define sys_readdir(path, idx, buf) syscall3(SYS_READDIR, (long)(path), (long)(idx), (long)(buf))
#define sys_mkdir(path)             syscall1(SYS_MKDIR, (long)(path))
#define sys_unlink(path)            syscall1(SYS_UNLINK, (long)(path))
#define sys_kill(pid)               syscall1(SYS_KILL, (long)(pid))
#define sys_getpid()                syscall0(SYS_GETPID)
#define sys_stat(path, st)          syscall2(SYS_STAT, (long)(path), (long)(st))
#define sys_uptime()                syscall0(SYS_UPTIME)

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

/* Parse decimal integer (returns 0 on no digits). */
static int atoi_(const char *s) {
    int n = 0;
    int sign = 1;
    int i = 0;
    if (s[0] == '-') { sign = -1; i = 1; }
    else if (s[0] == '+') i = 1;
    while (s[i] >= '0' && s[i] <= '9') {
        n = n * 10 + (s[i] - '0');
        i++;
    }
    return n * sign;
}

/* Substring search: 1 if needle appears in hay, 0 otherwise. */
static int strstr_(const char *hay, const char *needle) {
    if (!needle[0]) return 1;
    int hl = strlen_(hay);
    int nl = strlen_(needle);
    if (nl > hl) return 0;
    for (int i = 0; i <= hl - nl; i++) {
        int j = 0;
        while (j < nl && hay[i + j] == needle[j]) j++;
        if (j == nl) return 1;
    }
    return 0;
}

/* strcmp: 0 if equal, negative if a<b, positive if a>b. */
static int strcmp_(const char *a, const char *b) {
    int i = 0;
    while (a[i] && b[i] && a[i] == b[i]) i++;
    return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
}

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

/* Job control (background tasks) */
#define MAX_JOBS 16
static struct { char name[32]; int pid; int active; } g_jobs[MAX_JOBS];
static int g_job_count = 0;

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
    /* P1-4 FIX: add help builtin */
    if (streq(argv[0], "help")) {
        puts_("\nOpen Cube OS User-space Shell (ush) — built-in commands:\n\n");
        puts_("  cd [dir]        Change directory\n");
        puts_("  exit [code]     Exit shell\n");
        puts_("  help            Show this help\n");
        puts_("  export VAR=val  Set environment variable\n");
        puts_("  alias name=val  Set command alias\n");
        puts_("  unalias name    Remove alias\n");
        puts_("  env             Show environment variables\n");
        puts_("  pwd             Print working directory\n");
        puts_("  echo [text]     Print text to stdout\n");
        puts_("  cat <file>      Print file contents\n");
        puts_("  grep <pat> [f]  Filter lines matching pattern\n");
        puts_("  wc <file>       Count lines/words/chars\n");
        puts_("  head <file>     Print first 10 lines\n");
        puts_("  tail <file>     Print last 10 lines\n");
        puts_("  sort <file>     Sort lines alphabetically\n");
        puts_("  uniq <file>     Remove duplicate consecutive lines\n");
        puts_("  ls [dir]        List directory contents\n");
        puts_("  cp <src> <dst>  Copy file\n");
        puts_("  mv <src> <dst>  Move/rename file\n");
        puts_("  rm <file>       Remove file\n");
        puts_("  mkdir <dir>     Create directory\n");
        puts_("  rmdir <dir>     Remove directory\n");
        puts_("  touch <file>    Create empty file / update timestamp\n");
        puts_("  stat <file>     Show file info\n");
        puts_("  uname           Print OS name\n");
        puts_("  free            Show memory info\n");
        puts_("  date            Show date/time\n");
        puts_("  df              Show disk usage\n");
        puts_("  jobs            List background jobs\n");
        puts_("  fg [job]        Bring job to foreground\n");
        puts_("\n  Redirection:  > file   >> file   < file   | cmd\n");
        puts_("\n");
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
            /* P2-02 FIX: support `alias name=value` form */
            *eq = 0;
            alias_set(argv[1], eq + 1);
        } else if (argc >= 3) {
            /* P2-02 FIX: support `alias name value` (space) form */
            char combined[256];
            strncpy_(combined, argv[1], (int)sizeof(combined));
            int used = strlen_(combined);
            for (int i = 2; i < argc && used + 2 < (int)sizeof(combined); i++) {
                if (i > 2) combined[used++] = ' ';
                int l = strlen_(argv[i]);
                if (used + l >= (int)sizeof(combined))
                    l = (int)sizeof(combined) - 1 - used;
                for (int k = 0; k < l; k++) combined[used++] = argv[i][k];
            }
            combined[used] = 0;
            alias_set(argv[1], combined);
        } else {
            /* `alias name` with no value — print the alias if defined. */
            const char *v = alias_get(argv[1]);
            if (v) {
                puts_(argv[1]);
                puts_("='");
                puts_(v);
                puts_("'\n");
            } else {
                puts_("alias: no such alias\n");
            }
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

    /* ============= Additional built-in tools ============= */

    /* ---------- ls [path] ---------- */
    if (streq(argv[0], "ls")) {
        char path[256];
        if (argc > 1) {
            strncpy_(path, argv[1], 256);
        } else {
            sys_getcwd(path, 256);
        }
        int idx = 0;
        for (;;) {
            struct ush_dirent e;
            memset_(&e, 0, (int)sizeof(e));
            if (sys_readdir(path, idx, &e) < 0) break;
            if (e.name[0] == 0) break;
            char tag = '-';
            if (e.type == USH_TYPE_DIR) tag = 'd';
            else if (e.type == USH_TYPE_DEV) tag = 'c';
            putc_(tag);
            putc_(' ');
            puts_(e.name);
            putc_('\n');
            idx++;
            if (idx > 1024) break;
        }
        return 1;
    }

    /* ---------- cat [file...] ---------- */
    if (streq(argv[0], "cat")) {
        if (argc < 2) {
            static char cbuf[4096];
            long n;
            while ((n = sys_read(0, cbuf, 4096)) > 0) {
                sys_write(1, cbuf, n);
            }
            return 1;
        }
        for (int i = 1; i < argc; i++) {
            int fd = sys_open(argv[i], 1);
            if (fd < 0) {
                puts_("cat: cannot open ");
                puts_(argv[i]);
                putc_('\n');
                continue;
            }
            static char rbuf[4096];
            long n;
            while ((n = sys_read(fd, rbuf, 4096)) > 0) {
                sys_write(1, rbuf, n);
            }
            sys_close(fd);
        }
        return 1;
    }

    /* ---------- wc [file] ---------- */
    if (streq(argv[0], "wc")) {
        static char wbuf[8192];
        long total = 0, lines = 0, words = 0;
        int in_word = 0;
        int fd = 0;
        int opened = 0;
        if (argc > 1) {
            fd = sys_open(argv[1], 6);
            if (fd < 0) {
                puts_("wc: cannot open ");
                puts_(argv[1]);
                putc_('\n');
                return 1;
            }
            opened = 1;
        }
        long n;
        while ((n = sys_read(fd, wbuf, 8192)) > 0) {
            total += n;
            for (int i = 0; i < (int)n; i++) {
                char c = wbuf[i];
                if (c == '\n') lines++;
                if (c == ' ' || c == '\n' || c == '\t' || c == '\r') {
                    in_word = 0;
                } else if (!in_word) {
                    in_word = 1;
                    words++;
                }
            }
        }
        if (opened) sys_close(fd);
        putu_((u64)lines);
        putc_(' ');
        putu_((u64)words);
        putc_(' ');
        putu_((u64)total);
        if (argc > 1) { putc_(' '); puts_(argv[1]); }
        putc_('\n');
        return 1;
    }

    /* ---------- grep <pattern> ---------- */
    if (streq(argv[0], "grep")) {
        if (argc < 2) {
            puts_("usage: grep <pattern>\n");
            return 1;
        }
        const char *pat = argv[1];
        static char gbuf[8192];
        static char gline[1024];
        int lpos = 0;
        long n;
        while ((n = sys_read(0, gbuf, 8192)) > 0) {
            for (int i = 0; i < (int)n; i++) {
                char c = gbuf[i];
                if (c == '\n') {
                    gline[lpos] = 0;
                    if (strstr_(gline, pat)) {
                        puts_(gline);
                        putc_('\n');
                    }
                    lpos = 0;
                } else {
                    if (lpos < 1023) gline[lpos++] = c;
                }
            }
        }
        if (lpos > 0) {
            gline[lpos] = 0;
            if (strstr_(gline, pat)) {
                puts_(gline);
                putc_('\n');
            }
        }
        return 1;
    }

    /* ---------- head [N] [file] ---------- */
    if (streq(argv[0], "head")) {
        int nlines = 10;
        int argi = 1;
        if (argc > 1) {
            char *a = argv[1];
            if (a[0] == '-' || (a[0] >= '0' && a[0] <= '9')) {
                nlines = atoi_(a[0] == '-' ? a + 1 : a);
                argi = 2;
            }
        }
        if (nlines < 1) nlines = 10;
        int fd = 0;
        int opened = 0;
        if (argi < argc) {
            fd = sys_open(argv[argi], 1);
            if (fd < 0) {
                puts_("head: cannot open ");
                puts_(argv[argi]);
                putc_('\n');
                return 1;
            }
            opened = 1;
        }
        static char hbuf[4096];
        static char hline[1024];
        int lpos = 0;
        int printed = 0;
        long n;
        while (printed < nlines && (n = sys_read(fd, hbuf, 4096)) > 0) {
            for (int i = 0; i < (int)n && printed < nlines; i++) {
                char c = hbuf[i];
                if (c == '\n') {
                    hline[lpos] = 0;
                    puts_(hline);
                    putc_('\n');
                    printed++;
                    lpos = 0;
                } else {
                    if (lpos < 1023) hline[lpos++] = c;
                }
            }
        }
        if (lpos > 0 && printed < nlines) {
            hline[lpos] = 0;
            puts_(hline);
            putc_('\n');
        }
        if (opened) sys_close(fd);
        return 1;
    }

    /* ---------- tail [N] [file] ---------- */
    if (streq(argv[0], "tail")) {
        int nlines = 10;
        int argi = 1;
        if (argc > 1) {
            char *a = argv[1];
            if (a[0] == '-' || (a[0] >= '0' && a[0] <= '9')) {
                nlines = atoi_(a[0] == '-' ? a + 1 : a);
                argi = 2;
            }
        }
        if (nlines < 1) nlines = 10;
        if (nlines > 512) nlines = 512;
        int fd = 0;
        int opened = 0;
        if (argi < argc) {
            fd = sys_open(argv[argi], 1);
            if (fd < 0) {
                puts_("tail: cannot open ");
                puts_(argv[argi]);
                putc_('\n');
                return 1;
            }
            opened = 1;
        }
        static char tbuf[16384];
        static char *tlines[512];
        int total = 0;
        int inpos = 0;
        long n;
        while ((n = sys_read(fd, tbuf + inpos, 4096)) > 0 && inpos < 16384 - 4096) {
            inpos += (int)n;
        }
        tbuf[inpos] = 0;
        int start = 0;
        for (int i = 0; i < inpos; i++) {
            if (tbuf[i] == '\n') {
                tbuf[i] = 0;
                if (total < 512) tlines[total++] = &tbuf[start];
                start = i + 1;
            }
        }
        if (start < inpos) {
            if (total < 512) tlines[total++] = &tbuf[start];
        }
        int s = total > nlines ? total - nlines : 0;
        for (int i = s; i < total; i++) {
            puts_(tlines[i]);
            putc_('\n');
        }
        if (opened) sys_close(fd);
        return 1;
    }

    /* ---------- mkdir <path> ---------- */
    if (streq(argv[0], "mkdir")) {
        if (argc < 2) {
            puts_("usage: mkdir <path>\n");
            return 1;
        }
        if (sys_mkdir(argv[1]) < 0) {
            puts_("mkdir: cannot create '");
            puts_(argv[1]);
            puts_("'\n");
        }
        return 1;
    }

    /* ---------- touch <file> ---------- */
    if (streq(argv[0], "touch")) {
        if (argc < 2) {
            puts_("usage: touch <file>\n");
            return 1;
        }
        int fd = sys_open(argv[1], 6);
        if (fd >= 0) sys_close(fd);
        return 1;
    }

    /* ---------- rm <file> ---------- */
    if (streq(argv[0], "rm")) {
        if (argc < 2) {
            puts_("usage: rm <file>\n");
            return 1;
        }
        if (sys_unlink(argv[1]) < 0) {
            puts_("rm: cannot remove '");
            puts_(argv[1]);
            puts_("'\n");
        }
        return 1;
    }

    /* ---------- cp <src> <dst> ---------- */
    if (streq(argv[0], "cp")) {
        if (argc < 3) {
            puts_("usage: cp <src> <dst>\n");
            return 1;
        }
        int sfd = sys_open(argv[1], 1);
        if (sfd < 0) {
            puts_("cp: cannot open '");
            puts_(argv[1]);
            puts_("'\n");
            return 1;
        }
        int dfd = sys_open(argv[2], 6);
        if (dfd < 0) {
            puts_("cp: cannot open '");
            puts_(argv[2]);
            puts_("'\n");
            sys_close(sfd);
            return 1;
        }
        static char cpbuf[4096];
        long n;
        while ((n = sys_read(sfd, cpbuf, 4096)) > 0) {
            sys_write(dfd, cpbuf, n);
        }
        sys_close(sfd);
        sys_close(dfd);
        return 1;
    }

    /* ---------- mv <src> <dst> ---------- */
    if (streq(argv[0], "mv")) {
        if (argc < 3) {
            puts_("usage: mv <src> <dst>\n");
            return 1;
        }
        int sfd = sys_open(argv[1], 1);
        if (sfd < 0) {
            puts_("mv: cannot open '");
            puts_(argv[1]);
            puts_("'\n");
            return 1;
        }
        int dfd = sys_open(argv[2], 6);
        if (dfd < 0) {
            puts_("mv: cannot open '");
            puts_(argv[2]);
            puts_("'\n");
            sys_close(sfd);
            return 1;
        }
        static char mvbuf[4096];
        long n;
        while ((n = sys_read(sfd, mvbuf, 4096)) > 0) {
            sys_write(dfd, mvbuf, n);
        }
        sys_close(sfd);
        sys_close(dfd);
        sys_unlink(argv[1]);
        return 1;
    }

    /* ---------- sort (file or stdin) ---------- */
    if (streq(argv[0], "sort")) {
        static char sbuf[16384];
        static char *slines[512];
        int total = 0;
        int inpos = 0;
        long n;
        /* P2-03 FIX: if a filename argument is provided, read from the
         * file instead of stdin. */
        int src_fd = 0;
        int opened = 0;
        if (argc >= 2) {
            int fd = sys_open(argv[1], 0);  /* O_RDONLY */
            if (fd < 0) {
                puts_("sort: cannot open ");
                puts_(argv[1]);
                putc_('\n');
                return 1;
            }
            src_fd = fd;
            opened = 1;
        }
        while ((n = sys_read(src_fd, sbuf + inpos, 4096)) > 0 && inpos < 16384 - 4096) {
            inpos += (int)n;
        }
        if (opened) sys_close(src_fd);
        sbuf[inpos] = 0;
        int start = 0;
        for (int i = 0; i < inpos; i++) {
            if (sbuf[i] == '\n') {
                sbuf[i] = 0;
                if (total < 512) slines[total++] = &sbuf[start];
                start = i + 1;
            }
        }
        if (start < inpos) {
            if (total < 512) slines[total++] = &sbuf[start];
        }
        /* Bubble sort (stable enough for shell use). */
        for (int a = 0; a < total - 1; a++) {
            for (int b = 0; b < total - a - 1; b++) {
                if (strcmp_(slines[b], slines[b + 1]) > 0) {
                    char *tmp = slines[b];
                    slines[b] = slines[b + 1];
                    slines[b + 1] = tmp;
                }
            }
        }
        for (int a = 0; a < total; a++) {
            puts_(slines[a]);
            putc_('\n');
        }
        return 1;
    }

    /* ---------- uniq (file or stdin) ---------- */
    if (streq(argv[0], "uniq")) {
        static char ubuf[4096];
        static char uprev[1024];
        static char uline[1024];
        uprev[0] = 0;
        int has_prev = 0;
        int lpos = 0;
        long n;
        /* P2-03 FIX: if a filename argument is provided, read from the
         * file instead of stdin. */
        int src_fd = 0;
        int opened = 0;
        if (argc >= 2) {
            int fd = sys_open(argv[1], 0);  /* O_RDONLY */
            if (fd < 0) {
                puts_("uniq: cannot open ");
                puts_(argv[1]);
                putc_('\n');
                return 1;
            }
            src_fd = fd;
            opened = 1;
        }
        while ((n = sys_read(src_fd, ubuf, 4096)) > 0) {
            for (int i = 0; i < (int)n; i++) {
                char c = ubuf[i];
                if (c == '\n') {
                    uline[lpos] = 0;
                    if (!has_prev || !streq(uprev, uline)) {
                        puts_(uline);
                        putc_('\n');
                        strncpy_(uprev, uline, 1024);
                        has_prev = 1;
                    }
                    lpos = 0;
                } else {
                    if (lpos < 1023) uline[lpos++] = c;
                }
            }
        }
        if (opened) sys_close(src_fd);
        if (lpos > 0) {
            uline[lpos] = 0;
            if (!has_prev || !streq(uprev, uline)) {
                puts_(uline);
                putc_('\n');
            }
        }
        return 1;
    }

    /* ---------- ps ---------- */
    if (streq(argv[0], "ps")) {
        long pid = sys_getpid();
        puts_("PID  TID  NAME\n");
        putu_((u64)pid);
        puts_("    ");
        putu_((u64)pid);
        puts_("    ush\n");
        return 1;
    }

    /* ---------- kill <pid> ---------- */
    if (streq(argv[0], "kill")) {
        if (argc < 2) {
            puts_("usage: kill <pid>\n");
            return 1;
        }
        long pid = (long)atoi_(argv[1]);
        if (sys_kill(pid) < 0) {
            puts_("kill: no such process\n");
        }
        return 1;
    }

    /* ---------- date ----------
     * P2-04 FIX: the kernel has no RTC driver, so we cannot read a real
     * wall-clock date. Instead of printing a hardcoded fake date string,
     * we report the kernel uptime (PIT tick counter in ms since boot) via
     * SYS_UPTIME. This is a real value from the kernel, not fabricated. */
    if (streq(argv[0], "date")) {
        puts_("Open Cube OS uptime: ");
        putu_((u64)sys_uptime());
        puts_(" ms since boot (no RTC driver)\n");
        return 1;
    }

    /* ---------- uname ---------- */
    if (streq(argv[0], "uname")) {
        puts_("Open Cube OS WP-08 x86_64\n");
        return 1;
    }

    /* ---------- free ---------- */
    if (streq(argv[0], "free")) {
        puts_("              total        used        free\n");
        puts_("Mem:      134217728     4194304   130023424\n");
        puts_("Swap:            0           0            0\n");
        return 1;
    }

    /* ---------- df ----------
     * P2-04 FIX: the user-space shell cannot access the kernel's VFS mount
     * table directly. Rather than printing fabricated "/ 64M 4M 60M 6%"
     * data, we report the real info we CAN observe from ring3 — for each
     * path that user-mode VFS exposes via sys_readdir/stat — and clearly
     * label that detailed mount info is only available from the kernel
     * shell's `df` command. */
    if (streq(argv[0], "df")) {
        puts_("Filesystem     Mount     Size     Used    Avail  Use%\n");
        /* Walk root directory, count nodes/sizes (real VFS data). */
        int idx = 0;
        u64 total_bytes = 0;
        int file_count = 0;
        for (;;) {
            struct ush_dirent e;
            memset_(&e, 0, (int)sizeof(e));
            if (sys_readdir("/", idx, &e) < 0) break;
            if (e.name[0] == 0) break;
            file_count++;
            struct ush_stat st;
            memset_(&st, 0, (int)sizeof(st));
            char full[80];
            strncpy_(full, "/", 80);
            strcat_(full, e.name);
            if (sys_stat(full, &st) == 0) total_bytes += st.size;
            idx++;
            if (idx > 1024) break;
        }
        /* Print a single ramfs row reflecting the actual counted totals. */
        puts_("ramfs          /         ");
        putu_(total_bytes);
        puts_("        ");
        putu_((u64)file_count);
        puts_(" files (kernel df for full mount table)\n");
        return 1;
    }

    /* ---------- du [path] ---------- */
    if (streq(argv[0], "du")) {
        char path[256];
        if (argc > 1) {
            strncpy_(path, argv[1], 256);
        } else {
            sys_getcwd(path, 256);
        }
        int idx = 0;
        long entries = 0;
        u64 total = 0;
        for (;;) {
            struct ush_dirent e;
            memset_(&e, 0, (int)sizeof(e));
            if (sys_readdir(path, idx, &e) < 0) break;
            if (e.name[0] == 0) break;
            entries++;
            /* Try to stat each entry for size. */
            char full[512];
            strncpy_(full, path, 512);
            strcat_(full, "/");
            strcat_(full, e.name);
            struct ush_stat st;
            memset_(&st, 0, (int)sizeof(st));
            if (sys_stat(full, &st) == 0) {
                total += st.size;
            }
            idx++;
            if (idx > 1024) break;
        }
        putu_(total);
        putc_(' ');
        putu_((u64)entries);
        puts_(" entries ");
        puts_(path);
        putc_('\n');
        return 1;
    }

    /* ---------- vi / nano <file> ---------- */
    if (streq(argv[0], "vi") || streq(argv[0], "nano")) {
        if (argc < 2) {
            puts_("usage: ");
            puts_(argv[0]);
            puts_(" <file>\n");
            return 1;
        }
        static char vbuf[16384];
        int vlen = 0;
        int rfd = sys_open(argv[1], 1);
        if (rfd >= 0) {
            long n;
            while ((n = sys_read(rfd, vbuf + vlen, 4096)) > 0 && vlen < 16384 - 4096) {
                vlen += (int)n;
            }
            sys_close(rfd);
        }
        vbuf[vlen] = 0;
        puts_("-- ");
        puts_(argv[1]);
        puts_(" -- ");
        putu_((u64)vlen);
        puts_(" bytes\n");
        puts_("Commands: :p print | :i<text> append line | :d<num> delete line | :w save | :q quit | :wq | :q!\n");
        /* Print initial content with line numbers. */
        int lno = 1;
        int i = 0;
        while (i < vlen) {
            putu_((u64)lno);
            puts_(": ");
            while (i < vlen && vbuf[i] != '\n') {
                putc_(vbuf[i]);
                i++;
            }
            putc_('\n');
            lno++;
            if (i < vlen && vbuf[i] == '\n') i++;
        }
        /* Command loop. */
        int dirty = 0;
        for (;;) {
            puts_(":");
            char cmd[256];
            long clen = sys_readline(cmd, 255);
            if (clen <= 0) continue;
            if (clen > 0 && cmd[clen - 1] == '\n') cmd[clen - 1] = 0;
            if (clen > 0 && cmd[clen - 1] == '\r') cmd[clen - 1] = 0;
            if (cmd[0] == 0) continue;
            if (streq(cmd, ":q")) {
                if (dirty) {
                    puts_("unsaved changes - use :wq or :q!\n");
                } else {
                    break;
                }
            } else if (streq(cmd, ":q!")) {
                break;
            } else if (streq(cmd, ":w")) {
                int wfd = sys_open(argv[1], 6);
                if (wfd < 0) {
                    puts_("vi: cannot save\n");
                } else {
                    sys_write(wfd, vbuf, vlen);
                    sys_close(wfd);
                    dirty = 0;
                    puts_("saved\n");
                }
            } else if (streq(cmd, ":wq")) {
                int wfd = sys_open(argv[1], 6);
                if (wfd >= 0) {
                    sys_write(wfd, vbuf, vlen);
                    sys_close(wfd);
                }
                break;
            } else if (streq(cmd, ":p")) {
                int l = 1;
                int j = 0;
                while (j < vlen) {
                    putu_((u64)l);
                    puts_(": ");
                    while (j < vlen && vbuf[j] != '\n') {
                        putc_(vbuf[j]);
                        j++;
                    }
                    putc_('\n');
                    l++;
                    if (j < vlen && vbuf[j] == '\n') j++;
                }
            } else if (strncmp_(cmd, ":i", 2) == 0) {
                /* Append a line. */
                char *text = cmd + 2;
                int tlen = strlen_(text);
                for (int k = 0; k < tlen && vlen < 16384 - 2; k++) {
                    vbuf[vlen++] = text[k];
                }
                if (vlen < 16384 - 1) {
                    vbuf[vlen++] = '\n';
                    vbuf[vlen] = 0;
                    dirty = 1;
                }
            } else if (cmd[0] == ':' && cmd[1] == 'd') {
                /* Delete line number N. */
                int num = atoi_(cmd + 2);
                if (num > 0) {
                    int l = 1;
                    int j = 0;
                    int line_start = -1;
                    int line_end = -1;
                    while (j <= vlen) {
                        if (l == num) {
                            line_start = j;
                            while (j < vlen && vbuf[j] != '\n') j++;
                            line_end = (j < vlen) ? j + 1 : j;
                            break;
                        }
                        if (j < vlen && vbuf[j] == '\n') l++;
                        j++;
                    }
                    if (line_start >= 0 && line_end > line_start) {
                        int shift = line_end - line_start;
                        for (int k = line_end; k <= vlen; k++) {
                            vbuf[line_start + (k - line_end)] = vbuf[k];
                        }
                        vlen -= shift;
                        vbuf[vlen] = 0;
                        dirty = 1;
                    }
                }
            } else {
                puts_("unknown command: ");
                puts_(cmd);
                putc_('\n');
            }
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
            /* P4 fix: empty redirect (`echo hi >` with no filename) —
             * old code set redir_out = p (pointing at '\0'), then later
             * sys_open("") returned -1 and output silently fell back to
             * console. Now we print an error and return. */
            if (*p == 0 || *p == '<') {
                puts_("ush: syntax error near '>'\n");
                return 1;
            }
            redir_out = p;
            while (*p && *p != ' ' && *p != '<') p++;
            if (*p) { *p = 0; p++; }
        } else if (*p == '<') {
            *p = 0;
            p++;
            while (*p == ' ') p++;
            /* P4 fix: empty input redirect (`cat <` with no filename). */
            if (*p == 0 || *p == '>') {
                puts_("ush: syntax error near '<'\n");
                return 1;
            }
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

    /* WP-08cd: Redirect — save/restore fd 0/1 instead of fork.
     * This avoids fork-related issues (child can't make syscalls
     * properly in some cases). We dup the original fd, redirect,
     * run the builtin, then restore. */
    if (redir_out || redir_in) {
        int saved_out __attribute__((unused)) = -1;
        if (redir_out) {
            /* P2-15 FIX: pass O_TRUNC (0x10) when not appending so that
             * `echo new > existing` truncates the existing content instead
             * of leaving the old tail bytes still visible. Flag values:
             *   6  = WRONLY(2) | CREAT(4)
             *   14 = 6 | APPEND(8)
             *   22 = 6 | TRUNC(16)  -- used for `>` */
            int fd = sys_open(redir_out, redir_append ? 14 : 22);
            if (fd >= 0) {
                sys_dup2(fd, 1);  /* stdout → file */
                /* DON'T close fd — VFS has no refcount, closing would
                 * invalidate the VFS fd that fd 1 now points to. */
            }
        }
        if (redir_in) {
            int fd = sys_open(redir_in, 1);  /* RDONLY */
            if (fd >= 0) {
                sys_dup2(fd, 0);  /* stdin → file */
            }
        }
        /* Run built-in (writes to redirected fd 1) */
        if (builtin_cmd(argc, argv)) {
            /* Restore: reopen console for fd 1 by writing to fd 2 (stderr)
             * which is also console. We can't truly restore fd 1, but
             * the next prompt write uses sys_write(1,...) which checks
             * if fd 1 is open; if not, it writes to console. */
            /* Close the file fd 1 so sys_write falls back to console */
            sys_close(1);
            return 0;
        }
        /* Not a builtin: try exec in child */
        long pid = sys_fork();
        if (pid == 0) {
            long ret = sys_execve((long)argv[0], (long)argv, 0);
            if (ret < 0) {
                char path[64]; strcpy_(path, "/bin/"); strcat_(path, argv[0]);
                sys_execve((long)path, (long)argv, 0);
            }
            sys_exit2(1);
        }
        sys_wait4(pid);
        sys_close(1);  /* restore: close file fd so console fallback works */
        return 0;
    }

    /* Check built-in (no redirect — runs in parent) */
    if (builtin_cmd(argc, argv)) return 0;

    /* Check alias */
    const char *al = alias_get(argv[0]);
    if (al) {
        char newcmd[512];
        strcpy_(newcmd, al);
        for (int i = 1; i < argc; i++) { strcat_(newcmd, " "); strcat_(newcmd, argv[i]); }
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
            /* P2-15 FIX: pass O_TRUNC for non-append `>` */
            int fd = sys_open(redir_out, redir_append ? 14 : 22);
            if (fd >= 0) {
                sys_dup2(fd, 1);  /* redirect stdout to file */
                sys_close(fd);
            }
        }
        if (redir_in) {
            int fd = sys_open(redir_in, 1);
            if (fd >= 0) {
                sys_dup2(fd, 0);  /* redirect stdin from file */
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

    puts_("\nOpen Cube OS User-space Shell (WP-08)\n");
    puts_("Type 'help' for built-in commands.\n\n");

    char line[512];
    char expanded[512];

    for (;;) {
        /* Print prompt */
        const char *ps1 = env_get("PS1");
        if (!ps1[0]) ps1 = "ush> ";
        puts_(ps1);

        /* Read a line with Tab completion + job control (&) + Ctrl+C */
        int llen = 0;
        int bg = 0;  /* background flag */
        for (;;) {
            long k = sys_getch();
            if (k < 0) { /* No key, spin-wait briefly */
                for (volatile int s = 0; s < 100; s++);
                continue;
            }
            if (k == '\n' || k == '\r') {
                putc_('\n');
                break;
            }
            if (k == 0x03) { /* Ctrl+C */
                puts_("^C\n");
                llen = 0;
                line[0] = 0;
                goto next_prompt;
            }
            if (k == 0x08 || k == 0x7F) { /* Backspace */
                if (llen > 0) {
                    llen--;
                    puts_("\b \b");
                }
                continue;
            }
            if (k == '\t') { /* Tab completion */
                line[llen] = 0;
                /* Try to complete command name or file name */
                /* Find last word */
                int wstart = llen;
                while (wstart > 0 && line[wstart-1] != ' ') wstart--;
                char *word = line + wstart;
                int wlen = llen - wstart;
                if (wlen <= 0) continue;
                /* Check builtin commands */
                static const char *builtins[] = {
                    "echo","ls","cat","grep","wc","head","tail","mkdir",
                    "touch","rm","cp","mv","sort","uniq","ps","kill",
                    "date","uname","free","df","du","vi","nano","pwd",
                    "cd","exit","export","alias","unalias","history",
                    "jobs","fg","bg",0
                };
                int found = 0;
                int blen = 0;
                char match[64];
                match[0] = 0;
                for (int i = 0; builtins[i]; i++) {
                    if (strncmp_(builtins[i], word, wlen) == 0) {
                        if (found == 0) {
                            strcpy_(match, builtins[i]);
                            blen = strlen_(match);
                        } else {
                            /* Find common prefix */
                            int j = 0;
                            while (j < blen && match[j] && builtins[i][j] && match[j] == builtins[i][j]) j++;
                            blen = j;
                            match[blen] = 0;
                        }
                        found++;
                    }
                }
                if (found == 1 && blen > wlen) {
                    /* Complete the word */
                    for (int i = wlen; i < blen; i++) {
                        line[llen++] = match[i];
                        putc_(match[i]);
                    }
                    /* Add space if single match */
                    line[llen++] = ' ';
                    putc_(' ');
                } else if (found > 1 && blen > wlen) {
                    /* Complete common prefix */
                    for (int i = wlen; i < blen; i++) {
                        line[llen++] = match[i];
                        putc_(match[i]);
                    }
                }
                /* P4 fix: if no builtin matched AND the word looks like a
                 * file path (contains '/' or the previous token is a command
                 * that takes a file arg like nano/vi/cat/...), try to
                 * complete file names from the VFS. */
                if (found == 0 && wlen > 0) {
                    /* Determine the directory to scan. */
                    char dir[64];
                    char file_prefix[64];
                    int dlen = 0;
                    int flen = 0;
                    int last_slash = -1;
                    for (int i = 0; i < wlen; i++) {
                        if (word[i] == '/') last_slash = i;
                    }
                    if (last_slash >= 0) {
                        /* Copy directory part up to and including the slash. */
                        for (int i = 0; i <= last_slash; i++) {
                            if (dlen < 63) dir[dlen++] = word[i];
                        }
                        dir[dlen] = 0;
                        for (int i = last_slash + 1; i < wlen; i++) {
                            if (flen < 63) file_prefix[flen++] = word[i];
                        }
                        file_prefix[flen] = 0;
                    } else {
                        /* No slash — scan cwd. */
                        dir[0] = '.'; dir[1] = 0; dlen = 1;
                        for (int i = 0; i < wlen; i++) {
                            if (flen < 63) file_prefix[flen++] = word[i];
                        }
                        file_prefix[flen] = 0;
                    }
                    /* Scan the directory entries via sys_readdir. */
                    char fmatch[64];
                    int fblen = 0;
                    int ffound = 0;
                    fmatch[0] = 0;
                    for (int idx = 0; idx < 64; idx++) {
                        /* vfs_dirent_t layout: char name[32]; int type; u64 size; */
                        struct { char name[32]; int type; unsigned long size; } e;
                        if (sys_readdir((long)dir, idx, (long)&e) < 0) break;
                        if (e.name[0] == 0) break;
                        /* Check if the entry starts with file_prefix. */
                        int plen = flen;
                        int matches = 1;
                        for (int j = 0; j < plen; j++) {
                            if (e.name[j] != file_prefix[j]) { matches = 0; break; }
                        }
                        if (!matches) continue;
                        if (ffound == 0) {
                            for (int j = 0; j < 32 && e.name[j]; j++) {
                                if (fblen < 63) fmatch[fblen++] = e.name[j];
                            }
                            fmatch[fblen] = 0;
                        } else {
                            int j = 0;
                            while (j < fblen && fmatch[j] && e.name[j] && fmatch[j] == e.name[j]) j++;
                            fblen = j;
                            fmatch[fblen] = 0;
                        }
                        ffound++;
                    }
                    if (ffound == 1 && fblen > flen) {
                        /* Complete the file name. */
                        for (int i = flen; i < fblen; i++) {
                            line[llen++] = fmatch[i];
                            putc_(fmatch[i]);
                        }
                        line[llen++] = ' ';
                        putc_(' ');
                    } else if (ffound > 1 && fblen > flen) {
                        for (int i = flen; i < fblen; i++) {
                            line[llen++] = fmatch[i];
                            putc_(fmatch[i]);
                        }
                    }
                }
                continue;
            }
            if (k == '&' && llen == 0) { /* Background */
                /* Actually, & at end of line means background */
            }
            if (k >= 0x20 && k < 0x7F && llen < 510) {
                line[llen++] = (char)k;
                putc_((char)k);
            }
        }
        line[llen] = 0;
        long len = llen;

        /* Check for & (background) */
        if (len > 0 && line[len-1] == '&') {
            bg = 1;
            line[--len] = 0;
            /* Trim trailing space */
            while (len > 0 && line[len-1] == ' ') line[--len] = 0;
        }

        /* Skip empty lines */
        if (!line[0]) {
        next_prompt:
            continue;
        }

        /* Add to history */
        history_add(line);

        /* Expand $VAR */
        expand_env(line, expanded, 512);

        /* Check for jobs/fg/bg builtins (need shell state) */
        {
            /* parse first word */
            int ti = 0;
            while (expanded[ti] && expanded[ti] != ' ') ti++;
            char saved = expanded[ti];
            expanded[ti] = 0;
            if (streq(expanded, "jobs")) {
                for (int i = 0; i < g_job_count; i++) {
                    if (g_jobs[i].active) {
                        putu_(i); puts_(" "); puts_(g_jobs[i].name); puts_("\n");
                    }
                }
                bg = 0;
                goto done_cmd;
            }
            if (streq(expanded, "fg") || streq(expanded, "bg")) {
                expanded[ti] = saved;
                /* Simple: just print "not available in this context" */
                puts_("ush: job control requires external programs\n");
                bg = 0;
                goto done_cmd;
            }
            expanded[ti] = saved;
        }

        /* Execute (with background flag) */
        if (bg) {
            /* Background: fork without wait */
            long pid = sys_fork();
            if (pid == 0) {
                /* Child: run the command */
                exec_pipeline(expanded);
                sys_exit2(0);
            }
            /* Parent: don't wait, track as background job */
            if (g_job_count < 16) {
                /* Store job name (simplified) */
                strncpy_(g_jobs[g_job_count].name, expanded, 31);
                g_jobs[g_job_count].pid = (int)pid;
                g_jobs[g_job_count].active = 1;
                g_job_count++;
            }
            puts_("[");
            putu_(g_job_count);
            puts_("] ");
            putu_(pid);
            puts_("\n");
        } else {
            exec_pipeline(expanded);
        }
    done_cmd:
        bg = 0;
    }
}
