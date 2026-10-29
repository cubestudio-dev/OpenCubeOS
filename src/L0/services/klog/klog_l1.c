/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP04 - services/klog ring-3 surface.
 *
 * The v2.2.0 L0 commands dmesg / logcat / logctl (recovery/
 * shell_cmds_log.c) migrate to L1: category 10 ("klog", reserved since
 * v2.0.0) gets its first ops.  The one-shot replay core (lc_* helpers,
 * option parsing, the line-matching rules) is the v2.2.0 body carried
 * over verbatim; only the sink changed - lines append into the
 * caller's buffer (capacity-clamped, honest truncation note below).
 * Interactive follow (logcat -f) keeps the v2.2.0 console-side loop
 * inside the op (multi-screen live output has no one-shot buffer
 * semantics). */
#include "core_registry.h"
#include "l1svc_defs.h"
#include "screen_console.h"
#include "screen_console_in.h"
#include "lib_string.h"
#include "lib_klog.h"
#include "klog_file.h"
#include "fs_vfs.h"
#include "core_timer.h"

#include <stdint.h>

static char *s_out;
static int   s_left;

static void put(const char *s) {
    while (*s && s_left > 1) { *s_out++ = *s++; s_left--; }
}
static void put_u64(u64 v) {
    char b[24];
    u64_to_str(v, b);
    put(b);
}

/* ---- tiny helpers (v2.2.0 lc_* carried over) ------------------------ */
static char *lc_strstr(const char *hay, const char *needle) {
    if (!needle || !*needle) return (char*)hay;
    for (const char *h = hay; *h; h++) {
        const char *a = h, *b = needle;
        while (*a && *b && *a == *b) { a++; b++; }
        if (!*b) return (char*)h;
    }
    return 0;
}

static int lc_num(const char *s, long *out) {
    if (!s || !*s) return 0;
    long v = 0; int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    if (!*s) return 0;
    for (; *s; s++) {
        if (*s < '0' || *s > '9') return 0;
        v = v * 10 + (*s - '0');
        if (v > 100000000) return 0;
    }
    *out = neg ? -v : v;
    return 1;
}

static int lc_level_of(const char *s) {
    long n;
    if (lc_num(s, &n) && n >= 0 && n <= 4) return (int)n;
    if (strcasecmp(s, "debug") == 0) return KLOG_DEBUG;
    if (strcasecmp(s, "info")  == 0) return KLOG_INFO;
    if (strcasecmp(s, "warn")  == 0) return KLOG_WARN;
    if (strcasecmp(s, "error") == 0) return KLOG_ERROR;
    if (strcasecmp(s, "fatal") == 0) return KLOG_FATAL;
    return -1;
}

/* parse "[  sec.frac]" from a formatted line -> milliseconds */
static u64 lc_line_ms(const char *line) {
    const char *p = line;
    if (*p != '[') return 0;
    p++;
    u64 whole = 0;
    for (; *p && *p != '.' && *p != ']'; p++)
        if (*p >= '0' && *p <= '9') whole = whole * 10 + (u64)(*p - '0');
    u64 frac = 0;
    if (*p == '.') {
        p++;
        int digits = 0;
        for (; *p && *p != ']'; p++) {
            if (*p >= '0' && *p <= '9' && digits < 3) {
                frac = frac * 10 + (u64)(*p - '0');
                digits++;
            }
        }
        while (digits < 3) { frac *= 10; digits++; }
    }
    return whole * 1000 + frac;
}

/* the level tag sits right after "] " - parse it back to 0..4 (-1 bad) */
static int lc_line_level(const char *line) {
    const char *p = line;
    while (*p && *p != ']') p++;
    if (!*p) return -1;
    p += 2;
    const char *l = p;
    while (*l && *l != ' ') l++;
    usize tlen = (usize)(l - p);
    if (tlen > 5) tlen = 5;
    char tname[6];
    for (usize q = 0; q < tlen; q++) tname[q] = p[q];
    tname[tlen] = 0;
    for (int lv = 0; lv <= 4; lv++)
        if (strncmp(tname, klog_level_name(lv), 5) == 0) return lv;
    return -1;
}

/* the module token: between the level tag and the ':' */
static int lc_line_module_is(const char *line, const char *mod) {
    const char *p = line;
    while (*p && *p != ']') p++;
    if (!*p) return 0;
    p += 2;
    while (*p == ' ') p++;                 /* level tag */
    while (*p && *p != ' ') p++;           /* end of tag */
    while (*p == ' ') p++;                 /* module start */
    const char *m = p;
    const char *colon = m;
    while (*colon && *colon != ':') colon++;
    usize mlen = (usize)(colon - m);
    return (strncmp(m, mod, mlen) == 0 && strlen(mod) == mlen) ? 1 : 0;
}

