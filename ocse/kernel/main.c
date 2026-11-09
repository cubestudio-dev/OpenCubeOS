/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP06a-ocse
 * File: kernel/main.c  (OCSE edition, rewritten by tools/ocse_edits.py
 * group E11 from the pristine v1.0.0 kmain)
 * Purpose: OCSE kernel entry. The v1.0.0 boot ladder is kept (it is the
 * proven bring-up sequence for exactly the drivers OCSE vendored), the
 * ring-3 / shell / net / TUI / OTA stages are dropped, and the ladder
 * is re-cut into 24 numbered stages so the installer environment boot
 * is observable line by line. The ladder ends in ocse_main_run(), the
 * OCSE environment (detect / i18n / disk / GUI+CLI loop).
 */

#include "types.h"
#include "arch_multiboot2.h"
#include "screen_fb.h"
#include "screen_font.h"
#include "screen_console.h"
#include "l1_ext.h"
#include "lib_log.h"
#include "lib_klog.h"
#include "lib_string.h"

#include "arch_idt.h"
#include "arch_pic.h"
#include "arch_exceptions.h"
#include "arch_irq.h"
#include "arch_lapic.h"

#include "core_timer.h"
#include "core_sched.h"
#include "core_sync.h"
#include "core_rtc.h"
#include "core_power.h"
#include "core_crash.h"
#include "core_watchdog.h"

#include "mem_pmm.h"
#include "mem_vmm.h"
#include "mem_heap.h"

#include "fs_vfs.h"
#include "fs_ramfs.h"
#include "fs_fat32.h"
#include "fs_exfat.h"
#include "fs_ext4.h"

#include "driver_input_keyboard.h"
#include "driver_input_i2chid.h"
#include "screen_serial_in.h"
#include "screen_console_in.h"

#include "driver_acpi.h"
#include "driver_acpi_aml.h"
#include "driver_acpi_power.h"
#include "driver_pci.h"
#include "driver_pcie.h"
#include "driver_i2c.h"
#include "driver_i2c_dw.h"
#include "driver_i2c_i801.h"
#include "driver_smbus.h"

#include "driver_cmos_rtc.h"
#include "driver_hpet.h"
#include "driver_pmtimer.h"
#include "driver_tsc.h"

#include "driver_usb.h"
#include "driver_usb_hid.h"

#include "driver_block_blk.h"
#include "driver_block_cache.h"
#include "driver_block_ata.h"
#include "driver_block_ata_dma.h"
#include "driver_block_ahci.h"
#include "driver_block_nvme.h"
#include "driver_block_virtio_blk.h"
#include "driver_block_part.h"

#include "chinese_font.h"
#include "console_cn.h"
#include "render_cn.h"

#include "ocse_main.h"

/* WP-07-style local prototype (the v1.0.0 kmain declared it the same
 * way - no vendored header exports it). */
void driver_block_ata_register_blk(void);

/* OCSE environment assembly (new file, ocse/ocse_main.c). */
void ocse_main_run(const arch_multiboot2_info_t *mbi);

/* ---- serial output helpers (kept from the v1.0.0 kmain: the COM1
 * line is written directly - no other unit provides it). */
#define OCSE_COM1 0x3F8
static inline void ocse_outb(u16 p, u8 v) { __asm__ volatile("outb %0, %1" :: "a"(v), "Nd"(p)); }
static inline u8   ocse_inb(u16 p)        { u8 v; __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(p)); return v; }

static void screen_serial_init(void) {
    /* 115200 8N1, FIFO on - the QEMU -serial chardev defaults match. */
    ocse_outb(OCSE_COM1 + 1, 0x00);       /* no interrupts            */
    ocse_outb(OCSE_COM1 + 3, 0x80);       /* DLAB on                  */
    ocse_outb(OCSE_COM1 + 0, 0x01);       /* divisor low  (115200)    */
    ocse_outb(OCSE_COM1 + 1, 0x00);       /* divisor high             */
    ocse_outb(OCSE_COM1 + 3, 0x03);       /* 8N1                      */
    ocse_outb(OCSE_COM1 + 2, 0xC7);       /* FIFO, clear, 14B         */
    ocse_outb(OCSE_COM1 + 4, 0x0B);       /* DTR+RTS+OUT2             */
}

static void screen_serial_putc(char c) {
    while ((ocse_inb(OCSE_COM1 + 5) & 0x20) == 0) ;
    ocse_outb(OCSE_COM1, (u8)c);
}

