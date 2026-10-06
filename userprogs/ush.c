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
#define SYS_MEMINFO    76   /* WP-09-FIX BUG-014: real PMM stats */

/* Additional syscalls for built-in tools */
#define SYS_STAT     5
#define SYS_READDIR  6
#define SYS_MKDIR    7
#define SYS_RMDIR    8
#define SYS_UNLINK   9
#define SYS_KILL    13
#define SYS_GETPID  14

/* WP-10-wp08fix1: links, permissions, network bridge, process table */
#define SYS_SYMLINK  96
#define SYS_READLINK 97
#define SYS_LINK     98
#define SYS_CHMOD    99
#define SYS_CHOWN   100
#define SYS_NETCMD  101
#define SYS_PS      102
#define NETCMD_IFCONFIG  1
#define NETCMD_PING      2
#define NETCMD_NETSTAT   3
#define NETCMD_WGET      4

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
 * The kernel's sys_readdir/sys_stat memcpy sizeof(vfs_dirent_t / vfs_stat_t)
 * bytes into our buffer, so our struct size must equal the kernel's.
 * WP-10-wp08fix1: fs_vfs_stat_t grew mode/uid/gid/nlink - mirrored here. */
struct ush_dirent {
    char name[64];
    int  type;
    u64  inode;
};
struct ush_stat {
    int  type;
    u64  size;
    u32  mode;
    u32  uid;
    u32  gid;
    u32  nlink;
    char name[64];
};

/* WP-10-wp08fix1: kernel process table entry - MUST match the layout the
 * kernel writes in sys_ps (tid, state, priority, name[32], cpu ticks). */
struct ush_ps_entry {
    i32 tid;
    i32 state;
    i32 priority;
    char name[32];
    u64 cpu_time_ticks;
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
#define sys_dup(old)             syscall2(SYS_DUP, (old), 0)
#define sys_dup2(old, new)       syscall2(SYS_DUP2, (old), (new))
#define sys_chdir(path)          syscall1(SYS_CHDIR, (long)(path))
#define sys_getcwd(buf, size)    syscall2(SYS_GETCWD, (long)(buf), (size))
#define sys_open(path, flags)    syscall2(SYS_OPEN, (long)(path), (flags))
#define sys_close(fd)            syscall1(SYS_CLOSE, (fd))
#define sys_readdir(path, idx, buf) syscall3(SYS_READDIR, (long)(path), (long)(idx), (long)(buf))
#define sys_mkdir(path)             syscall1(SYS_MKDIR, (long)(path))
#define sys_rmdir(path)             syscall1(SYS_RMDIR, (long)(path))
#define sys_unlink(path)            syscall1(SYS_UNLINK, (long)(path))
#define sys_kill(pid)               syscall1(SYS_KILL, (long)(pid))
#define sys_getpid()                syscall0(SYS_GETPID)
#define sys_stat(path, st)          syscall2(SYS_STAT, (long)(path), (long)(st))
#define sys_uptime()                syscall0(SYS_UPTIME)
#define sys_meminfo(buf)            syscall1(SYS_MEMINFO, (long)(buf))
#define sys_symlink(target, linkp)  syscall2(SYS_SYMLINK, (long)(target), (long)(linkp))
#define sys_readlink(path, buf, cap) syscall3(SYS_READLINK, (long)(path), (long)(buf), (long)(cap))
#define sys_link(oldp, newp)        syscall2(SYS_LINK, (long)(oldp), (long)(newp))
#define sys_chmod(path, mode)       syscall2(SYS_CHMOD, (long)(path), (long)(mode))
#define sys_chown(path, uid, gid)   syscall3(SYS_CHOWN, (long)(path), (long)(uid), (long)(gid))
#define sys_netcmd(op, arg, out, cap) syscall4_(SYS_NETCMD, (long)(op), (long)(arg), (long)(out), (long)(cap))
#define sys_ps(buf, cap)            syscall2(SYS_PS, (long)(buf), (long)(cap))

/* 4-argument syscall (WP-10-wp08fix1: SYS_NETCMD needs it). */
static inline long syscall4_(long n, long a, long b, long c, long d) {
    long r;
    register long r10 __asm__("r10") = d;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10) : "rcx", "r11", "memory");
    return r;
}

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
    /* WP-10-wp08fix1: skip a consecutive duplicate (bash-like). Without
     * this, recalling a command and running it again pushed the same
     * line twice, which shifted the Up/Down paging position and broke
     * history navigation. */
    if (g_history_count > 0 &&
        strncmp_(g_history[g_history_count - 1], line, 256) == 0) {
        return;
    }
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
        /* WP-10-wp08fix1: single-quoted tokens keep their spaces and are
         * stripped, so `awk '{print $2}' f` arrives as ONE argument
         * ({print $2}) like a real shell. */
        if (line[i] == '\'') {
            i++;
            argv[argc++] = &line[i];
            while (line[i] && line[i] != '\'') i++;
            if (line[i]) { line[i] = 0; i++; }
            continue;
        }
        argv[argc++] = &line[i];
        /* Find end of this token */
        while (line[i] && !isspace_(line[i])) i++;
        if (line[i]) { line[i] = 0; i++; }
    }
    argv[argc] = 0;
    return argc;
}

/* ==================================================================
 * WP-10-wp08fix1: nano-style editor for ush (real interactive editing)
 * ==================================================================
 * Keys: arrows select/move, typing inserts, BKSP/DEL delete,
 * ENTER splits the line, ^K deletes the line, ^C reports the position,
 * ^O saves, ^X exits (asks before discarding), ^G shows the key guide.
 * Rendering: print the file once, then redraw only the CURRENT line
 * in place with \r (the console has no ANSI escapes). */

#define UNANO_MAX_LINES 256
#define UNANO_LINE_CAP  128

static char unano_lines[UNANO_MAX_LINES][UNANO_LINE_CAP];
static int  unano_nlines;
static int  unano_dirty;
static int  unano_cl;    /* current line */
static int  unano_cc;    /* cursor col */

static void unano_prefix(int lineno) {
    char num[12];
    putu_((u64)lineno);
    /* pad to width 3 + ": " */
    int n = 0;
    int v = lineno;
    while (v > 0) { v /= 10; n++; }
    while (n < 3) { putc_(' '); n++; }
    (void)num;
    putc_(':');
    putc_(' ');
}

