/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10-input_chipset (S3 resume reworked in
 * WP-10-chinese_support-fix2)
 * File: drivers/acpi/driver_acpi_power.c
 * Purpose: ACPI power management - sleep states S3/S4/S5, SCI + GPE
 *          dispatch, FACS waking vector + real-mode resume trampoline.
 *
 * S3 resume design (mainstream OSPM shape, QEMU+SeaBIOS tested):
 *   1. A leaf asm stub saves CR0/CR3/CR4 + callee-saved registers +
 *      RSP into the save area inside the low-memory trampoline page,
 *      then writes SLP_EN|SLP_TYP into PM1a_CNT. The CPU stops there.
 *   2. Firmware (SeaBIOS src/fw/resume.c s3_resume() -> farcall16big,
 *      and real BIOSes per the ACPI spec) wakes and jumps to the FACS
 *      waking vector in 16-bit BIG REAL MODE.  fix2 root cause: the
 *      previous trampoline was 32-bit flat code, which decoded as
 *      garbage under 16-bit delivery and crashed the machine on
 *      resume before the kernel ever ran again.
 *   3. arch_acpi_s3_resume.S (copied to a fixed free low-RAM page,
 *      0x8000) starts in 16-bit real mode, enters 32-bit protected
 *      mode with paging off, then re-arms long mode (CR4.PAE,
 *      EFER.LME|NXE, CR0.PG, lgdt of the LIVE kernel GDT), lidt of the
 *      LIVE kernel IDT, ltr of the TSS, reloads segments, restores the
 *      saved RSP + callee registers and returns into the caller
 *      exactly as if the port write returned.
 *      GDT/IDT/handler tables live in RAM and survive S3, so no
 *      rebuild is needed - afterwards the resume path re-arms the PIT
 *      (without wiping the soft-timer table), restores PCI command
 *      registers (the platform reset cleared bus mastering everywhere)
 *      and replays the active NIC's register programming.
 */
#include "driver_acpi_power.h"
#include "driver_acpi.h"
#include "driver_acpi_aml.h"
#include "arch_idt.h"
#include "arch_irq.h"
#include "arch_lapic.h"
#include "arch_multiboot2.h"
#include "core_timer.h"
#include "../pci/driver_pci.h"
#include "../nic/driver_nic.h"
#include "../../net/net_core.h"
#include "../display/screen_serial_in.h"
#include "lib_string.h"
#include "lib_log.h"

#include <stdint.h>

/* ---- port I/O ---- */
static inline void pw_outb(u16 p, u8 v)  { __asm__ volatile("outb %0, %1" :: "a"(v), "Nd"(p)); }
static inline u8   pw_inb(u16 p)         { u8 v;  __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(p)); return v; }
static inline void pw_outw(u16 p, u16 v) { __asm__ volatile("outw %0, %1" :: "a"(v), "Nd"(p)); }
static inline u16  pw_inw(u16 p)         { u16 v; __asm__ volatile("inw %1, %0" : "=a"(v) : "Nd"(p)); return v; }

/* ------------------------------------------------------------------ */
/* state                                                               */
/* ------------------------------------------------------------------ */

#define SLEEP_SLOTS 5   /* S1..S5 */

static int  g_slp_typ[SLEEP_SLOTS];   /* -1 = state not offered */
static int  g_sci_irq = -1;
static int  g_resumed;
static int  g_ready;
static u16  g_pm1a_cnt;
static u16  g_pm1a_evt;
static u16  g_gpe0_blk;
static u8   g_gpe0_len;
static u16  g_gpe1_blk;
static u8   g_gpe1_len;
static u8   g_gpe1_base;
static u8   g_gpe_snapshot[8];

u32 driver_acpi_power_sleep_mask(void) {
    u32 m = 0;
    for (int i = 0; i < SLEEP_SLOTS; i++)
        if (g_slp_typ[i] >= 0) m |= (1u << (i + 1));
    return m;
}

int driver_acpi_power_slp_typ(int state) {
    if (state < 1 || state > SLEEP_SLOTS) return -1;
    return g_slp_typ[state - 1];
}

