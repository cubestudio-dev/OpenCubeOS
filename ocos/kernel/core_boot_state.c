/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP07a-ocbl_state
 * File: src/L0/boot/boot_state.c
 * Purpose: the OCOS boot-state monitor - see boot_state.h.
 *
 *          Everything here is dependency-free on purpose: the stage
 *          writes must work from the very first kmain line (raw COM1
 *          serial + plain fixed-address stores + raw PCI config
 *          cycles for the 6300ESB).  The RTC progress clock comes
 *          from the CMOS reader in services/time (pure port I/O,
 *          safe before any driver exists); the runtime heartbeat
 *          rides the core_timer periodic callback table.
 *
 *          WRITE-ONLY DISCIPLINE: this file never reads the state
 *          area back.  The kernel-local mirror below is the only
 *          stage knowledge the kernel has.
 */
#include "core_boot_state.h"
#include "ocbl_state.h"        /* the shared contract (include/) */
#include "svc_time_rtc.h"          /* core_rtc_unix_now() - CMOS wall clock */
#include "core_timer.h"        /* core_timer_register_periodic() */

/* ---- raw COM1 serial (same bring-up as kmain's early console) ---- */
static void bs_outb(u16 port, u8 v)
{
    __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(port));
}
static u8 bs_inb(u16 port)
{
    u8 v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
#define BS_COM1 0x3F8
static void bs_puts(const char *s)
{
    while (*s) {
        while ((bs_inb(BS_COM1 + 5) & 0x20) == 0)
            ;
        bs_outb(BS_COM1, (u8)*s++);
    }
}
static void bs_putdec(u32 v)
{
    char b[12];
    u32 i = 0;
    do {
        b[i++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (i)
        bs_outb(BS_COM1, (u8)b[--i]);
}

/* ---- the 6300ESB watchdog (raw PCI cycles, mirroring the OCBL and
 * OCIRE drivers: config 0x60 latches on 16-bit writes, the lock
 * register 0x68 only on 8-bit writes, RELOAD needs the 0x80/0x86
 * unlock dance) ------------------------------------------------------ */
#define WDT_PCI_ADDR 0xCF8u
#define WDT_PCI_DATA 0xCFCu

static void wdt_pci_addr(u8 bus, u8 dev, u8 fn, u8 off)
{
    u32 a = 0x80000000u | ((u32)bus << 16) | ((u32)dev << 11) |
            ((u32)fn << 8) | (off & 0xFCu);
    __asm__ volatile("outl %0, %1" : : "a"(a), "dN"((u16)WDT_PCI_ADDR));
}
static u32 wdt_pci_read32(u8 bus, u8 dev, u8 fn, u8 off)
{
    u32 v;
    wdt_pci_addr(bus, dev, fn, off);
    __asm__ volatile("inl %1, %0" : "=a"(v) : "dN"((u16)WDT_PCI_DATA));
    return v;
}
static void wdt_pci_write16(u8 bus, u8 dev, u8 fn, u8 off, u16 val)
{
    wdt_pci_addr(bus, dev, fn, off);
    __asm__ volatile("outw %0, %1" : : "a"(val),
                     "dN"((u16)(WDT_PCI_DATA + (off & 3))));
}
static void wdt_pci_write8(u8 bus, u8 dev, u8 fn, u8 off, u8 val)
{
    wdt_pci_addr(bus, dev, fn, off);
    __asm__ volatile("outb %0, %1" : : "a"(val),
                     "dN"((u16)(WDT_PCI_DATA + (off & 3))));
}

static u8  g_wdt_bus, g_wdt_dev, g_wdt_fn;
static u16 g_wdt_io;           /* BAR0 I/O base (bit0 = 1) */
static u32 g_wdt_mem;          /* BAR0 MMIO base otherwise   */
static int g_wdt_found;

static void wdt_outw(u8 off, u16 val)
{
    if (g_wdt_io) {
        __asm__ volatile("outw %0, %1" : : "a"(val),
                         "dN"((u16)(g_wdt_io + off)));
    } else {
        *(volatile u16 *)(uintptr_t)(g_wdt_mem + off) = val;
    }
}
static void wdt_outl(u8 off, u32 val)
{
    if (g_wdt_io) {
        __asm__ volatile("outl %0, %1" : : "a"(val),
                         "dN"((u16)(g_wdt_io + off)));
    } else {
        *(volatile u32 *)(uintptr_t)(g_wdt_mem + off) = val;
    }
}
static void wdt_unlock(void)
{
    wdt_outw(0x0C, 0x80);
    wdt_outw(0x0C, 0x86);
}

/* probe the PCI bus for 8086:25AB and resolve BAR0 */
static void wdt_probe(void)
{
    g_wdt_found = 0;
    for (u16 bus = 0; bus < 2 && !g_wdt_found; bus++) {
        for (u8 dev = 0; dev < 32 && !g_wdt_found; dev++) {
            for (u8 fn = 0; fn < 8; fn++) {
                u32 id = wdt_pci_read32((u8)bus, dev, fn, 0);
                if (id == 0xFFFFFFFFu)
                    continue;
                if ((id & 0xFFFFu) != 0x8086u ||
                    ((id >> 16) & 0xFFFFu) != 0x25ABu)
                    continue;
                u32 bar = wdt_pci_read32((u8)bus, dev, fn, 0x10);
                if (!bar)
                    continue;
                g_wdt_bus = (u8)bus;
                g_wdt_dev = dev;
                g_wdt_fn = fn;
                if (bar & 1u) {
                    g_wdt_io = (u16)(bar & ~0x3u);
                    g_wdt_mem = 0;
                } else {
                    g_wdt_io = 0;
                    g_wdt_mem = bar & ~0xFu;
                }
                g_wdt_found = 1;
                break;
            }
        }
    }
}

/* arm both stages with `seconds` (1..2046; each stage counts
 * seconds<<9 at 1 kHz, so the pair spans ~seconds) */
static void wdt_arm(u32 seconds)
{
    if (!g_wdt_found || seconds < 1 || seconds > 2046)
        return;
    u32 t = seconds << 9;
    wdt_pci_write16(g_wdt_bus, g_wdt_dev, g_wdt_fn, 0x60, 0x0000);
    wdt_unlock();
    wdt_outl(0x00, t);                    /* TIMER1 */
    wdt_unlock();
    wdt_outl(0x04, t);                    /* TIMER2 */
    wdt_unlock();
    wdt_outw(0x0C, 0x100);                /* RELOAD now */
    wdt_pci_write8(g_wdt_bus, g_wdt_dev, g_wdt_fn, 0x68, 0x02);
}

static void wdt_pet(void)
{
    if (!g_wdt_found)
        return;
    wdt_unlock();
    wdt_outw(0x0C, 0x100);
}

static void wdt_disarm(void)
{
    if (!g_wdt_found)
        return;
    wdt_unlock();
    wdt_outw(0x0C, 0x100);
    wdt_pci_write8(g_wdt_bus, g_wdt_dev, g_wdt_fn, 0x68, 0x00);
}

/* ---- the kernel-local monitor state (never read back from the
 * shared area - the write-only contract) --------------------------- */
static u32 g_stage;             /* the mirror of the last written stage */
static int g_enabled = 1;       /* oc.stmon=off clears this            */
static u32 g_hang_stage;        /* the /HANGAT halt point, 0 = none    */
static int g_l1_pending = 1;    /* until the first default-entry syscall */
static u64 g_pet_epoch;         /* unix seconds at L1_READY            */
static u32 g_pet_ticks;         /* 1 Hz heartbeats since L1_READY      */

/* the state area (identity-mapped supervisor pages at a fixed
 * physical address; the kernel writes, never reads) */
static volatile ocst_state_t* const g_state =
    (volatile ocst_state_t *)OCST_STATE_ADDR;

/* ---- early init: cmdline facts + the watchdog takeover ------------ */
void boot_state_early_init(const char *cmdline)
{
    g_hang_stage = 0;
    g_enabled = 1;

    /* parse the oc.* monitor facts (same style as ocembed's cmdline
     * scan; a NULL cmdline - GRUB-era boots - simply means "none") */
    if (cmdline) {
        const char *p = cmdline;
        while (*p) {
            if (p[0] == ' ' || p[0] == '\t') {
                p++;
                continue;
            }
            if (p[0] == 'o' && p[1] == 'c' && p[2] == '.') {
                if (p[3] == 'h' && p[4] == 'a' && p[5] == 'n' &&
                    p[6] == 'g' && p[7] == '=' && p[8] >= '1' &&
                    p[8] <= '7') {
                    g_hang_stage = (u32)(p[8] - '0');
                } else if (p[3] == 's' && p[4] == 't' && p[5] == 'm' &&
                           p[6] == 'o' && p[7] == 'n' && p[8] == '=' &&
                           p[9] == 'o' && p[10] == 'f' && p[11] == 'f') {
                    g_enabled = 0;
                }
            }
            while (*p && *p != ' ')
                p++;
        }
    }

    if (!g_enabled) {
        bs_puts("[ocst] state monitor DISABLED (oc.stmon=off) - no "
                "low-memory writes, no watchdog relay\r\n");
        return;
    }

    wdt_probe();
    bs_puts("[ocst] boot-state monitor: state area 0x98000, watchdog ");
    if (g_wdt_found) {
        bs_puts("6300ESB (relay active)\r\n");
    } else {
        bs_puts("absent - stage recording only, no hardware reset "
                "enforcement\r\n");
    }
    if (g_hang_stage) {
        bs_puts("[ocst] /HANGAT diagnostic: will halt after stage ");
        bs_putdec(g_hang_stage);
        bs_puts("\r\n");
    }
}

/* ---- the stage advance -------------------------------------------- */
void boot_state_advance(u32 stage)
{
    if (!g_enabled || stage <= g_stage || stage >= OCST_STAGE_COUNT)
        return;
    g_stage = stage;

    /* blind writes only - the mirror above is the kernel's knowledge */
    g_state->stage = (ocst_u8)stage;
    g_state->last_progress = (ocst_u32)core_rtc_unix_now();
    wdt_arm(ocst_stage_budget((ocst_u8)stage));

    bs_puts("[ocst] stage ");
    bs_putdec(stage);
    bs_puts("/6: ");
    bs_puts(ocst_stage_name((ocst_u8)stage));
    bs_puts(" (budget ");
    bs_putdec(ocst_stage_budget((ocst_u8)stage));
    bs_puts("s)\r\n");

    /* the /HANGAT diagnostic halt: AFTER the stage write and the
     * phase arm, so the watchdog relay runs the REAL failure chain */
    if (g_hang_stage == stage) {
        bs_puts("[ocst] HANGAT: deliberate halt with interrupts off "
                "- the watchdog relay must catch this\r\n");
        __asm__ volatile("cli");
        for (;;)
            __asm__ volatile("hlt");
    }
}

/* ---- the runtime heartbeat (1 Hz, IRQ context) --------------------- */
static void bs_pet_tick(void *ctx)
{
    (void)ctx;
    if (!g_enabled || g_stage < OCST_STAGE_L1_READY)
        return;
    g_pet_ticks++;
    g_state->heartbeat = (ocst_u32)g_pet_ticks;
    /* the epoch + tick arithmetic keeps CMOS reads out of IRQ
     * context; drift against the RTC is < 1 s per boot session */
    g_state->last_progress = (ocst_u32)(g_pet_epoch + g_pet_ticks);
    wdt_pet();
}

/* ---- the L1 isolation self-check ------------------------------------
 * Walks the LIVE user page tables (the syscall runs on the caller's
 * CR3) and asserts no user-accessible leaf covers either monitor
 * page - the hardware proof that ring-3 cannot touch the area. */
static int bs_walk_user_leaf(u64 addr, u64 *leaf_out)
{
    u64 cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    cr3 &= ~0xFFFULL;
    u64 pml4e = *(u64 *)(uintptr_t)(cr3 + 8 * ((addr >> 39) & 0x1FF));
    if (!(pml4e & 1))
        return 0;
    u64 pdpte = *(u64 *)(uintptr_t)((pml4e & ~0xFFFULL) +
                                    8 * ((addr >> 30) & 0x1FF));
    if (!(pdpte & 1))
        return 0;
    u64 pdte = *(u64 *)(uintptr_t)((pdpte & ~0xFFFULL) +
                                   8 * ((addr >> 21) & 0x1FF));
    if (!(pdte & 1))
        return 0;
    if (pdte & 0x80) {                 /* 2 MiB huge leaf */
        *leaf_out = pdte;
        return 1;
    }
    u64 pte = *(u64 *)(uintptr_t)((pdte & ~0xFFFULL) +
                                  8 * ((addr >> 12) & 0x1FF));
    if (!(pte & 1))
        return 0;
    *leaf_out = pte;
    return 1;
}

static void bs_check_isolation(void)
{
    u64 addr;
    for (addr = OCST_STATE_ADDR;
         addr < OCST_RESERVED_ADDR + OCST_RESERVED_SIZE; addr += 0x1000) {
        u64 leaf;
        if (bs_walk_user_leaf(addr, &leaf) && (leaf & 0x4)) {
            bs_puts("[ocst] L1 ISOLATION FAILURE: a user page covers "
                    "0x");
            {
                char h[17];
                for (int i = 0; i < 16; i++)
                    h[i] = "0123456789abcdef"
                        [(addr >> ((15 - i) * 4)) & 0xF];
                h[16] = 0;
                bs_puts(h);
            }
            bs_puts("\r\n");
            return;
        }
    }
    bs_puts("[ocst] L1 isolation verified: no user mapping covers the "
            "state pages (live CR3 walk)\r\n");
}

/* ---- the L1_READY transition ---------------------------------------- */
int boot_state_l1_pending(void)
{
    return g_l1_pending;
}

void boot_state_l1_syscall(void)
{
    if (!g_enabled || !g_l1_pending)
        return;
    g_l1_pending = 0;

    boot_state_advance(OCST_STAGE_L1_READY);

    /* the runtime heartbeat: 1 Hz pets on the 15 s runtime budget */
    g_pet_epoch = core_rtc_unix_now();
    g_pet_ticks = 0;
    (void)core_timer_register_periodic(bs_pet_tick, NULL, 1000);

    bs_check_isolation();

    /* the /HANGAT runtime scenario: heartbeat armed, then the hard
     * hang (interrupts off) - the pets stop, the relay fires at 15 s */
    if (g_hang_stage == 7) {
        bs_puts("[ocst] HANGAT runtime: deliberate halt with "
                "interrupts off - the watchdog relay must catch this\r\n");
        __asm__ volatile("cli");
        for (;;)
            __asm__ volatile("hlt");
    }
}

/* ---- the clean-reboot marker ----------------------------------------- */
void boot_state_clean_reboot(void)
{
    if (!g_enabled)
        return;
    /* plain store, no read-modify-write: the kernel owns kflags
     * entirely (OCBL zeroed it at arm time) - the write-only
     * contract stays literally true */
    g_state->kflags = OCST_KFLAG_CLEAN;
    g_state->last_progress = (ocst_u32)core_rtc_unix_now();
    wdt_disarm();
    bs_puts("[ocst] clean-reboot marker recorded\r\n");
}