static void screen_serial_puts(const char* s) {
    while (*s) screen_serial_putc(*s++);
}

/* Shared serial line for the OCSE helpers (i18n console echo). Adds the
 * CR on LF so serial logs stay flat. */
void ocse_serial_puts_raw(const char* s) {
    while (*s) {
        if (*s == '\n') screen_serial_putc('\r');
        screen_serial_putc(*s++);
    }
}


/* ---- the 24-stage ladder ----------------------------------------------
 * Every stage prints one line on serial and one in the console log.
 * A stage that fails prints FAIL and halts (except where noted). */
#define OCSE_TOTAL_STAGES 24

static int g_stage_no = 0;
static char g_stage_line[96];

static void ocse_stage(const char *what) {
    char n[8];
    /* Live-caught (WP06a boot T1): callers may pass g_stage_line itself
     * (the stage-9 report is built there). Copying `what` into the line
     * AFTER clearing its head is an OVERLAPPING strcpy that replicates
     * its own prefix forever - it walked over 320 KiB of .bss into the
     * boot page tables and triple-faulted. Snapshot the caller text to
     * a stack buffer first, always. */
    char tmp[128];
    u32 k = 0;
    for (; what && what[k] && k < sizeof(tmp) - 1; k++) tmp[k] = what[k];
    tmp[k] = 0;
    g_stage_no++;
    strcpy(g_stage_line, "[ocse] stage ");
    u64_to_str((u64)g_stage_no, n);
    strcpy(g_stage_line + strlen(g_stage_line), n);
    strcpy(g_stage_line + strlen(g_stage_line), "/");
    u64_to_str((u64)OCSE_TOTAL_STAGES, n);
    strcpy(g_stage_line + strlen(g_stage_line), n);
    strcpy(g_stage_line + strlen(g_stage_line), ": ");
    strcpy(g_stage_line + strlen(g_stage_line), tmp);
    screen_serial_puts(g_stage_line);
    screen_serial_puts("\r\n");
    lib_log_info(g_stage_line);
    screen_console_puts(g_stage_line);
    screen_console_putc('\n');
}

static void ocse_stage_fail(const char *what) {
    char n[8];
    g_stage_no++;
    strcpy(g_stage_line, "[ocse] STAGE ");
    u64_to_str((u64)g_stage_no, n);
    strcpy(g_stage_line + strlen(g_stage_line), n);
    strcpy(g_stage_line + strlen(g_stage_line), " FAILED: ");
    strcpy(g_stage_line + strlen(g_stage_line), what);
    screen_serial_puts(g_stage_line);
    screen_serial_puts("\r\n");
    lib_log_info(g_stage_line);
    screen_console_puts(g_stage_line);
    screen_console_putc('\n');
}

/* ---- banner ------------------------------------------------------------ */
static void ocse_draw_banner(void) {
    screen_console_putc('\n');
    screen_console_puts("  ____                    ______  ____      ____\n");
    screen_console_puts(" / __ \\                  |____  ||___ \\    / ___|\n");
    screen_console_puts("| |  | |_ __ ___  _ __ __    / /    __) | | |\n");
    screen_console_puts("| |  | | '__/ _ \\| '_ \\ \\ \\  / /    |__ <  | |\n");
    screen_console_puts("| |__| | | | (_) | | | | |>  < / /    ___) |_| |___\n");
    screen_console_puts(" \\____/|_|  \\___/|_| |_/_/\\ \\_\\_/   |____/( )\\____|\n");
    screen_console_puts("                                          |/\n");
    screen_console_puts(" OpenCubeOS Setup Environment (OCSE) - WP-06a\n");
    screen_console_puts(" Apache 2.0 - cubestudio-dev\n");
    screen_console_putc('\n');
}

/* ---- deliberate exception self-test (kept verbatim from the v1.0.0
 * kmain: temporary handlers redirect RIP to the resume label after each
 * fault, proving the IDT + exception chain is alive; the counter shows
 * 3/3 when the dispatch path works). */
static u64 g_exc_test_pass_count = 0;

