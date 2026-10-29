/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP04 - shared L1 application library.
 *
 * A header-only library (every function static inline): each L1 app
 * folder compiles its own copy; there is no shared runtime to link.
 * Provides the syscall wrappers, the verbatim L1SVC numbering block,
 * tiny string/IO helpers, and the argv fetch (the kernel execve entry
 * leaves argc at [rsp]).
 *
 * Include from any app as:  #include "_shared/l1_lib.h"
 * (the app build adds -I src/L1 to the compiler invocation). */
#ifndef OC_L1_LIB_H
#define OC_L1_LIB_H

/* ---- syscall numbers (verbatim contract with services/sysapi) ----- */
#define SYS_WRITE     1
#define SYS_OPEN      3
#define SYS_CLOSE     4
#define SYS_STAT      5
#define SYS_READDIR   6
#define SYS_MKDIR     7
#define SYS_RMDIR     8
#define SYS_UNLINK    9
#define SYS_FORK     10
#define SYS_EXECVE   11
#define SYS_WAIT4    12
#define SYS_KILL     13
#define SYS_GETPID   14
#define SYS_EXIT2    16
#define SYS_PIPE     20
#define SYS_READ     70
#define SYS_WRITE2   72
#define SYS_READLINE 73
#define SYS_GETCH    74
#define SYS_UPTIME   75
#define SYS_MEMINFO  76
#define SYS_DUP      50
#define SYS_DUP2     51
#define SYS_CHDIR    60
#define SYS_GETCWD   61
#define SYS_SYMLINK  96
#define SYS_READLINK 97
#define SYS_LINK     98
#define SYS_CHMOD    99
#define SYS_CHOWN   100
#define SYS_NETCMD  101
#define SYS_PS      102
#define SYS_L1SVC   110

/* ---- raw syscall helpers ------------------------------------------ */
static inline long syscall0(long n) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(n) : "memory");
    return r;
}
static inline long syscall1(long n, long a) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(n), "D"(a) : "memory");
    return r;
}
static inline long syscall2(long n, long a, long b) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(n), "D"(a), "S"(b) : "memory");
    return r;
}
static inline long syscall3(long n, long a, long b, long c) {
    long r;
    __asm__ volatile("int $0x80"
                     : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c) : "memory");
    return r;
}
static inline long syscall4(long n, long a, long b, long c, long e) {
    long r;
    register long r10 __asm__("r10") = e;
    __asm__ volatile("int $0x80"
                     : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10)
                     : "memory");
    return r;
}
/* SYS_L1SVC takes five words (cat, op, arg, out, cap). */
static inline long syscall5(long n, long a, long b, long c, long e, long f) {
    long r;
    register long r10 __asm__("r10") = e;
    register long r8  __asm__("r8")  = f;
    __asm__ volatile("int $0x80"
                     : "=a"(r)
                     : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8)
                     : "memory");
    return r;
}

static inline long sys_write(long fd, const void *buf, long len) {
    return syscall3(SYS_WRITE, fd, (long)buf, len);
}
static inline long sys_read(long fd, void *buf, long len) {
    return syscall3(SYS_READ, fd, (long)buf, len);
}
static inline long sys_open(const char *path, long flags) {
    return syscall2(SYS_OPEN, (long)path, flags);
}
static inline long sys_close(long fd) { return syscall1(SYS_CLOSE, fd); }
static inline long sys_exit2(long code) { return syscall1(SYS_EXIT2, code); }
static inline long sys_getpid(void) { return syscall0(SYS_GETPID); }
static inline long sys_l1svc(long cat, long op, long arg,
                             long out, long cap) {
    return syscall5(SYS_L1SVC, cat, op, arg, out, cap);
}

/* ---- additional syscall wrappers (3.0.0-WP04 G5) -------------------
 * The file-tool apps moved out of the ush shell need the classic fs/
 * proc/mem/net syscalls; same numbers, same register contracts as the
 * ush macros had - only the packaging changed. */