int driver_acpi_power_sci_irq(void) { return g_sci_irq; }

int driver_acpi_power_resumed(void) { return g_resumed; }

void driver_acpi_power_gpe_status(u8 *out, int cap) {
    if (!out) return;
    int n = (g_gpe0_len / 2) ? (g_gpe0_len / 2) : 0;
    for (int i = 0; i < cap; i++) out[i] = (i < n) ? g_gpe_snapshot[i] : 0;
}

/* ------------------------------------------------------------------ */
/* init                                                                */
/* ------------------------------------------------------------------ */

int driver_acpi_power_init(void) {
    const acpi_fadt_t *f = driver_acpi_fadt();
    if (!f) return -1;

    for (int i = 0; i < SLEEP_SLOTS; i++) g_slp_typ[i] = -1;

    /* Resolve \_S1..\_S5 packages (SLP_TYPa element 0). */
    static const char *names[SLEEP_SLOTS] =
        { "\\_S1", "\\_S2", "\\_S3", "\\_S4", "\\_S5" };
    for (int i = 0; i < SLEEP_SLOTS; i++) {
        int cnt = 0;
        if (driver_acpi_aml_package_elements(names[i], &cnt) == 0 && cnt >= 1) {
            u64 v = 0;
            if (driver_acpi_aml_eval_package_integer(names[i], 0, &v) == 0)
                g_slp_typ[i] = (int)(v & 0x7F);
        }
    }

    /* PM register blocks (X_ fields gated on ACPI 2.0+ layout; see
     * driver_acpi_fadt_is_extended). */
    int ext = driver_acpi_fadt_is_extended();
    g_pm1a_cnt = (u16)((ext && f->x_pm1a_cnt.address)
                       ? (f->x_pm1a_cnt.address & 0xFFFF) : f->pm1a_cnt_blk);
    g_pm1a_evt = (u16)((ext && f->x_pm1a_evt.address)
                       ? (f->x_pm1a_evt.address & 0xFFFF) : f->pm1a_evt_blk);
    g_gpe0_blk = (u16)((ext && f->x_gpe0_blk.address)
                       ? (f->x_gpe0_blk.address & 0xFFFF) : f->gpe0_blk);
    g_gpe0_len = f->gpe0_blk_len;
    g_gpe1_blk = (u16)(f->gpe1_blk);
    g_gpe1_len = f->gpe1_blk_len;
    g_gpe1_base = f->gpe1_base;
    g_sci_irq  = f->sci_int;

    /* Enter ACPI mode when the firmware still expects the SMI switch
     * (mainstream OSPM behaviour; QEMU firmware is already in ACPI). */
    if (f->smi_cmd && f->acpi_enable) {
        pw_outb((u16)f->smi_cmd, f->acpi_enable);
        for (volatile int i = 0; i < 100000; i++) { }
    }

    /* SCI handler on the PIC line the FADT names (IRQ9 in practice). */
    if (g_sci_irq >= 0 && g_sci_irq < 16) {
        arch_irq_register_handler(g_sci_irq, driver_acpi_sci_handler, NULL);
    }

    /* Power button: enable the fixed PM1 event so a QEMU/ACPI power
     * button press raises the SCI. */
    if (g_pm1a_evt) {
        u16 en = pw_inw((u16)(g_pm1a_evt + 2));
        en |= (1u << 8);   /* PWRBTN_EN */
        pw_outw((u16)(g_pm1a_evt + 2), en);
    }

    g_ready = 1;
    {
        char line[96]; char n[16];
        strcpy(line, "ACPI power: SCI IRQ ");
        u64_to_str((u64)(g_sci_irq < 0 ? 0 : g_sci_irq), n); strcat(line, n);
        strcat(line, " PM1a_CNT 0x");
        u64_to_hex(g_pm1a_cnt, n, 4); strcat(line, n);
        strcat(line, " sleep mask 0x");
        u64_to_hex(driver_acpi_power_sleep_mask(), n, 2); strcat(line, n);
        OC_LOG_OK(line);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* SCI + GPE dispatch                                                  */
/* ------------------------------------------------------------------ */

static u32 g_sci_count;
static u32 g_gpe_count;
static u32 g_pwrbtn_count;

void driver_acpi_sci_handler(void *ctx, arch_irq_frame_t *f) {
    (void)ctx; (void)f;
    g_sci_count++;

    /* Fixed events in PM1a_EVT status (half of pm1_evt_len). */
    if (g_pm1a_evt) {
        u16 sts = pw_inw(g_pm1a_evt);
        if (sts & (1u << 8)) {           /* PWRBTN_STS */
            g_pwrbtn_count++;
            pw_outw(g_pm1a_evt, 1u << 8);        /* RW1C */
            driver_acpi_aml_notify_power_button();
        }
        if (sts & (1u << 15)) {          /* WAK_STS */
            pw_outw(g_pm1a_evt, 1u << 15);
        }
    }

    /* GPE0: status half at +0, enable half at +len/2. */
    if (g_gpe0_blk && g_gpe0_len >= 2) {
        u8 half = g_gpe0_len / 2;
        for (u8 b = 0; b < half && b < 8; b++) {
            u8 sts = pw_inb((u16)(g_gpe0_blk + b));
            if (!sts) continue;
            g_gpe_snapshot[b] = sts;
            for (u8 bit = 0; bit < 8; bit++) {
                if (!(sts & (1u << bit))) continue;
                u8 gpe = (u8)(b * 8 + bit);
                g_gpe_count++;
                /* dispatch: level method first, then edge method */
                if (driver_acpi_aml_gpe_method_exists(gpe)) {
                    if (driver_acpi_aml_run_gpe_method(gpe, 1) != 0)
                        (void)driver_acpi_aml_run_gpe_method(gpe, 0);
                }
                pw_outb((u16)(g_gpe0_blk + b), (u8)(1u << bit));   /* RW1C */
            }
        }
    }
}

u32 driver_acpi_power_sci_count(void)     { return g_sci_count; }
u32 driver_acpi_power_gpe_count(void)     { return g_gpe_count; }
u32 driver_acpi_power_pwrbtn_count(void)  { return g_pwrbtn_count; }

/* ------------------------------------------------------------------ */
/* S3 resume trampoline (real-mode head + long-mode re-entry)          */
/* ------------------------------------------------------------------ */

/* The trampoline code itself lives in arch_acpi_s3_resume.S.  It is
 * POSITION-FIXED: the firmware jumps to physical address 0x8000, a
 * free low-RAM page between the BDA (0x400-0x4FF) and the EBDA
 * (0x9FC00 on QEMU; GRUB's 0x500-0x7BFF scratch and 0x7C00 boot slot
 * are dead after boot).  All save-area offsets below match the .S:
 *   0x000..0x0FF  trampoline code (16 -> 32 -> 64-bit heads)
 *   0x200         u64 saved RSP (the fire stub saves the full RSP)
 *   0x208         u32 saved CR3
 *   0x20C         u32 saved CR4
 *   0x210         u32 saved CR0
 *   0x214         GDT descriptor: u16 limit + u32 base (10 bytes)
 *   0x21C         IDT descriptor: u16 limit + u32 base (10 bytes)
 *   0x228         u32 "resumed" magic (written by the trampoline)
 */
#define OC_S3_TRAMP_BASE   0x8000u
#define TRAMP_OFF_ESP      0x200   /* u64 */
#define TRAMP_OFF_CR3      0x208
#define TRAMP_OFF_CR4      0x20C
#define TRAMP_OFF_CR0      0x210
#define TRAMP_OFF_GDT      0x214
#define TRAMP_OFF_IDT      0x21C
#define TRAMP_OFF_RESUMED  0x228
#define TRAMP_RESUME_MAGIC 0x52334B4Fu   /* "R3KO" */

extern const u8 acpi_s3_resume_tramp_start[];
extern const u8 acpi_s3_resume_tramp_end[];

/* Is the fixed low page actually usable RAM according to the
 * multiboot2 memory map?  Non-QEMU machines can grow the EBDA down or
 * reserve 0x8000 for firmware tables - refuse S3 honestly there. */
static int s3_low_page_is_ram(void) {
    const arch_multiboot2_mmap_tag_t* mmap = arch_multiboot2_mmap();
    if (!mmap) return 0;
    u32 entry_size = mmap->entry_size;
    if (entry_size < sizeof(arch_multiboot2_mmap_entry_t)) return 0;
    u32 tag_payload = (mmap->size >= sizeof(*mmap)) ? (mmap->size - sizeof(*mmap)) : 0;
    u32 count = (entry_size != 0) ? (tag_payload / entry_size) : 0;
    const u8* p = (const u8*)mmap + sizeof(*mmap);
    for (u32 i = 0; i < count; ++i, p += entry_size) {
        const arch_multiboot2_mmap_entry_t* e = (const arch_multiboot2_mmap_entry_t*)p;
        /* entry type 1 = available RAM */
        if (e->type != 1) continue;
        u64 lo = e->addr;
        u64 hi = e->addr + e->len;
        if (lo <= OC_S3_TRAMP_BASE && hi >= (u64)OC_S3_TRAMP_BASE + 0x1000u)
            return 1;
    }
    return 0;
}

/* asm leaf that saves state, fires the sleep write, and (if the write
 * returns because sleep failed) unwinds normally.  Resume lands in the
 * low-memory trampoline which restores exactly this frame. */
extern int acpi_s3_fire_stub(u16 port, u16 value, u8 *save_area);

/* ------------------------------------------------------------------ */
/* sleep entry                                                         */
/* ------------------------------------------------------------------ */

static void acpi_prepare_sleep(int state) {
    /* run \_PTS (prepare to sleep) when present */
    driver_acpi_aml_eval_integer("\\_PTS", NULL);
    (void)state;
}

static int acpi_write_sleep(int state) {
    int typ = driver_acpi_power_slp_typ(state);
    if (typ < 0 || !g_pm1a_cnt) return -1;
    /* interrupts off BEFORE the flush + enabling write: the asm stub
     * documents that contract and a timer IRQ between wbinvd and the
     * SLP write would enter the scheduler with the machine about to
     * vanish (fix2: the cli used to be missing here). */
    __asm__ volatile("cli" ::: "memory");
    __asm__ volatile("wbinvd" ::: "memory");
    u16 val = (u16)(((u32)typ << 10) | (1u << 13));
    return acpi_s3_fire_stub(g_pm1a_cnt, val, (u8*)(uintptr_t)OC_S3_TRAMP_BASE);
}

int driver_acpi_power_enter_s5(void) {
    if (!g_ready) return -1;
    acpi_prepare_sleep(5);
    /* S5 is terminal: use the plain port write (no resume machinery). */
    int typ = driver_acpi_power_slp_typ(5);
    if (typ < 0 || !g_pm1a_cnt) return -1;
    __asm__ volatile("wbinvd" ::: "memory");
    __asm__ volatile("cli");
    pw_outw(g_pm1a_cnt, (u16)(((u32)typ << 10) | (1u << 13)));
    /* still here: machine did not power off */
    __asm__ volatile("sti");
    return -1;
}

int driver_acpi_power_enter_s4(void) {
    if (!g_ready) return -1;
    /* S4 (hibernate sleep type). QEMU/SeaBIOS treats the S4 write like a
     * power cut - a real hibernate image writer is L1 territory and is
     * reported honestly instead of faked. The sleep-type plumbing is
     * identical to S3 minus the waking vector. */
    acpi_prepare_sleep(4);
    int typ = driver_acpi_power_slp_typ(4);
    if (typ < 0 || !g_pm1a_cnt) return -1;
    __asm__ volatile("wbinvd" ::: "memory");
    __asm__ volatile("cli");
    pw_outw(g_pm1a_cnt, (u16)(((u32)typ << 10) | (1u << 13)));
    __asm__ volatile("sti");
    return -1;
}

int driver_acpi_power_enter_s3(void) {
    if (!g_ready) return -1;
    if (driver_acpi_power_slp_typ(3) < 0) return -1;
    u64 facs_phys = driver_acpi_facs_phys();
    if (!facs_phys) return -1;
    acpi_facs_t *facs = (acpi_facs_t*)(uintptr_t)facs_phys;
    if (!s3_low_page_is_ram()) return -1;

    u8 *tramp = (u8*)(uintptr_t)OC_S3_TRAMP_BASE;
    const u32 code_len = (u32)(acpi_s3_resume_tramp_end - acpi_s3_resume_tramp_start);
    if (code_len > 0x100) return -1;   /* code must never reach the save area */

    /* stage the position-fixed trampoline + clear its save area */
    memcpy(tramp, acpi_s3_resume_tramp_start, code_len);
    memset(tramp + 0x100, 0, 0x1000u - 0x100);

    /* arm the waking vector (both forms; firmware picks what it knows) */
    facs->firmware_waking_vector = OC_S3_TRAMP_BASE;
    facs->x_firmware_waking_vector = OC_S3_TRAMP_BASE;

    /* pre-fill the save area; acpi_s3_fire_stub re-saves CR0/3/4 + RSP
     * at fire time from the exact frame that goes to sleep */
    {
        u64 cr3, cr4, cr0;
        __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
        __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
        __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
        *(u32*)(tramp + TRAMP_OFF_CR3) = (u32)cr3;
        *(u32*)(tramp + TRAMP_OFF_CR4) = (u32)cr4;
        *(u32*)(tramp + TRAMP_OFF_CR0) = (u32)cr0;
        arch_gdt_descriptor(tramp + TRAMP_OFF_GDT);
        arch_idt_descriptor(tramp + TRAMP_OFF_IDT);
        *(u32*)(tramp + TRAMP_OFF_RESUMED) = 0;
    }

    acpi_prepare_sleep(3);

    int rc = acpi_write_sleep(3);

    /* Return path B: RESUMED. The trampoline jumped back here with the
     * register state of the pre-sleep call. Re-arm what the firmware
     * reset, clear the waking vector, and report. (Checked BEFORE the
     * error code: on the resume path eax is firmware garbage.) */
    if (*(volatile u32*)(tramp + TRAMP_OFF_RESUMED) == TRAMP_RESUME_MAGIC) {
        *(volatile u32*)(tramp + TRAMP_OFF_RESUMED) = 0;
        g_resumed = 1;
        /* PIC was reset by the firmware */
        arch_pic_remap();
        arch_irq_rearm();
        /* PIT was reset with the platform. fix2: reprogram it WITHOUT
         * core_timer_init() - that memset wiped the whole soft-timer
         * table, silently killing the 10 ms network poll timer (and
         * every other registered timer) after every resume. */
        core_timer_resume();
        /* LAPIC re-enable when present */
        arch_lapic_init();
        /* fix2: the platform reset cleared every PCI command register
         * (bus mastering + MMIO/IO decode gone). Re-enable the devices
         * the kernel drives, then replay the active NIC's register
         * programming - otherwise the resumed system is half-dead:
         * shell alive but disk/NIC/USB silent. */
        driver_pci_restore_commands();
        driver_nic_resume();
        net_e1000_resume();
        /* the platform reset cleared the UART's RX interrupt enable, so
         * serial input died with the sleep - reprogram the COM1 RX path
         * (the IRQ4 handler slot is RAM-resident and still live). */
        screen_serial_in_init();
        /* clear the waking vector so a warm reboot does not resume */
        facs->firmware_waking_vector = 0;
        facs->x_firmware_waking_vector = 0;
        /* re-enable the power button + GPEs */
        if (g_pm1a_evt) {
            u16 en = pw_inw((u16)(g_pm1a_evt + 2));
            en |= (1u << 8);
            pw_outw((u16)(g_pm1a_evt + 2), en);
        }
        __asm__ volatile("sti");
        return 0;
    }

    /* Return path A: sleep failed (no reset happened). */
    __asm__ volatile("sti");
    return rc != 0 ? rc : -1;
}