static int arch_exc_test_de_handler(arch_irq_frame_t *f) {
    extern u8 arch_exc_test_de_resume;
    f->rip = (u64)(uintptr_t)&arch_exc_test_de_resume;
    f->rax = 0; f->rdx = 0;
    g_exc_test_pass_count++;
    return 1;
}
static int arch_exc_test_ud_handler(arch_irq_frame_t *f) {
    extern u8 arch_exc_test_ud_resume;
    f->rip = (u64)(uintptr_t)&arch_exc_test_ud_resume;
    g_exc_test_pass_count++;
    return 1;
}
static int arch_exc_test_pf_handler(arch_irq_frame_t *f) {
    extern u8 arch_exc_test_pf_resume;
    f->rip = (u64)(uintptr_t)&arch_exc_test_pf_resume;
    g_exc_test_pass_count++;
    return 1;
}

static void arch_exc_tests(void) {
    arch_exc_register_handler(OC_EXC_DE, arch_exc_test_de_handler);
    arch_exc_register_handler(OC_EXC_UD, arch_exc_test_ud_handler);
    arch_exc_register_handler(OC_EXC_PF, arch_exc_test_pf_handler);

    __asm__ volatile(
        "xor %%rcx, %%rcx\n"
        "div %%rcx\n"
        "arch_exc_test_de_resume:\n"
        : : : "rax","rcx","rdx"
    );
    __asm__ volatile(
        ".byte 0x0F, 0x0B\n"
        "arch_exc_test_ud_resume:\n"
        : :
    );
    __asm__ volatile(
        "movq $0xFFFFFFFFDEADBEEF, %%rax\n"
        "movq (%%rax), %%rbx\n"
        "arch_exc_test_pf_resume:\n"
        : : : "rax","rbx"
    );

    arch_exc_unregister_handler(OC_EXC_DE, arch_exc_test_de_handler);
    arch_exc_unregister_handler(OC_EXC_UD, arch_exc_test_ud_handler);
    arch_exc_unregister_handler(OC_EXC_PF, arch_exc_test_pf_handler);
}