static inline long sys_getcwd(void *buf, long size) {
    return syscall2(SYS_GETCWD, (long)buf, size);
}
static inline long sys_getch(void) { return syscall0(SYS_GETCH); }
static inline long sys_uptime(void) { return syscall0(SYS_UPTIME); }
static inline long sys_meminfo(void *buf) {
    return syscall1(SYS_MEMINFO, (long)buf);
}
/* SYS_KILL is (pid, sig, ...) - WP04 G5 live-catch: a 1-arg wrapper
 * leaked stale register state into the kernel's `sig` (garbage failed
 * the sig >= NSIG gate and kill always returned -1).  The shell `kill`
 * command means SIGKILL (9), spelled explicitly. */
static inline long sys_kill(long pid) { return syscall2(SYS_KILL, pid, 9); }
static inline long sys_ps(void *buf, long cap) {
    return syscall2(SYS_PS, (long)buf, cap);
}
static inline long sys_readdir(const char *path, long idx, void *buf) {
    return syscall3(SYS_READDIR, (long)path, idx, (long)buf);
}
static inline long sys_mkdir(const char *path) {
    return syscall1(SYS_MKDIR, (long)path);
}
static inline long sys_rmdir(const char *path) {
    return syscall1(SYS_RMDIR, (long)path);
}
static inline long sys_unlink(const char *path) {
    return syscall1(SYS_UNLINK, (long)path);
}
static inline long sys_stat(const char *path, void *st) {
    return syscall2(SYS_STAT, (long)path, (long)st);
}
static inline long sys_symlink(const char *target, const char *linkpath) {
    return syscall2(SYS_SYMLINK, (long)target, (long)linkpath);
}
static inline long sys_readlink(const char *path, char *buf, long cap) {
    return syscall3(SYS_READLINK, (long)path, (long)buf, cap);
}
static inline long sys_link(const char *oldpath, const char *newpath) {
    return syscall2(SYS_LINK, (long)oldpath, (long)newpath);
}
static inline long sys_chmod(const char *path, long mode) {
    return syscall2(SYS_CHMOD, (long)path, mode);
}
static inline long sys_chown(const char *path, long uid, long gid) {
    return syscall3(SYS_CHOWN, (long)path, uid, gid);
}
static inline long sys_netcmd(long op, const void *arg, void *out, long cap) {
    return syscall4(SYS_NETCMD, op, (long)arg, (long)out, cap);
}

/* ---- argv ------------------------------------------------------------
 * The kernel execve entry leaves argc at [rsp], argv[] right above it.
 * A compiled C _start() cannot read that layout itself (the compiler
 * may insert a stack prologue before any asm - the WP04 G5 live-catch),
 * so every folder app links the _shared/crt0.asm trampoline, which
 * hands argc/argv to app_main(int argc, char **argv). */

/* ---- tiny string / IO helpers -------------------------------------- */
static inline long strlen_(const char *s) {
    long n = 0;
    while (s && s[n]) n++;
    return n;
}
static inline int streq_(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}
static inline int starts_with_(const char *s, const char *p) {
    while (*p && *s == *p) { s++; p++; }
    return *p == 0;
}
static inline char *strcpy_(char *d, const char *s) {
    char *o = d;
    while ((*d++ = *s++)) ;
    return o;
}
static inline char *strcat_(char *d, const char *s) {
    char *o = d;
    while (*d) d++;
    while ((*d++ = *s++)) ;
    return o;
}
static inline int strcmp_(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}
static inline long atoi_(const char *s) {
    long v = 0, neg = 0;
    if (!s) return 0;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    return neg ? -v : v;
}
static inline void puts_(const char *s) {
    if (s) sys_write(1, s, strlen_(s));
}
static inline void putc_(char c) { sys_write(1, &c, 1); }
static inline void putu_(unsigned long v) {
    char buf[24];
    int i = 23;
    buf[i--] = 0;
    if (!v) buf[i--] = '0';
    while (v) { buf[i--] = '0' + (v % 10); v /= 10; }
    puts_(&buf[i + 1]);
}
static inline void putx_(unsigned long v) {
    char buf[20];
    const char *hex = "0123456789abcdef";
    int i = 19;
    buf[i--] = 0;
    if (!v) buf[i--] = '0';
    while (v) { buf[i--] = hex[v & 0xF]; v >>= 4; }
    puts_(&buf[i + 1]);
}

