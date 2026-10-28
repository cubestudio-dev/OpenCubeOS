/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-12-watchdog_logging
 * File: kernel/lib/lib_klog.c
 * Purpose: unified kernel log implementation - see lib_klog.h.
 *
 * Layout of the ring (static BSS, no heap dependency so the earliest
 * boot stage can log):
 *
 *   klog_entry_t { u64 ts_ms; u8 level; char module[12]; char msg[144]; }
 *   1024 entries = 168 KiB
 *
 * Absolute sequence numbers: g_total counts every entry ever written
 * (monotonic, never wraps in practice); entry with absolute seq S
 * lives at slot (g_ring_head - (g_total - S)) mod 1024 while it is
 * still inside the window.  The file channel keeps its own cursor
 * g_file_synced in the SAME absolute space, so catching up is a plain
 * "append everything from cursor to total" loop and entries lost to
 * the wrap (g_total - count > cursor) are skipped forward honestly -
 * the .1 generation simply starts at the oldest entry still held.
 *
 * Concurrency: the ring publish (slot write + head/total/count bump)
 * is a short pushfq/cli critical section; readers only ever see fully
 * written slots.  The file channel is only touched with interrupts
 * enabled (task context); IRQ-context writers set the dirty flag and
 * the 1 s timer kick / next task writer / any flush catches up.
 */
#include "lib_klog.h"
#include "lib_string.h"
#include "core_timer.h"
#include "mem_heap.h"             /* kmalloc for klog_grow()  */
#include "fs_vfs.h"                    /* real stat struct + flags */

/* ---- external surfaces -------------------------------------------- */
extern void screen_console_puts(const char* s);            /* drivers/display  */
extern void arch_exc_ser_puts(const char *s);              /* arch_exceptions  */
extern int  fs_vfs_mkdir(const char *path);
extern int  fs_vfs_open(const char *path, int flags);
extern int  fs_vfs_write(int fd, const void *buf, int size);
extern int  fs_vfs_close(int fd);
extern int  fs_vfs_unlink(const char *path);
extern int  fs_vfs_rename(const char *oldpath, const char *newpath);

/* ---- entry + ring --------------------------------------------------- */
#define KLOG_RING_EARLY 24                 /* static BSS bootstrap ring (4 KiB) */
#define KLOG_RING_MAX   1024               /* after klog_grow() (heap)   */
#define KLOG_MOD_MAX  12
#define KLOG_MSG_MAX  144

typedef struct {
    u64  ts_ms;
    u8   level;
    char module[KLOG_MOD_MAX];
    char msg[KLOG_MSG_MAX];
} klog_entry_t;

/* The identity-window ASSERT caps the kernel image below 0x400000,
 * so the big ring lives on the heap: a 64-entry static bootstrap ring
 * carries the earliest boot lines, and klog_grow() (called right after
 * mem_heap_init) kmallocs the 1024-entry ring and copies the boot
 * entries over - nothing is lost. */
static klog_entry_t g_ring_early[KLOG_RING_EARLY];
static klog_entry_t *g_ring = g_ring_early;
static u32 g_ring_cap = KLOG_RING_EARLY;
static volatile u32 g_ring_head = 0;   /* next write slot               */
static volatile u32 g_ring_count = 0;  /* valid entries (<= cap)        */
static volatile u64 g_total = 0;       /* absolute entries ever written */
static volatile u64 g_dropped = 0;     /* slots overwritten since boot  */

/* ---- level switches + live-channel gates ---------------------------- */
static u32 g_level_mask = 0x1F;        /* all five levels on            */
static int g_live_level = KLOG_INFO;   /* console+serial verbosity      */

/* ---- active filter -------------------------------------------------- */
static int  g_flt_active  = 0;
static int  g_flt_level   = -1;
static char g_flt_module[KLOG_MOD_MAX];
static char g_flt_keyword[32];

/* ---- file channel ---------------------------------------------------- */
static int  g_file_fd = -1;            /* open O_APPEND fd, -1 = down   */
static u64  g_file_bytes = 0;          /* bytes in the ACTIVE file      */
static u64  g_file_synced = 0;         /* absolute seq already appended */
static u64  g_rotate_bytes = KLOG_ROTATE_BYTES;
static u32  g_rotate_keep  = KLOG_ROTATE_KEEP;
static volatile u32 g_file_dirty = 0;  /* ring tail not appended yet    */

/* ---- small local helpers (lib_string has no strstr) ----------------- */
static char *k_strstr(const char *hay, const char *needle) {
    if (!needle || !*needle) return (char*)hay;
    for (const char *h = hay; *h; h++) {
        const char *a = h, *b = needle;
        while (*a && *b && *a == *b) { a++; b++; }
        if (!*b) return (char*)h;
    }
    return 0;
}

