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
 * alias, unalias, history).
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
#define SYS_L1SVC   110

/* 3.0.0-WP04: the SVC dispatch table is GONE from ush.  Every former
 * table row is now its own ring-3 app (src/L1/<name>/main.c, a thin
 * l1svc_main() wrapper over _shared/l1_lib.h); ush reaches them like
 * any other program through fork + execve + the generated OC_USERPROGS
 * registry.  G5: the file tools, the net bridge tools and the nano/vi
 * editor moved out the same way (see the `3.0.0-WP04: ... moved to
 * src/L1/<name>/` markers below); the shell keeps only its own
 * process-state builtins (cd/exit/help/export/alias/unalias/history/
 * set/env/jobs/fg/bg) plus the line editor. */

/* SYS_NETCMD bridge op codes (kernel net_core.c contract) */
#define NETCMD_IFCONFIG  1
#define NETCMD_PING      2
#define NETCMD_NETSTAT   3
#define NETCMD_WGET      4

/* 3.0.0-WP04 G5: USH_TYPE_* moved to _shared/fs_lib.h (only the moved
 * file-tool apps use them now). */

typedef unsigned long u64;
/* VFS on-wire structures - must match kernel layout (vfs.h, VFS_NAME_LEN=64).
 * The kernel's sys_readdir/sys_stat memcpy sizeof(vfs_dirent_t / vfs_stat_t)
 * bytes into our buffer, so our struct size must equal the kernel's.
 * WP-10-wp08fix1: fs_vfs_stat_t grew mode/uid/gid/nlink - mirrored here.
 * 3.0.0-WP04 G5: the shared copy of these structs now lives in
 * _shared/fs_lib.h; ush_stat and ush_ps_entry are GONE from here (all
 * their users became apps) - ush keeps ush_dirent for Tab file-name
 * completion. */
struct ush_dirent {
    char name[64];
    int  type;
    u64  inode;
};
/* 3.0.0-WP04 G5: ush_ps_entry moved to _shared/l1_lib.h (ps/top apps). */

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
#define sys_readline(buf, max)   syscall2(SYS_READLINE, (long)(buf), (max))
#define sys_getch()           syscall0(SYS_GETCH)
#define sys_fork()               syscall0(SYS_FORK)
#define sys_execve(path, argv, envp) syscall3(SYS_EXECVE, (long)(path), (long)(argv), (long)(envp))
/* SYS_WAIT4 is 4-arg; spell the status_ptr (0 = not collected) so the
 * call site can never leak stale register state into the kernel. */
#define sys_wait4(pid)           syscall2(SYS_WAIT4, (pid), 0)
#define sys_exit2(code)          syscall1(SYS_EXIT2, (code))
#define sys_pipe(fds)            syscall1(SYS_PIPE, (long)(fds))
#define sys_dup(old)             syscall2(SYS_DUP, (old), 0)
#define sys_dup2(old, new)       syscall2(SYS_DUP2, (old), (new))
#define sys_chdir(path)          syscall1(SYS_CHDIR, (long)(path))
#define sys_open(path, flags)    syscall2(SYS_OPEN, (long)(path), (flags))
#define sys_close(fd)            syscall1(SYS_CLOSE, (fd))
#define sys_readdir(path, idx, buf) syscall3(SYS_READDIR, (long)(path), (long)(idx), (long)(buf))
#define sys_getpid()                syscall0(SYS_GETPID)
#define sys_l1svc(cat, op, arg, out, cap) syscall5(SYS_L1SVC, (long)(cat), (long)(op), (long)(arg), (long)(out), (long)(cap))

/* 3.0.0-WP04 G5: the fs/proc/mem/net macro wrappers below moved to
 * _shared/l1_lib.h as static inline functions (sys_read/sys_stat/
 * sys_mkdir/sys_rmdir/sys_unlink/sys_kill/sys_getcwd/sys_uptime/
 * sys_meminfo/sys_symlink/sys_readlink/sys_link/sys_chmod/sys_chown/
 * sys_netcmd/sys_ps) - only the migrated tool apps use them now. */

/* 5-argument syscall (v2.0.0: SYS_L1SVC needs it). */
static inline long syscall5(long n, long a, long b, long c, long d, long e) {
    long r;
    register long r10 __asm__("r10") = d;
    register long r8  __asm__("r8")  = e;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8) : "rcx", "r11", "memory");
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

