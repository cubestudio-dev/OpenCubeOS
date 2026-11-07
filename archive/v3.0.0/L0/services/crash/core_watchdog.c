/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-12-watchdog_logging
 * File: kernel/core/core_watchdog.c
 * Purpose: unified watchdog implementation - see core_watchdog.h.
 *
 * Hardware probe:
 *   WDAT first (the table is authoritative when present - QEMU injects
 *   one via -acpitable for the E2E gate), then the fixed Intel TCO
 *   layout on the LPC bridge.  The two are mutually exclusive at
 *   runtime (whichever probes first wins), and every action goes
 *   through ONE function-pointer-free switch so the shell output can
 *   name the exact mechanism.
 *
 * TCO register map (ICH9 / QEMU hw/acpi/tco.c, TCOBASE = PMBASE+0x60):
 *   +0x00 TCO_RLD   write ANY value -> reload the countdown
 *   +0x04 TCO1_STS  status bits (we clear TIMEOUT on start)
 *   +0x06 TCO2_STS  (BOOT_STS lives here)
 *   +0x08 TCO1_CNT  bit0 TMR_HLT: 1 = halted, 0 = running
 *   +0x12 TCOv2_TMR countdown seconds (6 bits on QEMU)
 *
 * WDAT layout (ACPI 5.2+, byte offsets from the table start):
 *    0  header (36 bytes: signature 'WDAT', length, ...)
 *   36  Header Length (u32)  - offset of the entries array
 *   40  PCI segment/bus/device/function (u16 each)
 *   48  Timer Period ms (u32) / Max Count / Min Count
 *   60  Flags (u8) bit0 = Running; Reserved u8[3]
 *   64  Number of entries (u32)
 *   68  entries: 24 bytes each {
 *        u8 action; u8 instruction; u16 reserved;
 *        acpi_gas_t reg (12B); u32 value; u32 mask; }
 *   Actions used here (ACPI_WDAT_*): 4 SET_COUNTDOWN, 5 RUNNING,
 *   6 STOP, 3 GET_COUNTDOWN? (QEMU WDAT E2E uses SET/RUNNING/STOP).
 *
 * Soft slots: static array, 1 s periodic tick, policy switch as
 * documented in the header.  The tick itself is IRQ context: klog
 * goes through the IRQ-safe ring path, KILL uses
 * core_sched_task_exited (the same primitive `kill` uses), PANIC/REBOOT
 * never return.
 */
#include "core_watchdog.h"
#include "lib_string.h"
#include "lib_klog.h"
#include "core_timer.h"
#include "core_sched.h"
#include "core_power.h"
#include "core_crash.h"        /* kernel_panic (policy PANIC)         */
#include "driver_pci.h"
#include "driver_acpi.h"

/* ================================================================== *
 * port primitives (same inline-asm style as core_power.c)
 * ================================================================== */