static u64 klog_irq_save(void) {
    u64 f;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(f) :: "memory");
    return f;
}
static void klog_irq_restore(u64 f) {
    if (f & 0x200) __asm__ volatile("sti" ::: "memory");
}

const char *klog_level_name(int level) {
    /* no trailing-pad: callers add their own spacing, and the shell
     * filter parser tokenizes on spaces (a padded "INFO " made the
     * 4-char names unmatchable - found live by the WP-12 logcat test) */
    static const char *names[5] = {"DEBUG", "INFO", "WARN", "ERROR", "FATAL"};
    if (level < KLOG_LVL_MIN || level > KLOG_LVL_MAX) return "?";
    return names[level];
}

/* ---- the freestanding formatter (same grammar as tui_printw) -------- */
static int klog_vformat(char *out, int cap, const char *fmt, __builtin_va_list ap) {
    int n = 0;
    for (const char *p = fmt; *p && n < cap - 1; p++) {
        if (*p != '%') { out[n++] = *p; continue; }
        p++;
        if (*p == 0) break;
        char num[24];
        if (*p == 's') {
            const char *s = __builtin_va_arg(ap, const char *);
            if (!s) s = "(null)";
            while (*s && n < cap - 1) out[n++] = *s++;
        } else if (*p == 'd') {
            long long v = (long long)__builtin_va_arg(ap, int);
            if (v < 0 && n < cap - 1) { out[n++] = '-'; v = -v; }
            u64_to_str((u64)v, num);
            for (char *q = num; *q && n < cap - 1; q++) out[n++] = *q;
        } else if (*p == 'u') {
            u64_to_str((u64)__builtin_va_arg(ap, unsigned int), num);
            for (char *q = num; *q && n < cap - 1; q++) out[n++] = *q;
        } else if (*p == 'x') {
            u64_to_hex((u64)__builtin_va_arg(ap, unsigned int), num, 1);
            for (char *q = num; *q && n < cap - 1; q++) out[n++] = *q;
        } else if (*p == 'c') {
            int c = __builtin_va_arg(ap, int);
            if (n < cap - 1) out[n++] = (char)c;
        } else if (*p == '%') {
            if (n < cap - 1) out[n++] = '%';
        } else {
            if (n < cap - 1) out[n++] = '%';
            if (n < cap - 1) out[n++] = *p;
        }
    }
    out[n] = 0;
    return n;
}

/* ---- one formatted line (replay + file + channels share it) --------- */
static int klog_format_entry(const klog_entry_t *e, char *out, int cap) {
    char num[24];
    int n = 0;
    out[n++] = '[';
    u64_to_str(e->ts_ms / 1000, num);
    for (char *q = num; *q && n < cap - 1; q++) out[n++] = *q;
    out[n++] = '.';
    u64_to_str(e->ts_ms % 1000, num);
    int fl = (int)strlen(num);               /* pad fraction to 3 digits */
    for (int pad = fl; pad < 3 && n < cap - 1; pad++) out[n++] = '0';
    for (char *q = num; *q && n < cap - 1; q++) out[n++] = *q;
    if (n < cap - 1) out[n++] = ']';
    if (n < cap - 1) out[n++] = ' ';
    /* level tag padded to a 6-column field so the module column
     * aligns (DEBUG/INFO/WARN are 4-5 chars) */
    {
        const char *nm = klog_level_name(e->level);
        for (const char *q = nm; *q && n < cap - 1; q++) out[n++] = *q;
        for (int pad = (int)strlen(nm); pad < 6 && n < cap - 1; pad++)
            out[n++] = ' ';
    }
    for (const char *q = e->module; *q && n < cap - 1; q++) out[n++] = *q;
    if (n < cap - 1) out[n++] = ':';
    if (n < cap - 1) out[n++] = ' ';
    for (const char *q = e->msg; *q && n < cap - 1; q++) out[n++] = *q;
    if (n < cap - 1) out[n++] = '\n';
    out[n] = 0;
    return n;
}

/* ---- filter matching ------------------------------------------------- */
static int klog_entry_matches(const klog_entry_t *e) {
    if (!g_flt_active) return 1;
    if (g_flt_level >= 0 && e->level < g_flt_level) return 0;
    if (g_flt_module[0] && strncmp(e->module, g_flt_module, KLOG_MOD_MAX) != 0)
        return 0;
    if (g_flt_keyword[0] && !k_strstr(e->msg, g_flt_keyword)) return 0;
    return 1;
}