/* 3.0.0-WP04 G5: strstr_/strcmp_ moved to _shared (fs_lib.h carries
 * strstr_; l1_lib.h carries strcmp_) - grep/awk/sort are apps now. */

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

/* 3.0.0-WP04 G5: the nano/vi editor moved to _shared/editor_core.h;
 * the apps src/L1/nano/, src/L1/vi/ and src/L1/edit/ (the v2.2.0 L0
 * `edit` command) are thin shells over it (reached via execve). */

/* Built-in commands. Return 1 if handled, 0 if not. */
/* ------------------------------------------------------------------ */
/* v2.0.0 L1 service-call commands (SYS_L1SVC)                        */
/* Each entry maps a shell word to one kernel service operation; the  */
/* service renders the text, ush only forwards argv and prints.       */
/* ------------------------------------------------------------------ */
static int builtin_cmd(int argc, char *argv[]) {
    if (argc == 0) return 1;
    if (streq(argv[0], "cd")) {
        if (argc > 1) sys_chdir(argv[1]);
        else sys_chdir("/");
        return 1;
    }
    if (streq(argv[0], "exit")) {
        /* A16-15 FIX: both arms of the ternary were 0, so the documented
         * `exit [code]` argument was silently dropped (status always 0).
         * Parse the argument (atoi_: non-numeric -> 0) and pass it on so
         * a parent's wait4 can observe the real status; no arg stays 0. */
        sys_exit2(argc > 1 ? atoi_(argv[1]) : 0);
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
        puts_("\n  v2.0.0 service commands (SYS_L1SVC):\n");
        puts_("  rtc [uie|pie|alarm N]  CMOS RTC status + interrupt tests\n");
        puts_("  hpet [counter|oneshot|periodic|test]  HPET status + tests\n");
        puts_("  pmtimer [read|cal|wrap|test]  ACPI PM timer + calibration\n");
        puts_("  tsc [cal|delay N|test]  TSC reads + calibration + delay\n");
        puts_("  mount [type dev path]  List mounts or mount a filesystem\n");
        puts_("  mounts                 List VFS mount table\n");
        puts_("  umount <path>          Unmount a filesystem\n");
        puts_("  fatmount [dev path]    Mount FAT32 (default ata0 /mnt)\n");
        puts_("  fatstat                Show FAT32 stats\n");
        puts_("  tree [path]            Directory tree view\n");
        puts_("  dskstat / lsblk        Block devices + cache stats\n");
        puts_("  ahci / nvme / ata      Controller status\n");
        puts_("  parted [dev]           Partition table\n");
        puts_("  mkfs.fat32|exfat|ext4 <dev>  Format (confirm)\n");
        puts_("  fsck / sync            Filesystem check / flush cache\n");
        puts_("  abdisk / install / grub-install <dev>  Disk setup\n");
        puts_("  abcfg                  A/B grub.cfg preview\n");
        puts_("  *_test                 Block driver test suite\n");
        puts_("  <nicname> [/_test]     NIC status + tests (e1000e igb ixgbe\n");
        puts_("                         rtl8139/8168/8125/810x bcm57xx, other)\n");
        puts_("  wifi / bt              Wireless + bluetooth families\n");
        puts_("  sound / play / volume  Audio cards, playback, mixer\n");
        puts_("  hda/ac97/sb16/es1370/virtiosnd/usbaudio + _test variants\n");
        puts_("  update / checkupdate / rollback   A/B update family\n");
        puts_("  config list|get|set    System configuration store\n");
        puts_("  update_*_test          OTA test suite\n");
        puts_("  halt/shutdown/reboot   Power controls (same bodies as oc>)\n");
        puts_("  sleep <sec>            Delay N seconds\n");
        puts_("  suspend_test           S3 capability contract\n");
        puts_("  stats / uname / clear  Console + kernel counters\n");
        puts_("  lang [en|zh]           Output language\n");
        puts_("  chinese / font / encoding / ime [+ -test variants]\n");
        puts_("  touchpad / touchtest   Input subsystem\n");
        puts_("  uvc / uvctest          Camera (UVC devices + self test)\n");
        puts_("  scsi / scsitest        SCSI controllers + self test\n");
        puts_("  gameport / parallel / ir [+ *test]  Legacy port probes\n");
        puts_("  usb / usbdev [slot]    USB hosts, devices, descriptors\n");
        puts_("  usb_*_test             USB class driver test suite\n");
        puts_("  arp / route / ip       Neighbor, routing and address tables\n");
        puts_("  dhcp / dns / dnstest   Resolver + lease diagnostics\n");
        puts_("  firewall / nf_test     Ruleset + netfilter tests\n");
        puts_("  tcpstats / tcptest / tcpcc_test  TCP diagnostics\n");
        puts_("  lspci                  PCI device scan\n");
        puts_("  cryptotest / dhtest    Crypto self-tests (NIST + DH)\n");
        puts_("\n  Editing: Up/Down history, Left/Right/Home/End cursor, Del,\n");
        puts_("  Ctrl+A/E/U/K/W, Ctrl+C cancel, Tab completion\n");
        puts_("\n  Redirection:  > file   >> file   < file   | cmd\n");
        puts_("\n");
        return 1;
    }
    /* 3.0.0-WP04 G5: write moved to src/L1/write/ (app via execve). */
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
    /* 3.0.0-WP04 G5: echo moved to src/L1/echo/ (app via execve). */
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
    /* 3.0.0-WP04 G5: pwd moved to src/L1/pwd/ (app via execve; a forked
     * app inherits the shell cwd, so the output is unchanged). */
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

    /* ============= File tools: all moved out (3.0.0-WP04 G5) ============= */

    /* 3.0.0-WP04 G5: ls moved to src/L1/ls/ (app via execve). */

    /* 3.0.0-WP04 G5: cat moved to src/L1/cat/ (app via execve). */

    /* 3.0.0-WP04 G5: wc moved to src/L1/wc/ (app via execve). */

    /* 3.0.0-WP04 G5: grep moved to src/L1/grep/ (app via execve). */

    /* 3.0.0-WP04 G5: head moved to src/L1/head/ (app via execve). */

    /* 3.0.0-WP04 G5: tail moved to src/L1/tail/ (app via execve). */

    /* 3.0.0-WP04 G5: mkdir moved to src/L1/mkdir/ (app via execve). */

    /* 3.0.0-WP04 G5: rmdir moved to src/L1/rmdir/ (app via execve). */

    /* 3.0.0-WP04 G5: touch moved to src/L1/touch/ (app via execve). */

    /* 3.0.0-WP04 G5: rm moved to src/L1/rm/ (app via execve). */

    /* 3.0.0-WP04 G5: cp moved to src/L1/cp/ (app via execve). */

    /* 3.0.0-WP04 G5: mv moved to src/L1/mv/ (app via execve). */

    /* 3.0.0-WP04 G5: sort moved to src/L1/sort/ (app via execve). */

    /* 3.0.0-WP04 G5: uniq moved to src/L1/uniq/ (app via execve). */

    /* 3.0.0-WP04 G5: ps moved to src/L1/ps/ (app via execve). */
    /* 3.0.0-WP04 G5: top moved to src/L1/top/ (app via execve). */

    /* 3.0.0-WP04 G5: kill moved to src/L1/kill/ (app via execve). */

    /* 3.0.0-WP04 G5: date moved to src/L1/date/ (app via execve). */

    /* 3.0.0-WP04: builtin uname removed - the v2.2.0 shell always
     * dispatched it through the SVC table (this branch was dead);
     * the uname app (src/L1/uname/) now serves it via execve. */
    /* 3.0.0-WP04 G5: free moved to src/L1/free/ (app via execve). */

    /* 3.0.0-WP04 G5: df moved to src/L1/df/ (app via execve). */

    /* 3.0.0-WP04 G5: stat moved to src/L1/stat/ (app via execve). */

    /* 3.0.0-WP04 G5: du moved to src/L1/du/ (app via execve). */

    /* ================= link/perm tools: moved out (3.0.0-WP04 G5) ================ */

    /* 3.0.0-WP04 G5: ln moved to src/L1/ln/ (app via execve). */

    /* 3.0.0-WP04 G5: chmod moved to src/L1/chmod/ (app via execve). */

    /* 3.0.0-WP04 G5: chown moved to src/L1/chown/ (app via execve). */

    /* 3.0.0-WP04 G5: sed moved to src/L1/sed/ (app via execve). */

    /* 3.0.0-WP04 G5: awk moved to src/L1/awk/ (app via execve). */

    /* 3.0.0-WP04 G5: ping moved to src/L1/ping/ (app via execve). */

    /* 3.0.0-WP04 G5: wget moved to src/L1/wget/ (app via execve). */

    /* 3.0.0-WP04 G5: netstat/ifconfig moved to src/L1/{netstat,ifconfig}/ (apps via execve). */

    /* 3.0.0-WP04 G5: nano/vi moved to src/L1/{nano,vi,edit}/ over
     * _shared/editor_core.h (apps via execve). */

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
        /* A16-14 FIX: the alias check lived only on the no-redirect
         * path, so `alias ll='ls -l'` + `ll > f` exec'd the literal
         * "ll" (and failed silently). Expand the command word here too,
         * BEFORE the redirect fds are opened — an alias that expands to
         * nothing then no longer creates an empty redirect file. */
        const char *alr = alias_get(argv[0]);
        if (alr) {
            char newcmd[512];
            strcpy_(newcmd, alr);
            for (int i = 1; i < argc; i++) { strcat_(newcmd, " "); strcat_(newcmd, argv[i]); }
            argc = parse_args(newcmd, argv, 32);
            if (argc == 0) return 0;
        }
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
    /* Parent: wait for child.  3.0.0-WP04 G5 live-catch: SYS_WAIT4 is a
     * 4-argument syscall (pid, status_ptr, options, ...); the old
     * syscall1 wrapper left RSI holding whatever the preceding code
     * left there, and the kernel's status_ptr validation rejected the
     * garbage pointer -> wait4 returned -1 immediately and the shell
     * printed the prompt before the child ever ran (observed: `date`
     * output landing after the prompt, `echo` intermittently lost).
     * v2.x got away with it only because its code layout happened to
     * leave RSI = 0 at this call site.  Pass status_ptr = 0
     * explicitly ("do not collect an exit status") - the wrapper
     * macro below now spells the second argument. */
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
                    "netstat","ifconfig","top",
                    /* v2.0.0: SYS_L1SVC service commands */
                    "rtc","hpet","pmtimer","tsc",
                    "mount","mounts","umount","fatmount","fatstat",
                    "tree",
                    "dskstat","lsblk","ahci","nvme","ata","parted",
                    "mkfs.fat32","mkfs.exfat","mkfs.ext4","fsck","sync",
                    "abdisk","install","grub-install","abcfg",
                    "ahci_test","nvme_test","ata_dma_test",
                    "virtio_blk_test","disk_rw_test","partition_test",
                    "fs_mount_test","real_hw_test",
                    "e1000e","igb","ixgbe","rtl8139","rtl8168","rtl8125",
                    "rtl810x","bcm57xx","other_nic_test","nic_rw_test",
                    "wifi","bt",
                    "e1000e_test","igb_test","ixgbe_test","rtl8139_test",
                    "rtl8168_test","rtl8125_test","rtl810x_test",
                    "bcm57xx_test",
                    "sound","hda","ac97","sb16","es1370","virtiosnd",
                    "usbaudio","play","volume",
                    "hda_test","ac97_test","sb16_test","es1370_test",
                    "virtio_snd_test","usb_audio_test","audio_rw_test",
                    "sample_rate_test",
                    "checkupdate","config","config_test","checkupdate_test",
                    "update","rollback","update_pkg_test","ab_partition_test",
                    "update_check_test","update_download_test",
                    "update_verify_test","update_install_test",
                    "update_rollback_test","update_local_test",
                    "update_status_test","real_update_test",
                    "shutdown","poweroff","reboot","halt","sleep",
                    "poweroff_test","suspend_test","halt_test",
                    "reboot_test",
                    "stats","uname","clear","lang",
                    "chinese","chinesetest","font","font-test",
                    "encoding","encoding-test",
                    "touchpad","touchtest",
                    "uvc","uvctest","scsi","scsitest",
                    "gameport","gameporttest","parallel","paralleltest",
                    "ir","irtest",
                    "usb","usbdev","usb_core_test","usb_kbd_test",
                    "usb_mouse_test","usb_storage_test","usb_serial_test",
                    "usb_hotplug_test","usb_hub_test",
                    "arp","dhcp","dns","dnstest","firewall","ip",
                    "lspci","nf_test","route","tcpcc_test","tcpstats",
                    "tcptest","cryptotest","dhtest",0
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
            /* WP-10-tui-qa: the old empty '&' branch here was dead code
             * (the real background handling is the line[llen-1]=='&'
             * check after Enter, below); removed. */
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
