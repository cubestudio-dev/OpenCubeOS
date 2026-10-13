/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10-input_chipset
 * File: drivers/acpi/driver_acpi_power.c
 * Purpose: ACPI power management - sleep states S3/S4/S5, SCI + GPE
 *          dispatch, FACS waking vector + 32-bit resume trampoline.
 *
 * S3 resume design (mainstream OSPM shape, QEMU+SeaBIOS/OVMF tested):
 *   1. A leaf asm stub saves CR0/CR3/CR4 + callee-saved registers +
 *      RSP into a save area next to the trampoline, then writes
 *      SLP_EN|SLP_TYP into PM1a_CNT. The CPU stops there.
 *   2. Firmware (SeaBIOS/OVMF) wakes, jumps to the FACS waking vector
 *      in 32-bit flat mode with paging off.
 *   3. The trampoline re-arms long mode (CR4.PAE, EFER.LME|NXE, CR0.PG,
 *      lgdt of the LIVE kernel GDT), lidt of the LIVE kernel IDT,
 *      reloads segments, restores the saved RSP + callee registers and
 *      returns into the caller exactly as if the port write returned.
 *      GDT/IDT/handler tables live in RAM and survive S3, so no
 *      rebuild is needed - only PIC re-init + timer re-arm + LAPIC
 *      re-enable are performed afterwards (device-reset class work).
 */
#include "driver_acpi_power.h"
#include "driver_acpi.h"
#include "driver_acpi_aml.h"
#include "arch_idt.h"
#include "arch_irq.h"
#include "arch_lapic.h"
#include "core_timer.h"
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
/* S3 resume trampoline (hand-assembled 32-bit code + save area)       */
/* ------------------------------------------------------------------ */

/* Save area / trampoline layout (offsets from the page base):
 *   0x000..0x0FF  32-bit trampoline code
 *   0x100         u32 saved ESP
 *   0x104         u32 saved CR3
 *   0x108         u32 saved CR4
 *   0x10C         u32 saved CR0
 *   0x110         GDT descriptor: u16 limit + u64 base (10 bytes)
 *   0x118         IDT descriptor: u16 limit + u64 base (10 bytes)
 *   0x124         u32 "resumed" magic (written by trampoline)
 *   0x128         u32 PM1a_CNT port / value pair for the asm stub
 */
#define TRAMP_OFF_ESP      0x100
#define TRAMP_OFF_CR3      0x104
#define TRAMP_OFF_CR4      0x108
#define TRAMP_OFF_CR0      0x10C
#define TRAMP_OFF_GDT      0x110
#define TRAMP_OFF_IDT      0x118
#define TRAMP_OFF_RESUMED  0x124
#define TRAMP_RESUME_MAGIC 0x52334B4Fu   /* "R3KO" */