typedef struct {
    int  lvl;          /* -1 = any                       */
    const char *mod;   /* 0  = any                       */
    const char *key;   /* 0  = any                       */
    long sec;          /* -1 = any time                  */
    long count;        /* how many entries to show       */
    int  follow;
} lc_opts_t;

static int lc_one_opt(const char *a, const char *b, lc_opts_t *o) {
    if (strcmp(a, "-l") == 0) {
        if (!b) { put("logcat: -l needs a level\n"); return -1; }
        int l = lc_level_of(b);
        if (l < 0) { put("logcat: bad level (debug/info/warn/error/fatal or 0-4)\n"); return -1; }
        o->lvl = l; return 2;
    }
    if (strcmp(a, "-m") == 0) {
        if (!b || !*b) { put("logcat: -m needs a module\n"); return -1; }
        o->mod = b; return 2;
    }
    if (strcmp(a, "-k") == 0) {
        if (!b || !*b) { put("logcat: -k needs a keyword\n"); return -1; }
        o->key = b; return 2;
    }
    if (strcmp(a, "-t") == 0) {
        if (!b || !lc_num(b, &o->sec) || o->sec < 0) { put("logcat: -t needs seconds\n"); return -1; }
        return 2;
    }
    if (strcmp(a, "-n") == 0) {
        if (!b || !lc_num(b, &o->count) || o->count < 1 || o->count > 1024) { put("logcat: -n needs a count 1-1024\n"); return -1; }
        return 2;
    }
    if (strcmp(a, "-f") == 0) { o->follow = 1; return 1; }
    put("logcat: unknown option ");
    put(a);
    put("\n");
    return -1;
}

static int lc_line_matches(const char *line, const lc_opts_t *o, u64 now_ms) {
    if (o->key && !lc_strstr(line, o->key)) return 0;
    if (o->lvl >= 0) {
        int tl = lc_line_level(line);
        if (tl < o->lvl) return 0;
    }
    if (o->mod && !lc_line_module_is(line, o->mod)) return 0;
    if (o->sec >= 0) {
        u64 lms = lc_line_ms(line);
        u64 cutoff = (u64)o->sec * 1000u;
        if (now_ms > cutoff && lms < now_ms - cutoff) return 0;
    }
    return 1;
}

static int lc_split(char *s, const char *av[], int max) {
    int n = 0;
    while (s && *s && n < max) {
        while (*s == ' ') s++;
        if (!*s) break;
        av[n++] = s;
        while (*s && *s != ' ') s++;
        if (*s) *s++ = 0;
    }
    return n;
}

/* one replay pass; appends lines into the out buffer (clamped),
 * returns the count shown. */
static int lc_replay(const lc_opts_t *o) {
    char line[200];
    int total = klog_ring_count();
    u64 now_ms = core_timer_now_ms();
    int start = 0;
    if (o->count > 0 && o->count < total) start = total - (int)o->count;
    int shown = 0;
    for (int i = start; i < total; i++) {
        int n = klog_ring_get(i, line, sizeof(line));
        if (n <= 0) continue;               /* filtered out / raced */
        if (!lc_line_matches(line, o, now_ms)) continue;
        put(line);
        shown++;
    }
    return shown;
}

/* ---- dmesg ------------------------------------------------------------ */
static int l1_dmesg(const char *args) {
    lc_opts_t o; o.lvl = -1; o.mod = 0; o.key = 0; o.sec = -1; o.count = 30; o.follow = 0;
    if (args && *args) {
        long v;
        if (!lc_num(args, &v) || v < 1 || v > 1024) {
            put("usage: dmesg [N]   (N = lines, default 30)\n");
            return 1;
        }
        o.count = v;
    }
    int shown = lc_replay(&o);
    put("-- ");
    put_u64((u64)shown);
    put(" lines (ring ");
    put_u64((u64)klog_ring_count());
    put("/1024, dropped ");
    put_u64(klog_dropped());
    put(") --\n");
    return 0;
}