/* ---- the standard L1SVC wrapper app body ---------------------------
 * v2.2.0 parity: the ush SVC_CMDS runner joined argv[1..] with single
 * spaces into one 256-byte argument line, called
 *     sys_l1svc(cat, op, arg, out, 4096)
 * and printed `out` (plus a trailing newline when the text did not end
 * with one).  l1svc_main() is exactly that body, verbatim semantics;
 * each wrapper app is:
 *     #include "_shared/l1_lib.h"
 *     #include "_shared/l1svc_ops.h"
 *     int app_main(int argc, char **argv)
 *     { return l1svc_main(argc, argv, CAT, OP); }
 * Errors print "<name>: service call failed (rc=-1)" like ush did.
 * Returns the app exit code. */
static inline int l1svc_main(int argc, char **argv, long cat, long op) {
    char arg[256];
    int len = 0;
    arg[0] = 0;
    for (int j = 1; j < argc && len < (int)sizeof(arg) - 1; j++) {
        if (j > 1 && len < (int)sizeof(arg) - 2) arg[len++] = ' ';
        for (const char *p = argv[j]; *p && len < (int)sizeof(arg) - 1; p++)
            arg[len++] = *p;
        arg[len] = 0;
    }
    char out[4096];
    long rc = sys_l1svc(cat, op, arg[0] ? (long)arg : 0,
                        (long)out, (long)sizeof(out));
    if (rc != 0) {
        puts_(argv[0]);
        puts_(": service call failed (rc=-1)\n");
        return 1;
    }
    puts_(out);
    if (out[0] && out[strlen_(out) - 1] != '\n') putc_('\n');
    return 0;
}

/* ---- L1SVC categories/ops -------------------------------------------
 * Verbatim copy of src/L0/services/sysapi/l1svc_defs.h (ring-3 apps
 * build standalone; renumber upstream only by appending, then mirror
 * here).  WP-04 apps include the whole block once and pick constants. */
#define L1SVC_CAT_TIME      1
#define L1SVC_CAT_BLOCK     2
#define L1SVC_CAT_NIC       3
#define L1SVC_CAT_SND       4
#define L1SVC_CAT_POWER     5
#define L1SVC_CAT_OTA       6
#define L1SVC_CAT_FS        7
#define L1SVC_CAT_NET       8
#define L1SVC_CAT_HEALTH    9
#define L1SVC_CAT_KLOG     10
#define L1SVC_CAT_WATCHDOG 11
#define L1SVC_CAT_CRYPTO   12
#define L1SVC_CAT_L1       13
#define L1SVC_CAT_DISPLAY  14
#define L1SVC_CAT_INPUT    15
#define L1SVC_CAT_CAMERA   16
#define L1SVC_CAT_USB      17
#define L1SVC_CAT_OCIRE    18
#define L1SVC_CAT_DIAG     19

/* ---- fixed-width aliases --------------------------------------------
 * The same aliases the ush shell defines for its builtin tools; the
 * kernel on-wire structures below and the G5 file-tool apps use them. */
typedef unsigned long u64;
typedef long i64;
typedef int i32;
typedef unsigned int u32;
typedef char i8;
typedef unsigned char u8;

/* ---- kernel process table entry -------------------------------------
 * MUST match the layout the kernel writes in sys_ps (tid, state,
 * priority, name[32], cpu ticks).  Moved verbatim from ush - the
 * `ush_` prefix is a historical name, the memory layout is the
 * contract; used by the ps/top apps. */
struct ush_ps_entry {
    i32 tid;
    i32 state;
    i32 priority;
    char name[32];
    u64 cpu_time_ticks;
};

/* ---- SYS_NETCMD bridge op codes (kernel net_core.c contract) --------
 * Verbatim copy of the ush block (the shell keeps its own duplicate of
 * these four numbers so it stays self-contained; the duplication is
 * declared in the WP04-G5 notes).  The ping/wget/netstat/ifconfig apps
 * pick these for sys_netcmd(). */
#define NETCMD_IFCONFIG  1
#define NETCMD_PING      2
#define NETCMD_NETSTAT   3
#define NETCMD_WGET      4

#endif /* OC_L1_LIB_H */