/* ---- kmain: the OCSE boot ladder -------------------------------------- */
void kmain(u64 magic, u64 mbi_phys) {
    /* 1: serial console */
    screen_serial_init();
    screen_serial_putc('\r'); screen_serial_putc('\n');
    screen_serial_puts("[ocse] OpenCubeOS Setup Environment "
                       OC_RELEASE_VERSION "\r\n");
    ocse_stage("serial console (COM1)");

    /* 2: multiboot2 handshake (OCBL handoff contract) */
    if (magic != OC_MB2_MAGIC) {
        ocse_stage_fail("multiboot2 magic");
        for (;;) { __asm__ volatile("hlt"); }
    }
    static arch_multiboot2_info_t mbi;
    if (arch_multiboot2_parse(&mbi, (uintptr_t)mbi_phys) != 0) {
        ocse_stage_fail("multiboot2 info parse");
        for (;;) { __asm__ volatile("hlt"); }
    }
    ocse_stage("multiboot2 handshake (OCBL handoff)");

    /* 3: framebuffer (from the OCBL mb2 framebuffer tag) */
    if (screen_fb_init(mbi.fb) != 0) {
        ocse_stage_fail("framebuffer init");
        for (;;) { __asm__ volatile("hlt"); }
    }
    ocse_stage("framebuffer (OCBL-provided 800x600x32)");

    /* 4: console + bilingual renderer + logs */
    screen_console_init();
    render_chinese_init();
    console_cn_init();
    lib_log_init();
    klog_init();
    screen_console_reset(screen_fb_rgb(0xE0, 0xE0, 0xE0),
                         screen_fb_rgb(0x10, 0x10, 0x14));
    l1_ext_register_font_engine(l1_ext_default_font_engine());
    ocse_stage("console + CJK renderer + kernel log");

    /* 5: banner */
    ocse_draw_banner();

    /* 6: IDT + PIC */
    arch_idt_init();
    ocse_stage("IDT + GDT + PIC remap");

    /* 7: PIT timer */
    core_timer_init();
    ocse_stage("PIT timer @ 100 Hz");

    /* 8: PS/2 keyboard */
    driver_input_keyboard_init();
    ocse_stage("PS/2 keyboard");

    /* 9: serial input + console line editor */
    screen_serial_in_init();
    screen_console_in_init();
    ocse_stage("COM1 serial input + line editor");

    /* 10: interrupts on + exception self-test */
    __asm__ volatile("sti");
    {
        u64 before = g_exc_test_pass_count;
        arch_exc_tests();
        char tail[24];
        strcpy(g_stage_line, "interrupts on + exception self-test (");
        u64_to_str(g_exc_test_pass_count - before, tail);
        strcat(g_stage_line, tail);
        strcat(g_stage_line, "/3 caught)");
        ocse_stage(g_stage_line);
    }

    /* 11: PMM */
    mem_pmm_init((arch_multiboot2_info_t *)&mbi);
    ocse_stage("PMM (physical memory manager)");

    /* 12: VMM */
    mem_vmm_init();
    ocse_stage("VMM (virtual memory manager)");

    /* 13: heap */
    mem_heap_init();
    klog_grow();
    ocse_stage("kernel heap + full klog ring");

    /* 14: scheduler */
    core_sched_init();
    ocse_stage("scheduler + sync primitives");

    /* 15: ACPI namespace + power */
    {
        int acpi_ok = 0;
        (void)driver_acpi_aml_selftest();   /* throwaway namespace, cached */
        if (driver_acpi_init(mbi.rsdp_phys) == 0) {
            if (driver_acpi_dsdt_phys())
                driver_acpi_aml_load_table(driver_acpi_dsdt_phys());
            for (int i = 0; i < driver_acpi_ssdt_count(); i++)
                driver_acpi_aml_load_table(driver_acpi_ssdt_phys(i));
            driver_acpi_power_init();
            acpi_ok = driver_acpi_aml_loaded();
        }
        if (acpi_ok) ocse_stage("ACPI namespace (DSDT/SSDT) + power");
        else        ocse_stage("ACPI absent - power paths unavailable");
    }

    /* 16: PCIe + ESB watchdog + SMBus + touchpad */
    (void)driver_pcie_scan();
    (void)driver_pcie_assign_bars();
    watchdog_init();   /* WP-12 probe: TCO/WDAT - the QEMU i6300ESB */
    if (driver_i2c_i801_init() == 0) (void)driver_smbus_init();
    (void)driver_input_i2chid_init();
    {
        const char *wd = watchdog_hw_present()
            ? "PCIe + i6300ESB watchdog + SMBus + touchpad"
            : "PCIe + (no watchdog) + SMBus + touchpad";
        ocse_stage(wd);
    }

    /* 17: RTC + HPET */
    if (driver_cmos_rtc_init() == 0) ocse_stage("CMOS RTC");
    else                             ocse_stage("CMOS RTC: not conclusive");
    if (driver_hpet_init() == 0)      ocse_stage("HPET time source");
    else                              ocse_stage("HPET: absent (PIT stands in)");

    /* 18: VFS + ramfs */
    fs_vfs_init();
    fs_ramfs_init();
    ocse_stage("VFS + ramfs");

    /* 19: ATA block layer */
    driver_block_init();
    driver_block_cache_init();
    driver_block_ata_init();
    driver_block_ata_dma_init(NULL);
    driver_block_ata_register_blk();
    driver_block_virtio_blk_init();
    driver_block_ahci_init(NULL);
    driver_block_nvme_init(NULL);
    ocse_stage("block layer (ATA/PIO+DMA, virtio/AHCI/NVMe probes)");

    /* 20: USB host controllers + HID */
    driver_usb_hid_init();
    {
        int uh = driver_usb_probe_all();
        driver_usb_poll_start();
        char tail[24];
        strcpy(g_stage_line, "USB stack up (");
        u64_to_str((u64)(uh > 0 ? uh : 0), tail);
        strcat(g_stage_line, tail);
        strcat(g_stage_line, " HC backends, HID kbd+mouse)");
        ocse_stage(g_stage_line);
    }

    /* 21: filesystems + PM timer */
    fs_fat32_init();
    fs_exfat_init();
    fs_ext4_init();
    (void)driver_pmtimer_init();
    ocse_stage("FAT32/exFAT/ext4 + ACPI PM timer");

    /* 22: partition scan */
    driver_block_part_scan_register_all();
    ocse_stage("partition scan (MBR + GPT children)");

    /* 23: crash infrastructure (panic + dump + crashlog) */
    core_dump_alloc();
    crashlog_boot_alloc();
    crash_wdt_init();
    ocse_stage("crash recovery armed (dump + crashlog + soft watchdog)");

    /* 24: the OCSE environment itself */
    ocse_stage("handing over to the OCSE environment");
    ocse_main_run(&mbi);

    for (;;) __asm__ volatile("hlt");
}