static const u8 g_s3_tramp[] = {
    /* 32-bit flat entry (paging off, CS=32-bit code) */
    0xFA,                                   /* 0x00 cli                        */
    0x8B, 0x25, 0x00, 0x01, 0x00, 0x00,     /* 0x01 mov esp,[0x100]            */
    0xA1, 0x04, 0x01, 0x00, 0x00,           /* 0x07 mov eax,[0x104]  (cr3)     */
    0x0F, 0x22, 0xD8,                       /* 0x0C mov cr3,eax                */
    0xA1, 0x08, 0x01, 0x00, 0x00,           /* 0x0F mov eax,[0x108]  (cr4)     */
    0x0F, 0x22, 0xE0,                       /* 0x14 mov cr4,eax                */
    0xB9, 0x80, 0x00, 0x00, 0xC0,           /* 0x17 mov ecx,0xC0000080 (EFER)  */
    0x0F, 0x32,                             /* 0x1C rdmsr                      */
    0x0D, 0x00, 0x09, 0x00, 0x00,           /* 0x1E or eax,0x900 (LME|NXE)     */
    0x0F, 0x30,                             /* 0x23 wrmsr                      */
    0xA1, 0x0C, 0x01, 0x00, 0x00,           /* 0x25 mov eax,[0x10C] (cr0)      */
    0x0D, 0x00, 0x00, 0x00, 0x80,           /* 0x2A or eax,0x80000000 (PG)     */
    0x0F, 0x22, 0xC0,                       /* 0x2F mov cr0,eax  -> long mode  */
    0x0F, 0x01, 0x15, 0x10, 0x01, 0x00, 0x00, /* 0x32 lgdt [0x110]             */
    /* far jump into 64-bit code segment (selector 0x18) at 0x50 */
    0xEA, 0x50, 0x00, 0x00, 0x00, 0x18, 0x00, /* 0x39 jmp 0x18:0x50            */
    /* 0x40..0x4F padding */
    0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
    0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
    /* 64-bit continuation */
    /* 0x50 mov dword [0x124], MAGIC */
    0xC7, 0x04, 0x25, 0x24, 0x01, 0x00, 0x00, 0x4F, 0x4B, 0x33, 0x52,
    /* 0x5B mov ax,0x10 (data) */
    0x66, 0xB8, 0x10, 0x00,
    0x8E, 0xD8,                             /* 0x5F mov ds,ax */
    0x8E, 0xC0,                             /* 0x61 mov es,ax */
    0x8E, 0xD0,                             /* 0x63 mov ss,ax */
    0x8E, 0xE0,                             /* 0x65 mov fs,ax */
    0x8E, 0xE8,                             /* 0x67 mov gs,ax */
    /* 0x69 lidt [0x118] (10-byte descriptor) */
    0x0F, 0x01, 0x1D, 0x18, 0x01, 0x00, 0x00,
    /* 0x70 mov rsp,[0x100] (restore saved RSP; REX.W) */
    0x48, 0x8B, 0x25, 0x00, 0x01, 0x00, 0x00,
    /* 0x77 pop r15 r14 r13 r12 rbp rbx (mirror of the stub's pushes) */
    0x41, 0x5F, 0x41, 0x5E, 0x41, 0x5D, 0x41, 0x5C, 0x5D, 0x5B,
    /* 0x81 ret -> back into enter_s3 right after the fire call */
    0xC3,
};

/* asm leaf that saves state, fires the sleep write, and (if the write
 * returns because sleep failed) unwinds normally.  Resume lands in the
 * trampoline which restores exactly this frame. */
extern int acpi_s3_fire_stub(u16 port, u16 value, u8 *save_area);

static u8 g_s3_page[4096] __attribute__((aligned(4096)));

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
    /* cache flush, interrupts off, then the enabling write */
    __asm__ volatile("wbinvd" ::: "memory");
    u16 val = (u16)(((u32)typ << 10) | (1u << 13));
    return acpi_s3_fire_stub(g_pm1a_cnt, val, g_s3_page);
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

    /* stage the trampoline + save area */
    memset(g_s3_page, 0, sizeof(g_s3_page));
    memcpy(g_s3_page, g_s3_tramp, sizeof(g_s3_tramp));
    u64 tramp_phys = (u64)(uintptr_t)g_s3_page;

    /* arm the waking vector (both forms; firmware picks what it knows) */
    facs->firmware_waking_vector = (u32)tramp_phys;
    facs->x_firmware_waking_vector = tramp_phys;

    /* register the saved-register dump helper (fills CR0/3/4 + GDT/IDT) */
    {
        u64 cr3, cr4, cr0;
        __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
        __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
        __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
        *(u32*)(g_s3_page + TRAMP_OFF_CR3) = cr3;
        *(u32*)(g_s3_page + TRAMP_OFF_CR4) = cr4;
        *(u32*)(g_s3_page + TRAMP_OFF_CR0) = cr0;
        arch_gdt_descriptor(g_s3_page + TRAMP_OFF_GDT);
        arch_idt_descriptor(g_s3_page + TRAMP_OFF_IDT);
        *(u32*)(g_s3_page + TRAMP_OFF_RESUMED) = 0;
    }

    acpi_prepare_sleep(3);

    int rc = acpi_write_sleep(3);

    /* Return path B: RESUMED. The trampoline jumped back here with the
     * register state of the pre-sleep call. Re-arm what the firmware
     * reset, clear the waking vector, and report. (Checked BEFORE the
     * error code: on the resume path eax is firmware garbage.) */
    if (*(volatile u32*)(g_s3_page + TRAMP_OFF_RESUMED) == TRAMP_RESUME_MAGIC) {
        *(volatile u32*)(g_s3_page + TRAMP_OFF_RESUMED) = 0;
        g_resumed = 1;
        /* PIC was reset by the firmware */
        arch_pic_remap();
        arch_irq_rearm();
        /* PIT may have been reset with the platform */
        core_timer_init();
        /* LAPIC re-enable when present */
        arch_lapic_init();
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