/* ---- file channel plumbing ------------------------------------------ */
static void klog_file_close(void) {
    if (g_file_fd >= 0) { fs_vfs_close(g_file_fd); g_file_fd = -1; }
}

/* shift generations: kernel.log -> .1 -> .2 -> ... (keep N) */
static int klog_file_shift(void) {
    char from[48], to[48];
    static const char *base = KLOG_PATH;

    if (g_rotate_keep >= 1) {
        /* drop the oldest generation (best-effort) */
        strcpy(from, base); strcat(from, ".");
        u64_to_str((u64)g_rotate_keep, from + strlen(from));
        fs_vfs_unlink(from);
        /* rename .1 -> .2 ... (keep-1) -> keep, newest last */
        for (u32 gen = g_rotate_keep; gen >= 2; gen--) {
            strcpy(from, base); strcat(from, ".");
            u64_to_str((u64)(gen - 1), from + strlen(from));
            strcpy(to, base); strcat(to, ".");
            u64_to_str((u64)gen, to + strlen(to));
            fs_vfs_rename(from, to);
            if (gen == 2) break;   /* guard: u32 would wrap below 2 */
        }
        /* active -> .1 */
        strcpy(from, base);
        strcpy(to, base); strcat(to, ".1");
        fs_vfs_rename(from, to);
    }
    g_file_bytes = 0;
    return 0;
}

/* ensure /var/log + an open append fd; returns fd or -1 */
static int klog_file_open_(void) {
    if (g_file_fd >= 0) return g_file_fd;
    fs_vfs_mkdir("/var");
    fs_vfs_mkdir("/var/log");
    g_file_fd = fs_vfs_open(KLOG_PATH, VFS_O_WRONLY | VFS_O_CREAT | VFS_O_APPEND);
    if (g_file_fd >= 0) {
        fs_vfs_stat_t st;
        g_file_bytes = (fs_vfs_stat(KLOG_PATH, &st) == 0) ? st.size : 0;
    }
    return g_file_fd;
}

/* absolute seq -> physical slot (entry must still be inside the window) */
static klog_entry_t *klog_slot_of(u64 seq) {
    if (seq >= g_total) return 0;                 /* not written yet */
    if (seq + g_ring_count < g_total) return 0;   /* overwritten    */
    u32 off = (u32)(g_total - seq);               /* >= 1 here       */
    if (off > g_ring_cap) return 0;
    u32 slot = (g_ring_head + g_ring_cap - off) % g_ring_cap;
    return &g_ring[slot];
}

/* append every not-yet-synced ring entry.  TASK CONTEXT ONLY (callers
 * gate on interrupts being enabled).  Returns bytes appended, or -1
 * when no filesystem is mounted. */
static int klog_file_sync(void) {
    if (klog_file_open_() < 0) return -1;

    u64 total = g_total;
    u64 from = g_file_synced;
    u64 oldest = g_total - g_ring_count;   /* first seq still held */
    if (from < oldest) from = oldest;      /* skip entries the wrap ate */
    if (from >= total) { g_file_synced = total; g_file_dirty = 0; return 0; }

    char line[200];
    int total_bytes = 0;
    for (u64 seq = from; seq < total; seq++) {
        klog_entry_t *e = klog_slot_of(seq);
        if (!e) continue;
        int len = klog_format_entry(e, line, sizeof(line));
        int w = fs_vfs_write(g_file_fd, line, len);
        if (w < 0) break;                  /* open/fs hiccup: retry next */
        total_bytes += w;
        g_file_bytes += (u64)w;
        g_file_synced = seq + 1;
    }
    g_file_dirty = (g_file_synced < g_total) ? 1 : 0;

    /* automatic rotation check */
    if (g_file_bytes >= g_rotate_bytes) {
        klog_file_close();
        klog_file_shift();
        klog_file_open_();
        /* the fresh file starts empty; the rotated entries live in .1;
         * the ring remains replayable for dmesg regardless */
    }
    return total_bytes;
}

/* ===================== init / mount hooks ============================ */
void klog_init(void) {
    g_ring_head = 0;
    g_ring_count = 0;
    g_total = 0;
    g_dropped = 0;
    g_level_mask = 0x1F;
    g_live_level = KLOG_INFO;
    g_flt_active = 0;
    g_flt_level = -1;
    g_flt_module[0] = 0;
    g_flt_keyword[0] = 0;
    g_file_fd = -1;
    g_file_bytes = 0;
    g_file_synced = 0;
    g_file_dirty = 0;
    g_rotate_bytes = KLOG_ROTATE_BYTES;
    g_rotate_keep = KLOG_ROTATE_KEEP;
}

