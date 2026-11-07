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

/* ---- external surfaces -------------------------------------------- */
extern void screen_console_puts(const char* s);            /* drivers/display  */
extern void arch_exc_ser_puts(const char *s);              /* arch_exceptions  */

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

/* ---- 3.0.0-WP03 G5: the emit hook (the file sink installs itself) ---
 * The ring is the MECHANISM (core); the on-disk sink (services/klog)
 * registers a callback that runs after each task-context publish -
 * the old in-file fs_vfs calls are gone from core.  The sink keeps
 * its own dirty/synced/rotate state. */
static klog_emit_hook_fn g_emit_hook;

void klog_set_emit_hook(klog_emit_hook_fn fn) {
    g_emit_hook = fn;
}

/* ---- level switches + live-channel gates ---------------------------- */
static u32 g_level_mask = 0x1F;        /* all five levels on            */
static int g_live_level = KLOG_INFO;   /* console+serial verbosity      */

/* ---- active filter -------------------------------------------------- */
static int  g_flt_active  = 0;
static int  g_flt_level   = -1;
static char g_flt_module[KLOG_MOD_MAX];
static char g_flt_keyword[32];

/* ---- file channel ---------------------------------------------------- */

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
    g_emit_hook = 0;
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

    /* sink notification: only with interrupts enabled (task context);
     * the registered sink (services/klog, on-disk channel) syncs its
     * own tail here - a NULL hook means no sink yet (early boot or a
     * memory-only system), the entries stay in the ring */
    if (live_ok && g_emit_hook) g_emit_hook();

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

/* absolute seq -> physical slot (entry must still be inside the
 * window).  Ring mechanics: kept in core (the sink + the snapshot
 * both resolve through it). */
static klog_entry_t *klog_slot_of(u64 seq) {
    if (seq >= g_total) return 0;                 /* not written yet */
    if (seq + g_ring_count < g_total) return 0;   /* overwritten    */
    u32 off = (u32)(g_total - seq);               /* >= 1 here       */
    if (off > g_ring_cap) return 0;
    u32 slot = (g_ring_head + g_ring_cap - off) % g_ring_cap;
    return &g_ring[slot];
}

/* ---- 3.0.0-WP03 G5: replay accessors for the on-disk sink -----------
 * The sink (services/klog) walks the not-yet-synced entries through
 * klog_entry_render(): pass a seq cursor, get the formatted line
 * back, advance.  -1 = past the live window (stop). */
u64 klog_total_seq(void)  { return g_total; }
u64 klog_oldest_seq(void) { return g_total - g_ring_count; }

int klog_entry_render(u64 *seq_io, char *line, int cap) {
    if (!seq_io || !line || cap < 2) return -1;
    klog_entry_t *e = klog_slot_of(*seq_io);
    if (!e) return -1;
    int len = klog_format_entry(e, line, cap);
    (*seq_io)++;
    return len;
}

void klog_record(int level, const char *module, const char *msg) {
    /* record-only: ring + file, never the live channels (lib_log boot
     * stage lines are already printed by lib_log itself) */
    klog_emit(level, module, msg, 0);
    /* task-context file catch-up opportunity */
    if (g_emit_hook) {
        u64 flags;
        __asm__ volatile("pushfq; popq %0" : "=r"(flags) :: "memory");
        if (flags & 0x200) g_emit_hook();
    }
}

/* 3.0.0-WP03 G4: render the WHOLE ring (oldest -> newest) into a
 * caller buffer as formatted lines - the OCIRE log agent's
 * KLOG_SNAPSHOT op.  Returns the number of bytes written (NUL
 * terminated), or 0 when nothing is recorded / cap too small.
 * Takes the irq lock once so a concurrent writer cannot tear the
 * walk; entries published after the call simply stay for the next
 * snapshot. */
int klog_snapshot_text(char *out, int cap) {
    if (!out || cap < 2) return 0;
    int n = 0;
    out[0] = 0;
    u64 f = klog_irq_save();
    u32 count = g_ring_count;
    u32 start = (g_ring_head + g_ring_cap - count) % g_ring_cap;
    for (u32 i = 0; i < count; i++) {
        const klog_entry_t *e = &g_ring[(start + i) % g_ring_cap];
        char line[200];
        int len = klog_format_entry(e, line, sizeof(line));
        if (len < 0) len = 0;
        for (int k = 0; k < len; k++) {
            if (n >= cap - 1) break;
            out[n++] = line[k];
        }
        if (n >= cap - 1) break;          /* buffer full: honest cut */
    }
    klog_irq_restore(f);
    out[n] = 0;
    return n;
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

/* 3.0.0-WP03 G5: klog_flush / klog_flush_panic / klog_rotate moved
 * to services/klog/klog_file.c - they are file-channel operations. */

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
    /* (the on-disk sink in services/klog self-corrects: its cursor
     * walks against klog_total_seq, which restarted at 0) */
}

u64  klog_dropped(void)    { return g_dropped; }