static void unano_redraw_line(void) {
    char cr = '\r';
    char sp = ' ';
    sys_write(1, &cr, 1);
    unano_prefix(unano_cl + 1);
    sys_write(1, unano_lines[unano_cl], strlen_(unano_lines[unano_cl]));
    sys_write(1, &sp, 1);
    sys_write(1, &cr, 1);
    unano_prefix(unano_cl + 1);
    if (unano_cc > 0) sys_write(1, unano_lines[unano_cl], unano_cc);
}

static void unano_state(const char *file) {
    puts_("[nano ");
    puts_(file);
    puts_(" line ");
    putu_((u64)(unano_cl + 1));
    putc_('/');
    putu_((u64)unano_nlines);
    puts_(unano_dirty ? "  modified] ^O save  ^X exit  ^K del-line  ^C pos  ^G help\n"
                       : "  saved   ] ^O save  ^X exit  ^K del-line  ^C pos  ^G help\n");
}

static void unano_print_all(const char *file) {
    puts_("-- nano: ");
    puts_(file);
    puts_(" -- ^O save ^X exit ^K del line ^G help --\n");
    for (int i = 0; i < unano_nlines; i++) {
        unano_prefix(i + 1);
        puts_(unano_lines[i]);
        putc_('\n');
    }
}

static int unano_getch_blocking(void) {
    for (;;) {
        long k = sys_getch();
        if (k >= 0) return (int)k;
        for (volatile int s = 0; s < 100; s++);
    }
}

static int unano_save(const char *file) {
    /* BUG-0089 FIX (A16-2): save with O_TRUNC - without it, saving a
     * SHORTER document over a longer existing file left the old tail
     * bytes behind (corrupted file). */
    int fd = sys_open(file, 22);   /* WRONLY|CREAT|TRUNC */
    if (fd < 0) return -1;
    for (int i = 0; i < unano_nlines; i++) {
        sys_write(fd, unano_lines[i], strlen_(unano_lines[i]));
        sys_write(fd, "\n", 1);
    }
    sys_close(fd);
    unano_dirty = 0;
    return 0;
}