void klog_on_mount(void) {
    /* first filesystem: open the channel and dump the boot log */
    if (klog_file_open_() >= 0) {
        klog_file_sync();
    }
}

/* ===================== the writers ==================================== */
/* core publish path. live_ok: interrupts enabled (task context) ->
 * live channels + file sync allowed. */
static int klog_emit(int level, const char *module, const char *msg, int live_ok) {
    if (level < KLOG_LVL_MIN || level > KLOG_LVL_MAX) return -1;
    if (!(g_level_mask & (1u << level))) return -1;

    klog_entry_t e;
    e.ts_ms = core_timer_now_ms();
    e.level = (u8)level;
    strncpy(e.module, module ? module : "kernel", KLOG_MOD_MAX - 1);
    e.module[KLOG_MOD_MAX - 1] = 0;
    strncpy(e.msg, msg ? msg : "", KLOG_MSG_MAX - 1);
    e.msg[KLOG_MSG_MAX - 1] = 0;

    /* publish into the ring under the irq lock */
    u64 f = klog_irq_save();
    g_ring[g_ring_head] = e;
    g_ring_head = (g_ring_head + 1) % g_ring_cap;
    if (g_ring_count < g_ring_cap) g_ring_count++;
    else g_dropped++;
    g_total++;
    klog_irq_restore(f);

    /* live channels (gated by verbosity + the active filter).
     * screen_console ONLY: the shell console hook already mirrors
     * every console line to the serial port, so adding a direct
     * arch_exc_ser_puts here printed every live line TWICE on serial
     * (found live by the WP-12 logctl test).  The panic path keeps
     * its own explicit ser_puts for the hostile-context case. */
    if (live_ok && level >= g_live_level && klog_entry_matches(&e)) {
        char line[200];
        klog_format_entry(&e, line, sizeof(line));
        screen_console_puts(line);
    }

    /* file channel: only with interrupts enabled (task context) */
    if (live_ok && g_file_fd >= 0) klog_file_sync();
    else g_file_dirty = 1;

    return (int)((g_ring_head + KLOG_RING_MAX - 1) % KLOG_RING_MAX);
}

int klog_write(int level, const char *module, const char *fmt, ...) {
    /* format into a stack buffer FIRST (vformat needs task-safety only
     * for the va_list, the ring publish below is the irq-safe part) */
    char msg[KLOG_MSG_MAX];
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    klog_vformat(msg, sizeof(msg), fmt, ap);
    __builtin_va_end(ap);

    /* task-context probe: IF set -> live channels + file sync are ok */
    u64 flags;
    __asm__ volatile("pushfq; popq %0" : "=r"(flags) :: "memory");
    int live_ok = (flags & 0x200) ? 1 : 0;
    return klog_emit(level, module, msg, live_ok);
}

void klog_record(int level, const char *module, const char *msg) {
    /* record-only: ring + file, never the live channels (lib_log boot
     * stage lines are already printed by lib_log itself) */
    klog_emit(level, module, msg, 0);
    /* task-context file catch-up opportunity */
    if (g_file_fd >= 0 && g_file_dirty) {
        u64 flags;
        __asm__ volatile("pushfq; popq %0" : "=r"(flags) :: "memory");
        if (flags & 0x200) klog_file_sync();
    }
}

/* ---- levels / switches ---------------------------------------------- */
int klog_set_level(int level) {
    if (level < -1 || level > KLOG_LVL_MAX) return g_live_level;
    int prev = g_live_level;
    g_live_level = level;
    return prev;
}

int klog_get_level(void) { return g_live_level; }

int klog_set_level_enabled(int level, int on) {
    if (level < KLOG_LVL_MIN || level > KLOG_LVL_MAX) return -1;
    int prev = (g_level_mask >> level) & 1;
    if (on) g_level_mask |= (1u << level);
    else    g_level_mask &= ~(1u << level);
    return prev;
}

int klog_get_level_enabled(int level) {
    if (level < KLOG_LVL_MIN || level > KLOG_LVL_MAX) return -1;
    return (int)((g_level_mask >> level) & 1);
}

/* ---- flush / rotate -------------------------------------------------- */
int klog_flush(void) {
    u64 flags;
    __asm__ volatile("pushfq; popq %0" : "=r"(flags) :: "memory");
    if (!(flags & 0x200)) return -1;     /* IRQ ctx: the timer/task will */
    return klog_file_sync();
}

/* Panic-path variant: interrupts are OFF and this is the last chance -
 * sync unconditionally (crashlog_flush runs the same VFS writes from
 * the same depth, empirically reliable). */
int klog_flush_panic(void) {
    return klog_file_sync();
}