/* ---- logcat ----------------------------------------------------------- */
static int l1_logcat(const char *args) {
    lc_opts_t o; o.lvl = -1; o.mod = 0; o.key = 0; o.sec = -1; o.count = 30; o.follow = 0;

    if (args && *args) {
        static char buf[192];
        strncpy(buf, args, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = 0;
        const char *av[12];
        int ac = lc_split(buf, av, 12);
        int i = 0;
        while (i < ac) {
            int used = lc_one_opt(av[i], (i + 1 < ac) ? av[i + 1] : 0, &o);
            if (used < 0) return 1;
            i += used;
        }
    }

    if (o.follow) {
        /* v2.2.0 follow mode: print new entries as they are recorded;
         * ENTER exits (console-side loop inside the op). */
        screen_console_puts("-- following (press ENTER to exit) --\n");
        extern int screen_console_in_pump(void);
        int last = klog_ring_count();
        int stop = 0;
        while (!stop) {
            if (screen_console_in_pump()) break;         /* a line is ready */
            int cur = klog_ring_count();
            if (cur > last) {
                char line[200];
                u64 now_ms = core_timer_now_ms();
                for (int i = last; i < cur; i++) {
                    int n = klog_ring_get(i, line, sizeof(line));
                    if (n > 0 && lc_line_matches(line, &o, now_ms))
                        screen_console_puts(line);
                }
                last = cur;
            }
            /* 10 Hz poll cadence: sleep ~100 ms in short hlt stretches,
             * checking for the exit key between them */
            u64 until = core_timer_now_ms() + 100;
            while (core_timer_now_ms() < until) {
                if (screen_console_in_pump()) { stop = 1; break; }
            }
        }
        screen_console_puts("-- follow stopped --\n");
        put("(follow session ended)\n");
        return 0;
    }

    int shown = lc_replay(&o);
    put("-- ");
    put_u64((u64)shown);
    put(" lines --\n");
    return 0;
}

/* ---- logctl ----------------------------------------------------------- */
static int l1_logctl(const char *args) {
    static char buf[128];
    buf[0] = 0;
    if (args && *args) { strncpy(buf, args, sizeof(buf) - 1); buf[sizeof(buf) - 1] = 0; }
    const char *av[6];
    int ac = lc_split(buf, av, 6);
    const char *sub = (ac > 0) ? av[0] : "status";

    if (strcmp(sub, "status") == 0) {
        put("klog status:\n  ring: ");
        put_u64((u64)klog_ring_count());
        put("/1024 entries, dropped ");
        put_u64(klog_dropped());
        put("\n  live level (console+serial): ");
        if (klog_get_level() < 0) put("muted");
        else {
            put_u64((u64)klog_get_level());
            put(" (");
            put(klog_level_name(klog_get_level()));
            put(")");
        }
        put("\n  level switches:");
        for (int l = 0; l <= 4; l++) {
            put(" ");
            put(klog_level_name(l));
            put("=");
            put(klog_get_level_enabled(l) ? "on" : "off");
        }
        put("\n  file: /var/log/kernel.log ");
        if (klog_file_open()) {
            put("open, ");
            put_u64(klog_file_bytes());
            put(" bytes (rotate at ");
            put_u64(klog_get_rotate_bytes());
            put(", keep ");
            put_u64((u64)klog_get_rotate_keep());
            put(")");
        } else put("not open (no filesystem mounted?)");
        if (klog_filter_active()) {
            put("\n  filter: ACTIVE");
            if (klog_filter_level() >= 0) {
                put(" level>=");
                put_u64((u64)klog_filter_level());
            }
            if (klog_filter_module()[0]) {
                put(" module=");
                put(klog_filter_module());
            }
            if (klog_filter_keyword()[0]) {
                put(" keyword=");
                put(klog_filter_keyword());
            }
        } else put("\n  filter: none");
        put("\n");
        return 0;
    }
    if (strcmp(sub, "level") == 0) {
        if (ac < 2) {
            put("live level = ");
            put_u64((u64)klog_get_level());
            put(" (-1 muted, 0..4)\n");
            return 0;
        }
        long v;
        if (!lc_num(av[1], &v) || v < -1 || v > 4) {
            put("logctl: level needs -1..4\n");
            return 1;
        }
        klog_set_level((int)v);
        put("live level set\n");
        return 0;
    }
    if (strcmp(sub, "enable") == 0 || strcmp(sub, "disable") == 0) {
        if (ac < 2) {
            put("usage: logctl enable|disable debug|info|warn|error|fatal\n");
            return 1;
        }
        int l = lc_level_of(av[1]);
        if (l < 0) { put("logctl: bad level\n"); return 1; }
        klog_set_level_enabled(l, sub[0] == 'e' ? 1 : 0);
        put(sub[0] == 'e' ? "level enabled\n" : "level disabled\n");
        return 0;
    }
    if (strcmp(sub, "clear") == 0) {
        klog_clear();
        put("ring cleared\n");
        return 0;
    }
    if (strcmp(sub, "flush") == 0) {
        int n = klog_flush();
        if (n < 0) { put("klog: flush FAILED (no filesystem mounted?)\n"); return 1; }
        put("klog: flushed ");
        put_u64((u64)n);
        put(" bytes to /var/log/kernel.log\n");
        return 0;
    }
    if (strcmp(sub, "threshold") == 0) {
        /* rotation threshold (bytes; default 1048576). E2E gate sets a
         * small one to exercise the real auto-rotate path quickly. */
        if (ac < 2) {
            put("usage: logctl threshold BYTES (min 512, default 1048576)\n");
            return 0;
        }
        long v;
        if (!lc_num(av[1], &v) || v < 512 || v > 16777216) {
            put("logctl: threshold needs 512..16777216\n");
            return 1;
        }
        klog_set_rotate_bytes((u64)v);
        put("rotate threshold set\n");
        return 0;
    }
    if (strcmp(sub, "rotate") == 0) {
        int r = klog_rotate();
        if (r < 0) { put("klog: rotate FAILED (no filesystem mounted?)\n"); return 1; }
        put("klog: rotated (kernel.log -> kernel.log.1, keep 3)\n");
        return 0;
    }
    if (strcmp(sub, "export") == 0) {
        const char *path = (ac >= 2) ? av[1] : "/var/log/kernel-export.log";
        int fd = fs_vfs_open(path, VFS_O_WRONLY | VFS_O_CREAT | VFS_O_TRUNC);
        if (fd < 0) { put("klog: export FAILED (no filesystem mounted?)\n"); return 1; }
        char line[200];
        int bytes = 0, lines = 0;
        int total = klog_ring_count();
        for (int i = 0; i < total; i++) {
            int n = klog_ring_get(i, line, sizeof(line));
            if (n > 0) { bytes += fs_vfs_write(fd, line, n); lines++; }
        }
        fs_vfs_close(fd);
        put("klog: exported ");
        put_u64((u64)lines);
        put(" lines (");
        put_u64((u64)bytes);
        put(" bytes) to ");
        put(path);
        put("\n");
        return 0;
    }
    if (strcmp(sub, "test") == 0) {
        /* test hook: emit one line per level so the filters can be
         * verified against a known set (spec 5.4/5.6 tests). */
        klog_write(KLOG_DEBUG, "klogtest", "debug line (level 0)");
        klog_write(KLOG_INFO,  "klogtest", "info line (level 1)");
        klog_write(KLOG_WARN,  "klogtest", "warn line (level 2)");
        klog_write(KLOG_ERROR, "klogtest", "error line (level 3)");
        klog_write(KLOG_FATAL, "klogtest", "fatal line (level 4)");
        put("klog: 5 lines emitted (DEBUG..FATAL, module klogtest)\n");
        return 0;
    }
    if (strcmp(sub, "filter") == 0) {
        if (ac < 2) {
            put("usage: logctl filter clear | filter [level] [module] [keyword]\n");
            return 0;
        }
        if (strcmp(av[1], "clear") == 0) {
            klog_filter_clear();
            put("filter cleared\n");
            return 0;
        }
        int lvl = -1;
        if (av[1][0] && strcmp(av[1], "-") != 0) {
            lvl = lc_level_of(av[1]);
            if (lvl < 0) { put("logctl: bad level ('-' = any)\n"); return 1; }
        }
        const char *mod = (ac >= 3 && av[2][0] && strcmp(av[2], "-") != 0) ? av[2] : 0;
        const char *key = (ac >= 4 && av[3][0] && strcmp(av[3], "-") != 0) ? av[3] : 0;
        klog_filter_set(lvl, mod, key);
        put("filter set (applies to live channels AND dmesg/logcat)\n");
        return 0;
    }
    put("usage: logctl status|level|enable|disable|clear|flush|rotate|threshold|export|filter|test\n");
    return 1;
}

static int klog_l1_dispatch(u32 op, const char *arg, char *out, int cap) {
    if (!out || cap <= 0) return -1;
    s_out = out; s_left = cap;
    out[0] = 0;
    const char *a = arg ? arg : "";
    switch (op) {
    case L1SVC_KLOG_DMESG:  return l1_dmesg(a);
    case L1SVC_KLOG_LOGCAT: return l1_logcat(a);
    case L1SVC_KLOG_LOGCTL: return l1_logctl(a);
    default: return -1;
    }
}

/* Category string equals the services/klog folder name; the on-disk
 * channel bring-up stays in kmain (emit-hook installation). */
OC_SERVICE_DECL_L1("klog", "klog service (ring + on-disk channel + filters + ring-3 surface)",
                   (oc_service_init_fn_t)0, (oc_service_late_fn_t)0,
                   klog_l1_dispatch);