static void ush_nano_run(const char *file) {
    /* load */
    unano_nlines = 0;
    unano_dirty = 0;
    unano_cl = 0;
    unano_cc = 0;
    int fd = sys_open(file, 1);
    if (fd >= 0) {
        static char rbuf[4096];
        long n;
        char cur[UNANO_LINE_CAP];
        int cl = 0;
        cur[0] = 0;
        while ((n = sys_read(fd, rbuf, 4096)) > 0) {
            for (int i = 0; i < (int)n; i++) {
                if (rbuf[i] == '\n') {
                    if (unano_nlines < UNANO_MAX_LINES) {
                        strncpy_(unano_lines[unano_nlines++], cur, UNANO_LINE_CAP);
                    }
                    cl = 0;
                    cur[0] = 0;
                } else if (cl + 1 < UNANO_LINE_CAP) {
                    cur[cl++] = rbuf[i];
                    cur[cl] = 0;
                }
            }
        }
        sys_close(fd);
        if (cl > 0 || unano_nlines == 0) {
            if (unano_nlines < UNANO_MAX_LINES) {
                strncpy_(unano_lines[unano_nlines++], cur, UNANO_LINE_CAP);
            }
        }
    } else {
        unano_lines[0][0] = 0;
        unano_nlines = 1;
    }

    unano_print_all(file);
    unano_state(file);
    unano_redraw_line();

    for (;;) {
        int k = unano_getch_blocking();
        if (k == 0x80) {          /* Up */
            if (unano_cl > 0) {
                unano_cl--;
                int ll = strlen_(unano_lines[unano_cl]);
                if (unano_cc > ll) unano_cc = ll;
                unano_redraw_line();
            }
        } else if (k == 0x81) {   /* Down */
            if (unano_cl + 1 < unano_nlines) {
                unano_cl++;
                int ll = strlen_(unano_lines[unano_cl]);
                if (unano_cc > ll) unano_cc = ll;
                unano_redraw_line();
            }
        } else if (k == 0x82) {   /* Left */
            if (unano_cc > 0) { unano_cc--; unano_redraw_line(); }
        } else if (k == 0x83) {   /* Right */
            if (unano_cc < strlen_(unano_lines[unano_cl])) {
                unano_cc++; unano_redraw_line();
            }
        } else if (k == 0x84 || k == 0x01) {   /* Home / ^A */
            unano_cc = 0; unano_redraw_line();
        } else if (k == 0x85 || k == 0x05) {   /* End / ^E */
            unano_cc = strlen_(unano_lines[unano_cl]); unano_redraw_line();
        } else if (k == 0x89) {   /* Delete */
            char *l = unano_lines[unano_cl];
            int ll = strlen_(l);
            if (unano_cc < ll) {
                for (int i = unano_cc; i < ll; i++) l[i] = l[i + 1];
                unano_dirty = 1;
                unano_redraw_line();
            }
        } else if (k == 0x08 || k == 0x7F) {   /* Backspace */
            char *l = unano_lines[unano_cl];
            if (unano_cc > 0) {
                int ll = strlen_(l);
                for (int i = unano_cc - 1; i < ll; i++) l[i] = l[i + 1];
                unano_cc--;
                unano_dirty = 1;
                unano_redraw_line();
            } else if (unano_cl > 0) {
                /* merge with previous line */
                char *prev = unano_lines[unano_cl - 1];
                int plen = strlen_(prev);
                int ll = strlen_(l);
                if (plen + ll < UNANO_LINE_CAP - 1) {
                    strcat_(prev, l);
                    unano_cc = plen;
                    unano_cl--;
                    for (int i = unano_cl + 1; i + 1 < unano_nlines; i++) {
                        strncpy_(unano_lines[i], unano_lines[i + 1], UNANO_LINE_CAP);
                    }
                    unano_nlines--;
                    unano_dirty = 1;
                    unano_print_all(file);
                    unano_state(file);
                    unano_redraw_line();
                }
            }
        } else if (k == '\n' || k == '\r') {   /* ENTER: split line */
            if (unano_nlines < UNANO_MAX_LINES) {
                char *l = unano_lines[unano_cl];
                char tail[UNANO_LINE_CAP];
                strncpy_(tail, l + unano_cc, UNANO_LINE_CAP);
                l[unano_cc] = 0;
                /* shift lines down */
                for (int i = unano_nlines; i > unano_cl + 1; i--) {
                    strncpy_(unano_lines[i], unano_lines[i - 1], UNANO_LINE_CAP);
                }
                strncpy_(unano_lines[unano_cl + 1], tail, UNANO_LINE_CAP);
                unano_nlines++;
                unano_cl++;
                unano_cc = 0;
                unano_dirty = 1;
                unano_print_all(file);
                unano_state(file);
                unano_redraw_line();
            }
        } else if (k == 0x0B) {   /* ^K: delete line */
            for (int i = unano_cl; i + 1 < unano_nlines; i++) {
                strncpy_(unano_lines[i], unano_lines[i + 1], UNANO_LINE_CAP);
            }
            unano_nlines--;
            if (unano_nlines == 0) {
                unano_lines[0][0] = 0;
                unano_nlines = 1;
            }
            if (unano_cl >= unano_nlines) unano_cl = unano_nlines - 1;
            int ll = strlen_(unano_lines[unano_cl]);
            if (unano_cc > ll) unano_cc = ll;
            unano_dirty = 1;
            unano_print_all(file);
            unano_state(file);
            unano_redraw_line();
        } else if (k == 0x03) {   /* ^C: report position (nano binding) */
            puts_("\n[line ");
            putu_((u64)(unano_cl + 1));
            puts_(", col ");
            putu_((u64)(unano_cc + 1));
            puts_("]\n");
            unano_state(file);
            unano_redraw_line();
        } else if (k == 0x07) {   /* ^G: help */
            puts_("\narrows move | type to insert | BKSP/DEL delete\n");
            puts_("ENTER split line | ^K del line | ^O save | ^X exit | ^C pos\n");
            unano_state(file);
            unano_redraw_line();
        } else if (k == 0x0F) {   /* ^O: save */
            if (unano_save(file) == 0) {
                puts_("\n[wrote ");
                putu_((u64)unano_nlines);
                puts_(" lines]\n");
            } else {
                puts_("\n[save failed]\n");
            }
            unano_state(file);
            unano_redraw_line();
        } else if (k == 0x18) {   /* ^X: exit */
            if (unano_dirty) {
                puts_("\nSave modified buffer? (y/n): ");
                int c = unano_getch_blocking();
                putc_((char)c);
                putc_('\n');
                if (c == 'y' || c == 'Y') {
                    if (unano_save(file) != 0) {
                        puts_("[save failed - stay in editor]\n");
                        unano_state(file);
                        unano_redraw_line();
                        continue;
                    }
                    return;
                }
                return;   /* quit without saving */
            }
            return;
        } else if (k >= 0x20 && k < 0x7F) {   /* insert */
            char *l = unano_lines[unano_cl];
            int ll = strlen_(l);
            if (ll + 1 < UNANO_LINE_CAP) {
                for (int i = ll; i > unano_cc; i--) l[i] = l[i - 1];
                l[unano_cc] = (char)k;
                unano_cc++;
                unano_dirty = 1;
                unano_redraw_line();
            }
        }
        /* other keys ignored */
    }
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
        puts_("  history         Show command history\n");
        puts_("  cat <file>      Print file contents\n");
        puts_("  grep <pat> [f]  Filter lines matching pattern\n");
        puts_("  wc <file>       Count lines/words/chars\n");
        puts_("  head <file>     Print first 10 lines\n");
        puts_("  tail <file>     Print last 10 lines\n");
        puts_("  sort <file>     Sort lines alphabetically\n");
        puts_("  uniq <file>     Remove duplicate consecutive lines\n");
        puts_("  sed <script>    Stream editor (s/old/new/g, -n Np, Nd)\n");
        puts_("  awk '{print $N}' Print a field / NF / NR per line\n");
        puts_("  ls [dir]        List directory contents\n");
        puts_("  cp <src> <dst>  Copy file\n");
        puts_("  mv <src> <dst>  Move/rename file\n");
        puts_("  rm <file>       Remove file\n");
        puts_("  ln [-s] <a> <b> Hard link, or -s symlink (abs target)\n");
        puts_("  chmod <m> <f>   Change permission bits (octal)\n");
        puts_("  chown <u>:<g> <f> Change owner (numeric)\n");
        puts_("  mkdir <dir>     Create directory\n");
        puts_("  rmdir <dir>     Remove directory\n");
        puts_("  touch <file>    Create empty file / update timestamp\n");
        puts_("  stat <file>     Show file info\n");
        puts_("  uname           Print OS name\n");
        puts_("  free            Show memory info\n");
        puts_("  date            Show date/time\n");
        puts_("  df              Show disk usage\n");
        puts_("  du [dir]        Directory usage (recursive)\n");
        puts_("  ps              List all kernel tasks (TID/state/prio)\n");
        puts_("  kill <pid>      Kill a task\n");
        puts_("  top [-n N]      Snapshot + refresh process/memory stats\n");
        puts_("  ping <host>     ICMP echo via the kernel stack\n");
        puts_("  wget <h> [p] <path> Download via the kernel HTTP client\n");
        puts_("  netstat         Network statistics / sockets\n");
        puts_("  ifconfig        Network interface info\n");
        puts_("  nano <file>     nano-style editor (^O save ^X exit)\n");
        puts_("  vi <file>       Same editor, vi-compatible name\n");
        puts_("  jobs            List background jobs\n");
        puts_("  fg [job]        Bring job to foreground\n");
        puts_("\n  Editing: Up/Down history, Left/Right/Home/End cursor, Del,\n");
        puts_("  Ctrl+A/E/U/K/W, Ctrl+C cancel, Tab completion\n");
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
        /* BUG-0088 FIX (A16-1): with no argument argv[1] is NULL and
         * streq dereferenced it, crashing ush. Require a name. */
        if (!argv[1]) {
            puts_("usage: unalias name\n");
            return 1;
        }
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
    /* WP-10-wp08fix1: `env` was listed in `help` since WP-08 but was never
     * actually implemented - the dispatch table jumped from `set` straight
     * to the file tools. Print the REAL environment table. */
    if (streq(argv[0], "env")) {
        for (int i = 0; i < g_env_count; i++) {
            puts_(g_env[i].name);
            putc_('=');
            puts_(g_env[i].val);
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

    /* ---------- rmdir <path> ---------- */
    if (streq(argv[0], "rmdir")) {
        if (argc < 2) {
            puts_("usage: rmdir <path>\n");
            return 1;
        }
        if (sys_rmdir(argv[1]) < 0) {
            puts_("rmdir: cannot remove '");
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
        /* BUG-0089 FIX (A16-2): O_TRUNC on the copy target - an existing
         * longer destination kept its stale tail after a shorter copy. */
        int dfd = sys_open(argv[2], 22);
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
        /* BUG-0089 FIX (A16-2): O_TRUNC on the move target - an existing
         * longer destination kept its stale tail after a shorter move. */
        int dfd = sys_open(argv[2], 22);
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

    /* ---------- ps (WP-10-wp08fix1: real kernel process table) ---------- */
    if (streq(argv[0], "ps")) {
        static struct ush_ps_entry ptab[32];
        long n = sys_ps((long)ptab, (long)sizeof(ptab));
        if (n < 0) {
            puts_("ps: SYS_PS unavailable (kernel too old)\n");
            return 1;
        }
        puts_("TID  STATE  PRI  CPU(ticks)  NAME\n");
        for (long i = 0; i < n; i++) {
            putu_((u64)ptab[i].tid);
            puts_("    ");
            switch (ptab[i].state) {
                case 0: puts_("READY "); break;
                case 1: puts_("RUN   "); break;
                case 2: puts_("BLOCK "); break;
                case 3: puts_("EXIT  "); break;
                default: puts_("?     "); break;
            }
            putu_((u64)ptab[i].priority);
            puts_("    ");
            putu_(ptab[i].cpu_time_ticks);
            puts_("          ");
            puts_(ptab[i].name);
            putc_('\n');
        }
        return 1;
 }

    /* ---------- top (WP-10-wp08fix1: snapshot + optional refresh) ----------
     * Rolling-screen TUI is impossible on a scrolling framebuffer console,
     * so top prints a REAL snapshot (scheduler stats + memory + process
     * table).  -n N refreshes N times, one second apart (uptime spin). */
    if (streq(argv[0], "top")) {
        int rounds = 1;
        if (argc > 2 && streq(argv[1], "-n")) rounds = atoi_(argv[2]);
        if (rounds < 1) rounds = 1;
        if (rounds > 10) rounds = 10;
        for (int r = 0; r < rounds; r++) {
            if (r > 0) {
                /* spin ~1 second between refreshes */
                u64 start = (u64)sys_uptime();
                while ((u64)sys_uptime() - start < 1000) { }
            }
            u64 now = (u64)sys_uptime();
            puts_("top - up ");
            putu_(now);
            puts_(" ms\n");
            unsigned long long mi[3];
            if (sys_meminfo((long)mi) == 0) {
                puts_("Mem: ");
                putu_(mi[1]);
                puts_(" used / ");
                putu_(mi[0]);
                puts_(" total / ");
                putu_(mi[2]);
                puts_(" free\n");
            }
            static struct ush_ps_entry ptab[32];
            long n = sys_ps((long)ptab, (long)sizeof(ptab));
            if (n >= 0) {
                puts_("TID  STATE  PRI  CPU(ticks)  NAME\n");
                for (long i = 0; i < n; i++) {
                    putu_((u64)ptab[i].tid);
                    puts_("    ");
                    switch (ptab[i].state) {
                        case 0: puts_("READY "); break;
                        case 1: puts_("RUN   "); break;
                        case 2: puts_("BLOCK "); break;
                        case 3: puts_("EXIT  "); break;
                        default: puts_("?     "); break;
                    }
                    putu_((u64)ptab[i].priority);
                    puts_("    ");
                    putu_(ptab[i].cpu_time_ticks);
                    puts_("          ");
                    puts_(ptab[i].name);
                    putc_('\n');
                }
            }
            if (r + 1 < rounds) puts_("\n");
        }
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
        /* WP-09-FIX BUG-014: query real PMM numbers via SYS_MEMINFO
         * instead of printing hardcoded fake data (the old line claimed
         * 128 MiB total on a 512 MiB VM). Falls back to an honest
         * message if the syscall is unavailable. */
        unsigned long long mi[3];
        if (sys_meminfo((long)mi) == 0) {
            puts_("              total        used        free\n");
            puts_("Mem:     ");
            putu_(mi[0]); puts_(" ");
            putu_(mi[1]); puts_(" ");
            putu_(mi[2]); puts_("\n");
            puts_("Swap:            0           0            0\n");
        } else {
            puts_("free: SYS_MEMINFO unavailable (kernel too old)\n");
        }
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

    /* ---------- stat <path> (WP-10-wp08fix1) ----------
     * `help` has listed `stat` since WP-08 but no dispatcher entry ever
     * existed. Print the REAL fs_vfs_stat_t data (type/size/mode/uid/gid/
     * nlink/name) returned by SYS_STAT. */
    if (streq(argv[0], "stat")) {
        if (argc < 2) {
            puts_("usage: stat <path>\n");
            return 1;
        }
        struct ush_stat st;
        memset_(&st, 0, (int)sizeof(st));
        if (sys_stat(argv[1], &st) < 0) {
            puts_("stat: cannot stat '");
            puts_(argv[1]);
            puts_("'\n");
            return 1;
        }
        puts_("  File: ");
        puts_(st.name[0] ? st.name : argv[1]);
        putc_('\n');
        puts_("  Type: ");
        if (st.type == USH_TYPE_DIR) puts_("directory");
        else if (st.type == USH_TYPE_DEV) puts_("device");
        else puts_("regular file");
        putc_('\n');
        puts_("  Size: ");
        putu_(st.size);
        puts_(" bytes\n");
        /* mode in octal (e.g. 0600) */
        puts_("  Mode: ");
        {
            char mb[8];
            int mv = (int)(st.mode & 07777);
            mb[5] = 0;
            for (int d = 4; d >= 1; d--) { mb[d] = (char)('0' + (mv & 7)); mv >>= 3; }
            mb[0] = '0';   /* leading 0 for octal display */
            puts_(mb);
        }
        putc_('\n');
        puts_("  Uid:  ");
        putu_((u64)st.uid);
        puts_("  Gid: ");
        putu_((u64)st.gid);
        putc_('\n');
        puts_("  Links: ");
        putu_((u64)st.nlink);
        putc_('\n');
        return 1;
    }

    /* ---------- du [path] (WP-10-wp08fix1: recursive) ---------- */
    if (streq(argv[0], "du")) {
        char path[256];
        if (argc > 1) {
            strncpy_(path, argv[1], 256);
        } else {
            sys_getcwd(path, 256);
        }
        /* Recursive walker: sums file sizes under path. */
        static u64 du_total;
        static long du_files;
        du_total = 0;
        du_files = 0;
        /* Iterative stack of (path) strings - depth-first walk. */
        static char stack[16][256];
        int sp = 0;
        strncpy_(stack[sp++], path, 256);
        while (sp > 0) {
            char cur[256];
            strncpy_(cur, stack[--sp], 256);
            int idx = 0;
            for (;;) {
                struct ush_dirent e;
                memset_(&e, 0, (int)sizeof(e));
                if (sys_readdir(cur, idx, &e) < 0) break;
                if (e.name[0] == 0) break;
                char full[512];
                strncpy_(full, cur, 500);
                int fl = strlen_(full);
                if (fl > 0 && full[fl - 1] != '/' && fl < 500) full[fl++] = '/';
                full[fl] = 0;
                strcat_(full, e.name);
                struct ush_stat st;
                memset_(&st, 0, (int)sizeof(st));
                if (sys_stat(full, &st) == 0) {
                    if (st.type == USH_TYPE_DIR) {
                        if (sp < 16) {
                            strncpy_(stack[sp++], full, 256);
                        }
                    } else {
                        du_total += st.size;
                        du_files++;
                    }
                }
                idx++;
                if (idx > 1024) break;
            }
        }
        putu_(du_total);
        puts_(" bytes  ");
        putu_((u64)du_files);
        puts_(" files  ");
        puts_(path);
        putc_('\n');
        return 1;
    }

    /* ================= WP-10-wp08fix1: new tools ================= */

    /* ---------- ln [-s] <target/old> <linkpath> ---------- */
    if (streq(argv[0], "ln")) {
        if (argc >= 4 && streq(argv[1], "-s")) {
            if (sys_symlink((long)argv[2], (long)argv[3]) < 0) {
                puts_("ln: cannot create symlink (target must be an absolute path)\n");
            }
            return 1;
        }
        if (argc < 3) {
            puts_("usage: ln <old> <new> | ln -s <abs-target> <linkpath>\n");
            return 1;
        }
        long lrc = sys_link((long)argv[1], (long)argv[2]);
        if (lrc < 0) {
            puts_("ln: hard link failed (rc=");
            /* small decimal print of the errno-ish value */
            long v = -lrc;
            char nb[12];
            int ni = 0;
            if (v == 0) nb[ni++] = '0';
            while (v > 0 && ni < 11) { nb[ni++] = (char)('0' + (v % 10)); v /= 10; }
            nb[ni] = 0;
            /* reverse */
            for (int a = 0, b = ni - 1; a < b; a++, b--) {
                char tc = nb[a]; nb[a] = nb[b]; nb[b] = tc;
            }
            puts_(nb);
            puts_(")\n");
        }
        return 1;
    }

    /* ---------- chmod <octal-mode> <path> ---------- */
    if (streq(argv[0], "chmod")) {
        if (argc < 3) {
            puts_("usage: chmod <mode> <path>   (mode in octal, e.g. 644)\n");
            return 1;
        }
        /* parse octal */
        u32 mode = 0;
        const char *m = argv[1];
        while (*m >= '0' && *m <= '7') { mode = mode * 8 + (u32)(*m - '0'); m++; }
        if (sys_chmod((long)argv[2], (long)mode) < 0) {
            puts_("chmod: cannot change mode\n");
        }
        return 1;
    }

    /* ---------- chown <uid>[:<gid>] <path> ---------- */
    if (streq(argv[0], "chown")) {
        if (argc < 3) {
            puts_("usage: chown <uid>[:<gid>] <path>\n");
            return 1;
        }
        u32 uid = 0, gid = 0;
        const char *s = argv[1];
        int got_uid = 0;
        while (*s >= '0' && *s <= '9') { uid = uid * 10 + (u32)(*s - '0'); s++; got_uid = 1; }
        if (*s == ':') {
            s++;
            while (*s >= '0' && *s <= '9') { gid = gid * 10 + (u32)(*s - '0'); s++; }
        }
        if (!got_uid || *s != 0) {
            puts_("usage: chown <uid>[:<gid>] <path>\n");
            return 1;
        }
        if (sys_chown((long)argv[2], (long)uid, (long)gid) < 0) {
            puts_("chown: cannot change owner\n");
        }
        return 1;
    }

    /* ---------- sed s/old/new/[g] | -n Np | Nd [file] ---------- */
    if (streq(argv[0], "sed")) {
        if (argc < 2) {
            puts_("usage: sed s/old/new/[g] [file] | sed -n Np [file] | sed Nd [file]\n");
            return 1;
        }
        int src_fd = 0;
        int opened = 0;
        int script_idx = 1;
        if (argc >= 3) {
            /* last arg may be a file (when it is not part of the script) */
            if (streq(argv[1], "-n") && argc >= 4) {
                script_idx = 2;
            }
            /* try the last argument as a file name */
            int fd = sys_open(argv[argc - 1], 1);
            if (fd >= 0) {
                src_fd = fd;
                opened = 1;
            }
        }
        static char sbuf[16384];
        long total = 0;
        long n;
        while ((n = sys_read(src_fd, sbuf + total, 4096)) > 0 &&
               total < (long)sizeof(sbuf) - 4096) {
            total += n;
        }
        if (opened) sys_close(src_fd);
        sbuf[total] = 0;

        const char *script = argv[script_idx];
        int lineno = 0;
        int i = 0;
        while (i <= total) {
            /* find one line */
            int ls = i;
            while (i < total && sbuf[i] != '\n') i++;
            int le = i;
            if (i < total) i++;      /* skip \n */
            int len = le - ls;
            if (ls == le && i > total) break;
            lineno++;
            (void)len;

            if (script[0] == '-' && script[1] == 'n') {
                script++;
            }
            if (script[0] == '-' && script[1] == 'n') {
                /* -n inside argv[1] handled above; skip */
            }
            const char *sc = argv[script_idx];
            if (sc[0] == '-' && sc[1] == 'n') sc += 2;
            if (sc[0] == 's' && sc[1] == '/') {
                /* s/old/new/[g] */
                char pat[128], rep[128];
                int pi = 0;
                const char *p = sc + 2;
                while (*p && *p != '/' && pi < 127) pat[pi++] = *p++;
                pat[pi] = 0;
                if (*p == '/') p++;
                int ri = 0;
                while (*p && *p != '/' && ri < 127) rep[ri++] = *p++;
                rep[ri] = 0;
                int global = 0;
                if (*p == '/') p++;
                if (*p == 'g') global = 1;
                /* emit line with replacements */
                int j = ls;
                while (j < le) {
                    int m = 0;
                    while (m < pi && j + m < le && sbuf[j + m] == pat[m]) m++;
                    if (pi > 0 && m == pi) {
                        sys_write(1, rep, ri);
                        if (!global) {
                            /* rest of the line verbatim */
                            sys_write(1, sbuf + j + pi, le - (j + pi));
                            break;
                        }
                        j += pi;
                    } else {
                        putc_(sbuf[j]);
                        j++;
                    }
                }
                putc_('\n');
            } else if (sc[0] == 'd') {
                int num = atoi_(sc + 1);
                if (num == lineno) {
                    /* delete: print nothing */
                } else {
                    sys_write(1, sbuf + ls, le - ls);
                    putc_('\n');
                }
            } else if (sc[0] == 'p' || (sc[0] >= '0' && sc[0] <= '9')) {
                /* [N]p - print only line N */
                int num = 0;
                int k2 = 0;
                while (sc[k2] >= '0' && sc[k2] <= '9') { num = num * 10 + (sc[k2] - '0'); k2++; }
                if (sc[k2] == 'p') {
                    if (num == lineno) {
                        sys_write(1, sbuf + ls, le - ls);
                        putc_('\n');
                    }
                }
            } else {
                puts_("sed: unknown script\n");
                return 1;
            }
            if (i > total) break;
            if (ls == le && i >= total) break;
        }
        return 1;
    }

    /* ---------- awk '{print $N | NF | NR}' [/pat/] [file] ---------- */
    if (streq(argv[0], "awk")) {
        if (argc < 2) {
            puts_("usage: awk '{print $N|NF|NR}' [/pat/] [file]\n");
            return 1;
        }
        const char *prog = argv[1];
        /* accept both quoted '{print $1}' and bare {print $1} */
        while (*prog && *prog != '{') prog++;
        if (*prog != '{') {
            puts_("awk: missing {print ...}\n");
            return 1;
        }
        prog++;
        while (*prog == ' ') prog++;
        if (strncmp_(prog, "print", 5) != 0) {
            puts_("awk: only {print ...} is supported\n");
            return 1;
        }
        prog += 5;
        while (*prog == ' ') prog++;
        char field_spec[8];
        int fs = 0;
        while (*prog && *prog != '}' && *prog != ' ' && fs < 7) {
            field_spec[fs++] = *prog++;
        }
        field_spec[fs] = 0;
        const char *pattern = 0;
        if (argc >= 3 && argv[2][0] == '/') {
            pattern = argv[2] + 1;
            int pl = strlen_(pattern);
            if (pl > 0 && pattern[pl - 1] == '/') pattern = 0;   /* bare / */
        }
        int file_arg = (pattern ? 3 : 2);
        int src_fd = 0;
        int opened = 0;
        if (argc > file_arg) {
            int fd = sys_open(argv[file_arg], 1);
            if (fd < 0) {
                puts_("awk: cannot open ");
                puts_(argv[file_arg]);
                putc_('\n');
                return 1;
            }
            src_fd = fd;
            opened = 1;
        }
        static char abuf[8192];
        static char aline[1024];
        int lpos = 0;
        int nr = 0;
        long n;
        /* helper macro-ish code: print requested field of aline */
        while ((n = sys_read(src_fd, abuf, 8192)) > 0) {
            for (int i2 = 0; i2 < (int)n; i2++) {
                char c = abuf[i2];
                if (c == '\n') {
                    aline[lpos] = 0;
                    nr++;
                    /* pattern filter */
                    int show = 1;
                    if (pattern && !strstr_(aline, pattern)) show = 0;
                    if (show) {
                        if (streq(field_spec, "NR")) {
                            putu_((u64)nr);
                            putc_('\n');
                        } else if (streq(field_spec, "NF")) {
                            int nf = 0;
                            int in_w = 0;
                            for (int q = 0; aline[q]; q++) {
                                if (aline[q] == ' ' || aline[q] == '\t') in_w = 0;
                                else if (!in_w) { in_w = 1; nf++; }
                            }
                            putu_((u64)nf);
                            putc_('\n');
                        } else if (field_spec[0] == '$') {
                            int want = atoi_(field_spec + 1);
                            int fno = 0;
                            int q = 0;
                            while (aline[q] == ' ' || aline[q] == '\t') q++;
                            while (aline[q] && fno <= want) {
                                int ws = q;
                                while (aline[q] && aline[q] != ' ' && aline[q] != '\t') q++;
                                fno++;
                                if (fno == want) {
                                    sys_write(1, aline + ws, q - ws);
                                    break;
                                }
                                while (aline[q] == ' ' || aline[q] == '\t') q++;
                            }
                            putc_('\n');
                        } else {
                            puts_(aline);
                            putc_('\n');
                        }
                    }
                    lpos = 0;
                } else {
                    if (lpos < 1023) aline[lpos++] = c;
                }
            }
        }
        if (opened) sys_close(src_fd);
        return 1;
    }

    /* ---------- ping <host> (kernel ICMP via NETCMD bridge) ---------- */
    if (streq(argv[0], "ping")) {
        if (argc < 2) {
            puts_("usage: ping <host>\n");
            return 1;
        }
        static char nout[4096];
        long rc = sys_netcmd(NETCMD_PING, (long)argv[1], (long)nout, (long)sizeof(nout));
        if (rc < 0) {
            puts_("ping: kernel bridge unavailable\n");
            return 1;
        }
        puts_(nout);
        return 1;
    }

    /* ---------- wget <host> [port] [path] ---------- */
    if (streq(argv[0], "wget")) {
        if (argc < 2) {
            puts_("usage: wget <host> [port] [path]\n");
            return 1;
        }
        static char arg[256];
        arg[0] = 0;
        int used = 0;
        for (int i = 1; i < argc && i <= 3; i++) {
            if (i > 1) arg[used++] = ' ';
            int al = strlen_(argv[i]);
            for (int q = 0; q < al && used < 250; q++) arg[used++] = argv[i][q];
        }
        arg[used] = 0;
        static char nout[4096];
        long rc = sys_netcmd(NETCMD_WGET, (long)arg, (long)nout, (long)sizeof(nout));
        if (rc < 0) {
            puts_("wget: kernel bridge unavailable\n");
            return 1;
        }
        puts_(nout);
        return 1;
    }

    /* ---------- netstat / ifconfig (kernel stats via bridge) ---------- */
    if (streq(argv[0], "netstat") || streq(argv[0], "ifconfig")) {
        int op = streq(argv[0], "netstat") ? NETCMD_NETSTAT : NETCMD_IFCONFIG;
        static char nout[4096];
        long rc = sys_netcmd(op, 0, (long)nout, (long)sizeof(nout));
        if (rc < 0) {
            puts_(argv[0]);
            puts_(": kernel bridge unavailable\n");
            return 1;
        }
        puts_(nout);
        return 1;
    }

    /* ---------- nano / vi <file> (WP-10-wp08fix1: real nano-style) ---- */
    if (streq(argv[0], "vi") || streq(argv[0], "nano")) {
        if (argc < 2) {
            puts_("usage: ");
            puts_(argv[0]);
            puts_(" <file>\n");
            return 1;
        }
        ush_nano_run(argv[1]);
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
     * run the builtin, then restore.
     *
     * BUG-0090 FIX (A16-3): this path had three fd-lifecycle bugs:
     *   1. the redirect fd was never closed after dup2 (fd leak),
     *   2. after a builtin, fd 1 was closed to "restore" the console —
     *      but fd 1 was merely OVERWRITTEN by dup2, and closing it left
     *      the slot empty forever (the next dup2 of the next command
     *      then reused slot 1 for the FILE), and stdin was NEVER
     *      restored: after `cmd < file` every later command read from
     *      the already-closed file (stdin permanently re-bound),
     *   3. the exec-child path closed fd 1 in the PARENT after wait()
     *      — a double close in disguise.
     * With the kernel-side fd refcounting (BUG-0098) the correct
     * sequence is: dup() the originals, dup2 the redirect fd over
     * 0/1, close the redirect fd (refcount now makes this safe), run,
     * then dup2 the saved originals back and close them. */
    if (redir_out || redir_in) {
        int saved_out = sys_dup(1);   /* keep the real console slot */
        int saved_in  = sys_dup(0);
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
                sys_close(fd);    /* safe now: fd 1 holds its own ref */
            }
        }
        if (redir_in) {
            int fd = sys_open(redir_in, 1);  /* RDONLY */
            if (fd >= 0) {
                sys_dup2(fd, 0);  /* stdin → file */
                sys_close(fd);
            }
        }
        /* Run built-in (writes to redirected fd 1) */
        if (builtin_cmd(argc, argv)) {
            /* Restore the saved console fds. */
            if (saved_out >= 0) { sys_dup2(saved_out, 1); sys_close(saved_out); }
            if (saved_in  >= 0) { sys_dup2(saved_in,  0); sys_close(saved_in);  }
            return 0;
        }
        /* Not a builtin: try exec in child (child inherits the redirect) */
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
        /* Parent: restore the console fds (single close per save). */
        if (saved_out >= 0) { sys_dup2(saved_out, 1); sys_close(saved_out); }
        if (saved_in  >= 0) { sys_dup2(saved_in,  0); sys_close(saved_in);  }
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

    /* Create pipes
     * P5 fix: old code had two bugs:
     * 1. Called sys_wait4(pid) AFTER each child — deadlocked when the
     *    producer's pipe buffer filled before the consumer was forked.
     * 2. Closed prev_read (pipe read end) at the START of the next
     *    iteration, BEFORE the next child was forked. This dropped
     *    reader_count to 0, causing the producer's sys_write to fail
     *    (writer_count > 0 but reader_count == 0 → write returns -1,
     *    data lost).
     *
     * Fix: fork ALL children first (so all pipe ends are connected),
     * close the parent's copies of all pipe fds AFTER all forks,
     * then wait for all children. */
    int prev_read = -1;
    long pids[8];
    int npids = 0;
    /* Track pipe fds the parent needs to close after all forks. */
    int parent_close_after[16];
    int n_parent_close = 0;
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
        /* Parent: DON'T close prev_read yet — the next child needs
         * to inherit it via fork. Close it AFTER the next fork. */
        if (prev_read >= 0 && n_parent_close < 16) {
            parent_close_after[n_parent_close++] = prev_read;
        }
        if (has_next) {
            /* Close the write end in the parent — the child has it
             * via dup2. But DON'T close the read end yet (it's prev_read
             * for the next child). */
            sys_close(pipefds[1]);
            prev_read = pipefds[0];
        } else {
            prev_read = -1;
        }
        if (pid > 0 && npids < 8) pids[npids++] = pid;
    }
    /* P5 fix: NOW close all parent's pipe read ends (after all children
     * are forked). This ensures reader_count stays > 0 while producers
     * are writing. */
    for (int i = 0; i < n_parent_close; i++) {
        sys_close(parent_close_after[i]);
    }
    /* P5 fix: wait for ALL children after all are forked. */
    for (int i = 0; i < npids; i++) {
        sys_wait4(pids[i]);
    }
}

/* ==================================================================
 * WP-10-wp08fix1: full line editing for the ush prompt
 * ==================================================================
 * Keys: Up/Down history, Left/Right/Home/End cursor, Delete,
 * Ctrl+A/E (line start/end), Ctrl+U/K (kill to start/end),
 * Ctrl+W (kill word), Ctrl+C (cancel line), Tab (completion).
 * Rendering uses only \r and re-printing (console has no ANSI). */

/* Redraw the input line with the cursor at `cur`. */
static void le_redraw(const char *line, int llen, int cur) {
    char cr = '\r';
    char sp = ' ';
    sys_write(1, &cr, 1);
    if (llen > 0) sys_write(1, line, llen);
    sys_write(1, &sp, 1);          /* clear one trailing cell */
    sys_write(1, &cr, 1);
    if (cur > 0) sys_write(1, line, cur);
}

/* Insert a char at the cursor position. Returns the new cursor. */
static int le_insert(char *line, int llen, int cur, char c, int cap) {
    if (llen + 1 >= cap) return cur;
    if (cur == llen) {
        line[llen] = c;
        putc_(c);
    } else {
        for (int i = llen; i > cur; i--) line[i] = line[i - 1];
        line[cur] = c;
        le_redraw(line, llen + 1, cur + 1);
    }
    line[llen + 1] = 0;
    return cur + 1;
}

/* Delete the char before the cursor. */
static int le_backspace(char *line, int llen, int cur) {
    if (cur <= 0) return cur;
    for (int i = cur - 1; i < llen; i++) line[i] = line[i + 1];
    if (cur == llen) {
        puts_("\b \b");
    } else {
        le_redraw(line, llen - 1, cur - 1);
    }
    return cur - 1;
}

/* Delete the char AT the cursor. */
static int le_delete(char *line, int llen, int cur) {
    if (cur >= llen) return cur;
    for (int i = cur; i < llen; i++) line[i] = line[i + 1];
    le_redraw(line, llen - 1, cur);
    return cur;
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
    /* WP-10-wp08fix1: history navigation state */
    int  hist_nav = -1;             /* -1 = typing a draft */
    char hist_draft[256];

    for (;;) {
        /* Print prompt */
        const char *ps1 = env_get("PS1");
        if (!ps1[0]) ps1 = "ush> ";
        puts_(ps1);

        /* Read a line: full editing (history/cursor/Tab/Ctrl+C) */
        int llen = 0;
        int cur = 0;
        int bg = 0;  /* background flag */
        hist_nav = -1;
        line[0] = 0;
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
            if (k == 0x03) { /* Ctrl+C: cancel the line */
                puts_("^C\n");
                llen = 0;
                cur = 0;
                line[0] = 0;
                goto next_prompt;
            }
            if (k == 0x80) { /* Up: older history entry */
                if (g_history_count > 0) {
                    if (hist_nav == -1) {
                        strncpy_(hist_draft, line, (int)sizeof(hist_draft));
                        hist_nav = g_history_count - 1;
                    } else if (hist_nav > 0) {
                        hist_nav--;
                    } else {
                        continue;   /* oldest entry */
                    }
                    strncpy_(line, g_history[hist_nav], (int)sizeof(hist_draft));
                    llen = strlen_(line);
                    cur = llen;
                    le_redraw(line, llen, cur);
                }
                continue;
            }
            if (k == 0x81) { /* Down: newer history entry (or draft) */
                if (hist_nav != -1) {
                    if (hist_nav < g_history_count - 1) {
                        hist_nav++;
                        strncpy_(line, g_history[hist_nav], (int)sizeof(hist_draft));
                    } else {
                        hist_nav = -1;
                        strncpy_(line, hist_draft, (int)sizeof(hist_draft));
                    }
                    llen = strlen_(line);
                    cur = llen;
                    le_redraw(line, llen, cur);
                }
                continue;
            }
            if (k == 0x82) { /* Left */
                if (cur > 0) { cur--; le_redraw(line, llen, cur); }
                continue;
            }
            if (k == 0x83) { /* Right */
                if (cur < llen) { cur++; le_redraw(line, llen, cur); }
                continue;
            }
            if (k == 0x84 || k == 0x01) { /* Home / Ctrl+A */
                cur = 0;
                le_redraw(line, llen, cur);
                continue;
            }
            if (k == 0x85 || k == 0x05) { /* End / Ctrl+E */
                cur = llen;
                le_redraw(line, llen, cur);
                continue;
            }
            if (k == 0x89) { /* Delete: remove char at cursor */
                cur = le_delete(line, llen, cur);
                llen = strlen_(line);
                continue;
            }
            if (k == 0x15) { /* Ctrl+U: kill to line start */
                if (cur > 0) {
                    int rest = llen - cur;
                    for (int i = 0; i < rest; i++) line[i] = line[cur + i];
                    llen = rest;
                    line[llen] = 0;
                    cur = 0;
                    le_redraw(line, llen, cur);
                }
                continue;
            }
            if (k == 0x0B) { /* Ctrl+K: kill to line end */
                if (cur < llen) {
                    llen = cur;
                    line[llen] = 0;
                    le_redraw(line, llen, cur);
                }
                continue;
            }
            if (k == 0x17) { /* Ctrl+W: kill the previous word */
                if (cur > 0) {
                    int p = cur;
                    while (p > 0 && line[p - 1] == ' ') p--;
                    while (p > 0 && line[p - 1] != ' ') p--;
                    int removed = cur - p;
                    int rest = llen - cur;
                    for (int i = 0; i < rest; i++) line[p + i] = line[cur + i];
                    llen -= removed;
                    line[llen] = 0;
                    cur = p;
                    le_redraw(line, llen, cur);
                }
                continue;
            }
            if (k == 0x08 || k == 0x7F) { /* Backspace */
                cur = le_backspace(line, llen, cur);
                llen = strlen_(line);
                continue;
            }
            if (k == '\t') { /* Tab completion (cursor at end of line) */
                if (cur != llen) continue;   /* simple: complete at EOL only */
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
                    "touch","rm","rmdir","cp","mv","sort","uniq","ps","kill",
                    "date","uname","free","df","du","vi","nano","pwd",
                    "cd","exit","export","alias","unalias","history",
                    "jobs","fg","bg",
                    /* WP-10-wp08fix1 */
                    "ln","chmod","chown","sed","awk","ping","wget",
                    "netstat","ifconfig","top",0
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
                        /* WP-10-wp08fix1 FIX: the kernel copies
                         * sizeof(fs_vfs_dirent_t) bytes (char name[64]; int
                         * type; u64 inode) - the old 32-byte-name local
                         * struct here was smaller than that and the kernel
                         * copy overflowed this stack frame. Use the same
                         * layout as ush_dirent (64-byte name). */
                        struct ush_dirent e;
                        memset_(&e, 0, (int)sizeof(e));
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
                /* WP-10-wp08fix1: redraw the whole line after completion
                 * (same rendering rule as the kernel oc> line editor) so
                 * the completed word is always visible in place. */
                cur = llen;
                le_redraw(line, llen, cur);
                continue;
            }
            if (k == '&' && llen == 0) { /* Background */
                /* Actually, & at end of line means background */
            }
            if (k >= 0x20 && k < 0x7F && llen < 510) {
                cur = le_insert(line, llen, cur, (char)k, 511);
                llen = strlen_(line);
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