int klog_rotate(void) {
    u64 flags;
    __asm__ volatile("pushfq; popq %0" : "=r"(flags) :: "memory");
    if (!(flags & 0x200)) return -1;
    klog_file_sync();                    /* never lose the pending tail */
    klog_file_close();
    if (klog_file_open_() < 0 && g_file_fd < 0) return -1;  /* no fs */
    klog_file_close();
    klog_file_shift();
    klog_file_open_();
    return 0;
}

/* ---- filters ---------------------------------------------------------- */
void klog_filter_set(int level, const char *module, const char *keyword) {
    g_flt_active = 1;
    g_flt_level = (level >= KLOG_LVL_MIN && level <= KLOG_LVL_MAX) ? level : -1;
    if (module && *module) {
        strncpy(g_flt_module, module, KLOG_MOD_MAX - 1);
        g_flt_module[KLOG_MOD_MAX - 1] = 0;
    } else g_flt_module[0] = 0;
    if (keyword && *keyword) {
        strncpy(g_flt_keyword, keyword, sizeof(g_flt_keyword) - 1);
        g_flt_keyword[sizeof(g_flt_keyword) - 1] = 0;
    } else g_flt_keyword[0] = 0;
}

void klog_filter_clear(void) {
    g_flt_active = 0;
    g_flt_level = -1;
    g_flt_module[0] = 0;
    g_flt_keyword[0] = 0;
}

int  klog_filter_active(void)      { return g_flt_active; }
int  klog_filter_level(void)       { return g_flt_level; }
const char *klog_filter_module(void)   { return g_flt_module; }
const char *klog_filter_keyword(void)  { return g_flt_keyword; }

/* ---- replay ----------------------------------------------------------- */
int klog_ring_count(void) { return (int)g_ring_count; }

int klog_ring_get(int idx, char *out, int cap) {
    if (!out || cap < 16) return -1;
    u64 count = g_ring_count;
    if (idx < 0 || (u64)idx >= count) return -1;
    /* idx 0 = OLDEST held entry; absolute seq = total - count + idx */
    u64 seq = g_total - count + (u64)idx;
    klog_entry_t *e = klog_slot_of(seq);
    if (!e) return -1;
    /* the filter applies to replay as well (spec 5.6) */
    if (!klog_entry_matches(e)) return 0;   /* filtered out */
    return klog_format_entry(e, out, cap);
}

/* WP-12: heap is up - swap the static bootstrap ring for the full
 * 1024-entry kmalloc'd ring, carrying every early entry over. */
void klog_grow(void) {
    if (g_ring_cap == KLOG_RING_MAX) return;    /* already grown     */
    klog_entry_t *big = (klog_entry_t*)kmalloc(KLOG_RING_MAX * sizeof(klog_entry_t));
    if (!big) return;                           /* stay on early ring */
    memset(big, 0, KLOG_RING_MAX * sizeof(klog_entry_t));
    u64 f = klog_irq_save();
    for (u64 seq = g_total - g_ring_count; seq < g_total; seq++) {
        /* early ring never wrapped (64 entries, < 64 boot lines before
         * the heap); the branch still handles it honestly */
        u32 off = (u32)(g_total - seq);
        u32 src = (g_ring_head + g_ring_cap - off) % g_ring_cap;
        u32 dst = (u32)(seq % KLOG_RING_MAX);
        big[dst] = g_ring[src];
    }
    g_ring = big;
    g_ring_cap = KLOG_RING_MAX;
    g_ring_head = (u32)(g_total % KLOG_RING_MAX);
    klog_irq_restore(f);
}

void klog_clear(void) {
    u64 f = klog_irq_save();
    g_ring_head = 0;
    g_ring_count = 0;
    g_total = 0;
    g_dropped = 0;
    klog_irq_restore(f);
    g_file_synced = 0;
    g_file_dirty = 0;
}

u64  klog_file_bytes(void) { return g_file_bytes; }
int  klog_file_open(void)  { return g_file_fd >= 0 ? 1 : 0; }
u64  klog_dropped(void)    { return g_dropped; }

u64  klog_set_rotate_bytes(u64 bytes) {
    u64 prev = g_rotate_bytes;
    if (bytes >= 512) g_rotate_bytes = bytes;   /* sane floor: one page+ */
    return prev;
}
u32  klog_set_rotate_keep(u32 keep) {
    u32 prev = g_rotate_keep;
    if (keep >= 1 && keep <= 9) g_rotate_keep = keep;
    return prev;
}
u64  klog_get_rotate_bytes(void) { return g_rotate_bytes; }
u32  klog_get_rotate_keep(void)  { return g_rotate_keep; }