static inline void wd_outw(u16 port, u16 val) {
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}
static inline u16 wd_inw(u16 port) {
    u16 v;
    __asm__ volatile("inw %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

/* ================================================================== *
 * GCS / NO_REBOOT (ICH9, the Linux iTCO_wdt "update_no_reboot_bit" step)
 *
 * The second TCO timeout only RESETS the machine when the GCS
 * register's NO_REBOOT bit (bit 5) is CLEAR.  QEMU models GCS at
 * RCBA + 0x3410 (MMIO; RCBA = LPC config 0xF0, bit 0 = enabled,
 * bits 31:14 = base).  Without clearing it, the QEMU trace shows
 * expired(no=0/1) oscillating forever with no reset - found live
 * during the WP-12 bring-up.  start() clears the bit (arming the
 * hard reset), stop() sets it back.
 * ================================================================== */
static u64 g_rcba = 0;

static void tco_no_reboot(int set) {
    if (!g_rcba) return;
    volatile u32 *gcs = (volatile u32*)(uintptr_t)(g_rcba + 0x3410);  /* ICH9_CC_GCS (QEMU hw/i386/ich9.h: 0x3410, NOT 0x3418) */
    u32 v = *gcs;
    if (set) v |= (1u << 5);
    else     v &= ~(1u << 5);
    *gcs = v;
}

/* ================================================================== *
 * hardware backend state
 * ================================================================== */
#define WD_HW_NONE 0
#define WD_HW_TCO  1
#define WD_HW_WDAT 2

static int  g_hw_kind = WD_HW_NONE;
static u16  g_tco_base = 0;          /* TCOBASE (0 = absent)          */
static u32  g_hw_timeout_s = 0;      /* current hw timeout, seconds  */
static int  g_hw_armed = 0;

/* ---- WDAT entry cache (parsed once at init) -------------------------- */
#define WDAT_MAX_ENTRIES 16
typedef struct {
    u8         action;               /* ACPI_WDAT_*                   */
    u8         instruction;          /* 1 write / 3 read-modify-write */
    acpi_gas_t reg;
    u32        value;
    u32        mask;
} wdat_entry_t;

static wdat_entry_t g_wdat[WDAT_MAX_ENTRIES];
static int  g_wdat_count = 0;
static u32  g_wdat_period_ms = 0;

/* find the entry for an action, or NULL */
static const wdat_entry_t *wdat_find(int action) {
    for (int i = 0; i < g_wdat_count; i++)
        if (g_wdat[i].action == action) return &g_wdat[i];
    return 0;
}

/* execute one WDAT instruction (write / read-modify-write) */
static void wdat_exec(const wdat_entry_t *e, u32 value) {
    if (!e) return;
    u32 v = value;
    if (e->mask) v = (v & e->mask) | (e->value & ~e->mask);
    if (e->instruction == 1) {           /* WRITE_REGISTER */
        driver_acpi_gas_write(&e->reg, e->reg.bit_width ? e->reg.bit_width / 8 : 4, v);
    } else if (e->instruction == 3) {    /* WRITE_REGISTER_PRESERVE */
        u64 cur = driver_acpi_gas_read(&e->reg, e->reg.bit_width ? e->reg.bit_width / 8 : 4);
        u32 merged = (u32)cur & ~e->mask;
        merged |= (value & e->mask);
        driver_acpi_gas_write(&e->reg, e->reg.bit_width ? e->reg.bit_width / 8 : 4, merged);
        klog_write(KLOG_DEBUG, "watchdog",
                   "wdat rmw action=%d space=%d addr=0x%x: cur=0x%x -> 0x%x",
                   e->action, e->reg.address_space,
                   (u32)e->reg.address, (u32)cur, merged);
    }
}

/* ================================================================== *
 * hardware probe
 * ================================================================== */
static void hw_probe_wdat(void) {
    u64 phys = driver_acpi_find_table("WDAT", 0);
    if (!phys) return;

    /* map-free read: the ACPI tables are in the low identity window,
     * covered by driver_acpi's cached copies; read via the phys map
     * the ACPI core validates.  Fallback: direct phys read is not
     * available pre-VMM - use the cache the driver keeps. */
    extern int driver_acpi_table_info(int index, char sig_out[5],
                                      u32 *length_out, u8 *revision_out,
                                      u64 *phys_out);
    /* The find_table() above already validated the header; read the
     * body through the same low-memory window (RSDT region is inside
     * the 0x400000-0x600000 ACPI mapping on QEMU; phys < 4 GiB and
     * the kernel identity map covers the ACPI tables the driver
     * cached). */
    const u8 *tbl = (const u8*)(uintptr_t)phys;
    if (!tbl) return;

    /* sanity: signature + length */
    if (tbl[0] != 'W' || tbl[1] != 'D' || tbl[2] != 'A' || tbl[3] != 'T') return;
    u32 total_len = (u32)tbl[4] | ((u32)tbl[5] << 8) | ((u32)tbl[6] << 16) | ((u32)tbl[7] << 24);
    if (total_len < 68 + 24 || total_len > 4096) return;

    u32 hdr_len  = (u32)tbl[36] | ((u32)tbl[37] << 8) | ((u32)tbl[38] << 16) | ((u32)tbl[39] << 24);
    u32 nentries = (u32)tbl[64] | ((u32)tbl[65] << 8) | ((u32)tbl[66] << 16) | ((u32)tbl[67] << 24);
    g_wdat_period_ms = (u32)tbl[48] | ((u32)tbl[49] << 8) | ((u32)tbl[50] << 16) | ((u32)tbl[51] << 24);

    /* entries offset: honour Header Length (Linux uses the same
     * field); accept the two layouts seen in the wild (68 with the
     * field counting from the table start, or 32 counting after the
     * 36-byte ACPI header). */
    u32 off = hdr_len;
    if (off < 36 || off > total_len - 24) off = 68;    /* fall back to spec layout */
    if (nentries > WDAT_MAX_ENTRIES) nentries = WDAT_MAX_ENTRIES;

    int n = 0;
    for (u32 i = 0; i < nentries; i++) {
        const u8 *p = tbl + off + i * 24;
        if ((u64)(uintptr_t)(p + 24) > (u64)(uintptr_t)(tbl + total_len)) break;
        wdat_entry_t e;
        e.action = p[0];
        e.instruction = p[1];
        e.reg.address_space = p[4];
        e.reg.bit_width = p[5];
        e.reg.bit_offset = p[6];
        e.reg.access_size = p[7];
        e.reg.address = 0;
        for (int b = 0; b < 8; b++) e.reg.address |= (u64)p[8 + b] << (8 * b);
        /* entry layout: action(1) instr(1) reserved(2) gas(12) value(4)
         * mask(4) = 24 bytes; value sits at +16, mask at +20 */
        e.value = (u32)p[16] | ((u32)p[17] << 8) | ((u32)p[18] << 16) | ((u32)p[19] << 24);
        e.mask  = (u32)p[20] | ((u32)p[21] << 8) | ((u32)p[22] << 16) | ((u32)p[23] << 24);
        if (e.reg.address_space > 2) continue;      /* 0=mem 1=io 2=pci: all valid GAS */
        g_wdat[n++] = e;
    }
    g_wdat_count = n;
    if (n > 0) {
        g_hw_kind = WD_HW_WDAT;
        klog_write(KLOG_INFO, "watchdog", "ACPI WDAT found: %d entries, period %u ms",
                   n, g_wdat_period_ms);
    }
}

static void hw_probe_tco(void) {
    /* LPC/ISA bridge at 00:1f.0, class 0x0601 (QEMU q35 ICH9-LPC).
     * PMBASE lives at config offset 0x40 (bit0 = enable, bits 7-15 =
     * base).  TCOBASE = PMBASE + 0x60.  RCBA (config 0xF0) is the
     * chip-config MMIO base holding GCS (see tco_no_reboot). */
    u8 bus = 0, dev = 0x1f, fn = 0;
    u16 vendor = (u16)(driver_pci_read_config(bus, dev, fn, 0x00) & 0xFFFF);
    if (vendor != 0x8086) {
        /* non-Intel LPC at 1f.0 - try the class match instead */
        if (driver_pci_find_class(0x060100, &bus, &dev, &fn) != 0) return;
    }
    u32 pmbase_raw = driver_pci_read_config(bus, dev, fn, 0x40);
    if (!(pmbase_raw & 1)) {
        klog_write(KLOG_INFO, "watchdog", "TCO probe: PMBASE disabled (raw=0x%x)",
                   pmbase_raw);
        return;
    }
    u16 pmbase = (u16)(pmbase_raw & 0xFF80);
    u16 tcobase = (u16)(pmbase + 0x60);

    /* RCBA for the GCS NO_REBOOT bit.  QEMU's LPC reset programs
     * ICH9_LPC_RCBA_DEFAULT into config 0xF0 and only maps the
     * chip-config MMIO when the ENABLE bit is set; SeaBIOS does not
     * touch it, so the driver enables it at the ICH9 default address
     * 0xFED1C000 itself (config write -> ich9_lpc_rcba_update -> the
     * rcrb MMIO appears).  GCS is then reachable at RCBA+0x3410. */
    u32 rcba_raw = driver_pci_read_config(bus, dev, fn, 0xF0);
    if (!(rcba_raw & 1)) {
        driver_pci_write_config(bus, dev, fn, 0xF0, 0xFED1C001u);
        rcba_raw = driver_pci_read_config(bus, dev, fn, 0xF0);
    }
    if (rcba_raw & 1) g_rcba = (u64)(rcba_raw & 0xFFFFC000ULL);
    else              g_rcba = 0xFED1C000ULL;
    {
        volatile u32 *gcs = (volatile u32*)(uintptr_t)(g_rcba + 0x3410);  /* ICH9_CC_GCS (QEMU hw/i386/ich9.h: 0x3410, NOT 0x3418) */
        u32 gcs_v = *gcs;
        klog_write(KLOG_INFO, "watchdog",
                   "TCO probe: RCBA=0x%x GCS=0x%x (NO_REBOOT bit %d)",
                   (u32)g_rcba, gcs_v, (gcs_v >> 5) & 1);
    }

    /* validate: reading TCO1_CNT twice must agree and look sane */
    u16 c1 = wd_inw(tcobase + 0x08);
    u16 c2 = wd_inw(tcobase + 0x08);
    if (c1 != c2) {
        klog_write(KLOG_INFO, "watchdog", "TCO probe: unstable CNT (0x%x/0x%x)", c1, c2);
        return;
    }
    g_tco_base = tcobase;
    g_hw_kind = WD_HW_TCO;
    /* boot-time view: TMR_HLT bit says whether the platform left the
     * dog running (QEMU leaves it HALTED after SeaBIOS) */
    g_hw_armed = ((c1 & 1) == 0) ? 1 : 0;
    klog_write(KLOG_INFO, "watchdog", "Intel TCO found: PMBASE=0x%x TCOBASE=0x%x CNT=0x%x (%s)",
               pmbase, tcobase, c1, g_hw_armed ? "RUNNING" : "halted");
}

/* ================================================================== *
 * hardware operations
 * ================================================================== */
int watchdog_hw_present(void) { return g_hw_kind != WD_HW_NONE; }
const char *watchdog_hw_name(void) {
    if (g_hw_kind == WD_HW_TCO)  return "Intel TCO";
    if (g_hw_kind == WD_HW_WDAT) return "ACPI WDAT";
    return "none";
}
u64  watchdog_hw_base(void) { return g_tco_base; }
int  watchdog_hw_armed(void) { return g_hw_armed; }
u32  watchdog_hw_timeout(void) { return g_hw_timeout_s; }

int watchdog_hw_gcs_noreboot(void) {
    if (!g_rcba) return -1;
    volatile u32 *gcs = (volatile u32*)(uintptr_t)(g_rcba + 0x3410);
    return (int)((*gcs >> 5) & 1);
}

int watchdog_hw_set_timeout(u32 seconds) {
    if (g_hw_kind == WD_HW_NONE) return -1;
    if (seconds < WD_HW_TIMEOUT_MIN || seconds > WD_HW_TIMEOUT_MAX) return -2;
    g_hw_timeout_s = seconds;
    if (g_hw_kind == WD_HW_TCO) {
        /* TCOv2_TMR at +0x12 (word write; QEMU honors the low 6 bits) */
        wd_outw(g_tco_base + 0x12, (u16)(seconds & 0x3F));
        wd_outw(g_tco_base + 0x00, 1);              /* reload */
    } else {
        const wdat_entry_t *e = wdat_find(4);       /* SET_COUNTDOWN */
        if (!e) return -3;
        wdat_exec(e, seconds);
    }
    return 0;
}

int watchdog_hw_start(u32 seconds) {
    if (g_hw_kind == WD_HW_NONE) return -1;
    int r = watchdog_hw_set_timeout(seconds);
    if (r != 0) return r;
    if (g_hw_kind == WD_HW_TCO) {
        /* Linux iTCO_wdt start order: clear GCS NO_REBOOT (arm the
         * hard reset), clear TIMEOUT/BOOT status, TMR_HLT=0, reload */
        tco_no_reboot(0);
        wd_outw(g_tco_base + 0x04, 0x0008);         /* TCO1_STS: TIMEOUT */
        wd_outw(g_tco_base + 0x06, 0x0002);         /* TCO2_STS: BOOT_STS */
        wd_outw(g_tco_base + 0x08, 0x0000);         /* TCO1_CNT: TMR_HLT=0 */
        wd_outw(g_tco_base + 0x00, 1);              /* reload NOW */
    } else {
        const wdat_entry_t *e;
        e = wdat_find(6);                           /* STOP first (idempotent) */
        if (e) wdat_exec(e, e->value);
        e = wdat_find(7);                           /* SET_REBOOT (OEM-provided
         * GCS/NO_REBOOT style bit; the TABLE carries the arming value) */
        if (e) wdat_exec(e, e->value);
        e = wdat_find(5);                           /* RUNNING = start: the table's
         * own value/mask is the semantics (e.g. clear TMR_HLT); passing
         * a literal 1 here once halted the dog instead - found live) */
        if (!e) return -3;
        wdat_exec(e, e->value);
    }
    g_hw_armed = 1;
    klog_write(KLOG_INFO, "watchdog", "hardware watchdog ARMED (%s, %u s)",
               watchdog_hw_name(), g_hw_timeout_s);
    return 0;
}

int watchdog_hw_feed(void) {
    if (!g_hw_armed) return -1;
    if (g_hw_kind == WD_HW_TCO) {
        wd_outw(g_tco_base + 0x00, 1);              /* TCO_RLD: any write */
    } else {
        /* no WDAT "ping" action in the table? SET_COUNTDOWN re-applies */
        const wdat_entry_t *e = wdat_find(4);
        if (!e) return -3;
        wdat_exec(e, g_hw_timeout_s);
    }
    return 0;
}

int watchdog_hw_stop(void) {
    if (g_hw_kind == WD_HW_NONE) return -1;
    if (g_hw_kind == WD_HW_TCO) {
        wd_outw(g_tco_base + 0x08, 0x0001);         /* TMR_HLT=1 */
        tco_no_reboot(1);                          /* disarm the reset */
    } else {
        const wdat_entry_t *e = wdat_find(6);       /* STOP: table's own value */
        if (!e) return -3;
        wdat_exec(e, e->value);
    }
    g_hw_armed = 0;
    klog_write(KLOG_INFO, "watchdog", "hardware watchdog stopped");
    return 0;
}

/* ================================================================== *
 * software slots
 * ================================================================== */
static watchdog_info_t g_slots[WD_MAX_SLOTS];
static int g_slots_used = 0;                        /* highest+1        */

int watchdog_register(const char *name, u32 timeout_ms, int policy) {
    if (!name || !*name || timeout_ms == 0 ||
        policy < WD_POLICY_PANIC || policy > WD_POLICY_LOG) return -2;
    if (g_slots_used >= WD_MAX_SLOTS) return -1;
    int id = g_slots_used;
    watchdog_info_t *s = &g_slots[id];
    memset(s, 0, sizeof(*s));
    strncpy(s->name, name, sizeof(s->name) - 1);
    s->timeout_ms = timeout_ms;
    s->policy = policy;
    s->running = 0;
    s->last_feed_ms = core_timer_now_ms();
    s->tid = (int)core_kthread_current_tid();
    g_slots_used++;
    klog_write(KLOG_INFO, "watchdog", "slot %d registered: %s %u ms policy %d",
               id, name, timeout_ms, policy);
    return id;
}

int watchdog_start(int id) {
    if (id < 0 || id >= g_slots_used) return -1;
    watchdog_info_t *s = &g_slots[id];
    s->last_feed_ms = core_timer_now_ms();
    s->running = 1;
    klog_write(KLOG_INFO, "watchdog", "slot %d (%s) ARMED, %u ms",
               id, s->name, s->timeout_ms);
    return 0;
}

int watchdog_feed(int id) {
    if (id < 0 || id >= g_slots_used) return -1;
    watchdog_info_t *s = &g_slots[id];
    s->last_feed_ms = core_timer_now_ms();
    return 0;
}

int watchdog_stop(int id) {
    if (id < 0 || id >= g_slots_used) return -1;
    g_slots[id].running = 0;
    klog_write(KLOG_INFO, "watchdog", "slot %d (%s) stopped", id, g_slots[id].name);
    return 0;
}

int watchdog_set_timeout(int id, u32 timeout_ms) {
    if (id < 0 || id >= g_slots_used) return -1;
    if (timeout_ms == 0) return -2;
    g_slots[id].timeout_ms = timeout_ms;
    return 0;
}

int  watchdog_count(void) { return g_slots_used; }
int  watchdog_get_info(int id, watchdog_info_t *out) {
    if (!out || id < 0 || id >= g_slots_used) return -1;
    *out = g_slots[id];
    return 0;
}

/* ---- the 1 s tick (IRQ context) -------------------------------------- */
static void watchdog_tick(void *ctx) {
    (void)ctx;
    u64 now = core_timer_now_ms();
    for (int id = 0; id < g_slots_used; id++) {
        watchdog_info_t *s = &g_slots[id];
        if (!s->running) continue;
        u64 since = now - s->last_feed_ms;
        if (since <= (u64)s->timeout_ms) continue;

        s->timeouts++;
        char msg[96];
        strcpy(msg, "watchdog slot ");
        {
            char n[12];
            u64_to_str((u64)id, n); strcat(msg, n);
            strcat(msg, " ("); strcat(msg, s->name); strcat(msg, ")");
        }

        switch (s->policy) {
        case WD_POLICY_PANIC: {
            char reason[112];
            strcpy(reason, "watchdog timeout: ");
            {
                char n[12];
                u64_to_str(since / 1000ULL, n); strcat(reason, n);
                strcat(reason, "s without a feed (task=");
                strcat(reason, s->name);
                strcat(reason, ")");
            }
            strcat(msg, " -> PANIC");
            klog_write(KLOG_FATAL, "watchdog", "%s", msg);
            kernel_panic(reason);           /* never returns */
            for (;;) __asm__ volatile("hlt");
        }
        case WD_POLICY_REBOOT:
            strcat(msg, " -> REBOOT");
            klog_write(KLOG_ERROR, "watchdog", "%s", msg);
            core_power_reboot();            /* never returns */
            for (;;) __asm__ volatile("hlt");
        case WD_POLICY_KILL:
            strcat(msg, " -> KILL task");
            klog_write(KLOG_ERROR, "watchdog", "%s", msg);
            if (s->tid > 0) core_sched_task_exited((tid_t)s->tid);
            s->running = 0;                 /* episode over */
            break;
        case WD_POLICY_LOG:
        default:
            /* one ERROR per episode: disarm until the next feed */
            strcat(msg, " -> LOGGED (no action)");
            klog_write(KLOG_ERROR, "watchdog", "%s", msg);
            s->running = 0;
            break;
        }
    }
}

/* ================================================================== *
 * init
 * ================================================================== */
void watchdog_init(void) {
    g_slots_used = 0;
    memset(g_slots, 0, sizeof(g_slots));

    /* 1. hardware probe.  TCO runs FIRST: its side effects (enabling
     * the RCBA chip-config MMIO at config 0xF0) are needed even when
     * a WDAT table later wins the mode selection - the injected E2E
     * WDAT carries a SystemMemory GCS entry that only responds once
     * the rcrb MMIO is mapped (found live: WDAT armed but never
     * reset).  When the WDAT table is present it takes authority over
     * the register operations; the TCO probe's RCBA enablement stays.
     * On real OEM boxes the WDAT entries point wherever the vendor
     * put them, so the TCO probe failing there is harmless. */
    hw_probe_tco();
    hw_probe_wdat();
    if (g_hw_kind == WD_HW_NONE)
        klog_write(KLOG_INFO, "watchdog",
                   "no hardware watchdog (WDAT absent, no Intel TCO LPC)");

    /* 2. the 1 s software check */
    core_timer_register_periodic(watchdog_tick, NULL, 1000);

    /* 3. slot 0: the system heartbeat wrapper (WP-11 semantics: the
     * shell kicks on every line; NOT armed at boot - same documented
     * decision as crash_wdt; crashtest wd keeps driving the WP-11
     * path unchanged).  L1 users can watchdog_start(0) + feed it. */
    int sys = watchdog_register("system-heartbeat", 15000, WD_POLICY_PANIC);
    (void)sys;   /* 0 by construction */
}
