/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP03 - kernel main entry (kmain).
 * 3.0.0-WP03: the L0 user plane is gone (oc> shell, commands, TUI,
 * IME all deleted); kmain is now a pure service-kernel boot: it
 * brings up the mechanisms (framebuffer console, IDT/PIC/timer,
 * PMM/VMM/heap, scheduler/sync, userspace), collects the driver /
 * service / L1-app declarations, probes the hardware, arms the
 * crash infrastructure, announces "L0 ready, waiting for L1" and
 * hands the machine to the default ring-3 entry (ush) via the L1
 * launcher.  The historical kmain bodies (self-test commands, the
 * ssh/sshd/net command wrappers) either died with the user plane or
 * were rescued into their owning services/ folders.
 *
 * =====================================================================
 * 3.0.0-WP03 G5: the BOOT DEPENDENCY CHAIN (the spec's five phases,
 * mapped onto this function - each phase prints a "boot chain N/4"
 * marker that the boot-order test asserts):
 *
 *   PHASE 1  kernel core mechanisms        (nothing above may depend
 *     serial/fb console, log, oc.* embed facts, banner, IDT/PIC,
 *     exceptions, PIT, keyboard/serial input, PMM/VMM/heap,
 *     scheduler/sync, userspace, registry collection, ACPI + PCIe +
 *     watchdog hardware + I2C/SMBus chipset, RTC + HPET time sources
 *
 *   PHASE 2  drivers + driver-free services   (interleaved: the two
 *     halves are independent of each other - VFS/ramfs need no
 *     driver, the block/USB/wireless/SD/BT/camera/misc drivers need
 *     no service; the boot log order within this phase is free)
 *     VFS + ramfs (driver-free services; encoding + the dynamic
 *     loader are demand-initialized on first use), block driver
 *     family probe (COLLECTED from .oc_drvs), SCSI, USB stack
 *
 *   PHASE 3  driver-dependent services     (each mounts/starts only
 *     FAT/exFAT/ext4 volume drivers + the /etc config + A/B slots
 *     (need the block family), the PM timer (needs ACPI + config),
 *     wireless families (need the fs for firmware files) + SDHCI,
 *     Bluetooth, camera, legacy ports, the network stack (needs the
 *     NIC family), the audio service (needs the snd family)
 *
 *   PHASE 4  interfaces ready              (everything is up: crash
 *     dump + crashlog + soft watchdog armed, the health monitor
 *     started, the auto-update check scheduled, the slot-B boot
 *     confirmed, the OCIRE embed boot-confirmation - on a supervised
 *     boot SIGNAL_OK + the i6300esb disarm + the whole-disk write
 *     protection + the protected agent)
 *
 *   PHASE 5  "L0 ready, waiting for L1"   (the expected terminal
 *     state of the L0 console: the launcher hands the machine to the
 *     default ring-3 entry, ush)
 * =====================================================================
 */
#include "types.h"
#include "arch_multiboot2.h"
#include "screen_fb.h"
#include "screen_font.h"
#include "screen_console.h"
#include "font_engine.h"    /* 3.0.0-WP03: was l1_ext.h (services/l1/ grab-bag, split per root service) */
int l1_ext_self_test(void);  /* boot/l1_selftest.c - WP-01 extension self-test */
#include "l1_wp8a.h"   /* BUG-0254 FIX: the 21 int 0x80 wrappers get kernel-side callers (selftest step 8) */
#include "lib_log.h"
#include "lib_string.h"

/* WP-02 headers. */
#include "arch_idt.h"
#include "arch_pic.h"
#include "arch_exceptions.h"
#include "arch_irq.h"
#include "core_timer.h"
#include "driver_input_keyboard.h"
#include "screen_serial_in.h"
#include "core_kcap.h"      /* 3.0.0-WP03: captured-run infra + base hook */
#include "screen_console_in.h"

/* WP-03 headers. */
#include "mem_pmm.h"
#include "mem_vmm.h"
#include "mem_heap.h"

/* WP-04 headers. */
#include "core_sched.h"
#include "core_sync.h"
#include "core_usermode.h"
#include "disp_lang.h"   /* 3.0.0-WP03: console language (ex shell_lang) */
#include "core_crash.h"   /* WP-11-crash_recovery */

/* WP-05 headers. */
#include "fs_vfs.h"
#include "fs_ramfs.h"
#include "driver_block_ata.h"
#include "fs_fat32.h"
#include "core_watchdog.h"     /* WP-12: watchdog subsystem init */
#include "core_health.h"       /* WP-12: health monitor init */

/* WP-06 headers. */
#include "net_core.h"
#include "config_gen.h"   /* WP-13 multi-config: OC_CONFIG_* + stubs */
/* 3.0.0-WP03: the CJK display pipeline is unconditional (the
 * CONFIG_TUI / CONFIG_CHINESE gates are deleted with the TUI user
 * plane). */
#include "chinese_font.h"
#include "encoding.h"
#include "render_cn.h"
#include "console_cn.h"

void net_wl_fw_provider_install(void); /* 3.0.0-WP03 G2: wl firmware provider (services/net/wl_fw.c) */
#include "block_svc.h"
#include "driver_block_cache.h"
#include "driver_block_part.h"
#include "driver_block_virtio_blk.h"
#include "driver_block_nvme.h"
#include "driver_block_ahci.h"      /* WP-10a: SATA */
#include "driver_block_ata_dma.h"   /* WP-10a: Bus-Master IDE */
#include "nic_svc.h"            /* WP-10b: NIC drivers + test/status commands */
#include "ota_ab.h"       /* WP-10u: A/B slots + in-system update */
#include "core_power.h"           /* WP-10d-fix2: shutdown/suspend/halt/reboot */
#include "snd_svc.h"             /* WP-10c: sound card drivers */
#include "driver_usb.h"              /* WP-10d: USB stack */
#include "driver_usb_hid.h"
#include "driver_usb_msc.h"
#include "driver_usb_serial.h"
#include "driver_usb_audio.h"
#include "fs_exfat.h"
#include "fs_ext4.h"
#include "driver_block_disk_setup.h"   /* WP-10d-pre: abdisk/install/grub-install */
#include "driver_acpi.h"               /* WP-10-input_chipset: ACPI core */
#include "driver_acpi_aml.h"           /* WP-10-input_chipset: AML interpreter */
#include "driver_acpi_power.h"         /* WP-10-input_chipset: S3/S4/S5 + SCI */
#include "driver_pcie.h"               /* WP-10-input_chipset: PCIe/MSI layer */
#include "driver_i2c_i801.h"           /* WP-10-input_chipset: i801/PIIX4 I2C */
#include "driver_smbus.h"              /* WP-10-input_chipset: SMBus protocol */
#include "driver_input_i2chid.h"       /* WP-10-input_chipset: touchpad */
#include "arch_lapic.h"                /* WP-10-input_chipset: LAPIC/MSI */
#include "driver_cmos_rtc.h"           /* WP-10-timer_rtc: CMOS RTC IRQ/alarm */
#include "driver_wireless.h"           /* WP-10-wireless_bluetooth: 802.11 core */
#include "driver_iwlwifi.h"            /* WP-10-wireless_bluetooth: Intel */
#include "driver_rtlwifi.h"            /* WP-10-wireless_bluetooth: Realtek */
#include "driver_ath.h"                /* WP-10-wireless_bluetooth: Atheros */
#include "driver_brcm.h"               /* WP-10-wireless_bluetooth: Broadcom */
#include "driver_sdhci.h"              /* WP-10-wireless_bluetooth: SDHCI/SDIO */
#include "driver_bt.h"                 /* WP-10-wireless_bluetooth: Bluetooth */
#include "driver_camera.h"             /* WP-10-camera_misc: camera core */
#include "driver_camera_uvc.h"         /* WP-10-camera_misc: UVC class */
#include "driver_parallel.h"           /* WP-10-camera_misc: IEEE 1284 */
#include "driver_gameport.h"           /* WP-10-camera_misc: game port */
#include "driver_ir.h"                 /* WP-10-camera_misc: infrared */
#include "driver_scsi.h"               /* WP-10-camera_misc: SCSI core */
#include "driver_scsi_lsi.h"           /* WP-10-camera_misc: LSI HBA */
#include "driver_hpet.h"               /* WP-10-timer_rtc: HPET */
#include "driver_pmtimer.h"            /* WP-10-timer_rtc: ACPI PM timer */
#include "lib_config.h"   /* WP-09-fix5: /etc/opencube.conf */
const lib_config_backend_t *fs_config_backend(void); /* 3.0.0-WP03 G5: services/fs */
#include "lib_klog.h"     /* WP-12: unified kernel log */
#include "klog_file.h"     /* 3.0.0-WP03 G5: services/klog (the on-disk sink) */
void crash_install_panic_handlers(void); /* 3.0.0-WP03 G5: services/crash */
#include "ota_update.h"   /* WP-09-fix5: checkupdate */
#include "version_gen.h"   /* WP-10-tui-qa: version recompile trigger (Makefile note) */
#include "core_registry.h"
#include "ocembed.h"      /* 3.0.0-WP03 G4: OCIRE embed (boot signal + agent) */

/* User program data (defined in usermode.c). */
extern const u8 userprog_hello[];
extern const u64 userprog_hello_size;
extern const u8 userprog_badapp[];
extern const u64 userprog_badapp_size;
extern const u8 userprog_loop[];
extern const u64 userprog_loop_size;
extern const u8 userprog_fork_test[];
extern const u64 userprog_fork_test_size;
extern const u8 userprog_exec_test[];
extern const u64 userprog_exec_test_size;
extern const u8 userprog_pipe_test[];
extern const u64 userprog_pipe_test_size;
extern const u8 userprog_mmap_test[];
extern const u64 userprog_mmap_test_size;
extern const u8 userprog_signal_test[];
extern const u64 userprog_signal_test_size;
extern const u8 userprog_select_test[];
extern const u64 userprog_select_test_size;
/* WP-08b Batch 1: ET_DYN test program (PIE ELF with PT_INTERP=/lib/ld.so). */
extern const u8 userprog_dyn_test[];
extern const u64 userprog_dyn_test_size;
/* WP-08b Batch 5: additional dynamic test programs. */
extern const u8 userprog_dyn_hello[];
extern const u64 userprog_dyn_hello_size;
extern const u8 userprog_so_test[];
extern const u64 userprog_so_test_size;
extern const u8 userprog_dlsym_test[];
extern const u64 userprog_dlsym_test_size;
extern const u8 userprog_pie_test[];
extern const u64 userprog_pie_test_size;
extern const u8 userprog_reloc_test[];
extern const u64 userprog_reloc_test_size;
extern const u8 userprog_main_dyn[];
extern const u64 userprog_main_dyn_size;
extern const u8 userprog_mmap_multi[];
extern const u64 userprog_mmap_multi_size;
extern const u8 userprog_g7mmap[];      /* WP-10-AUDIT_P2-fix3 G7: BUG-0281 regression */
extern const u64 userprog_g7mmap_size;
extern const u8 userprog_ush[];
extern const u8 userprog_test_min[];
extern const u64 userprog_test_min_size;
extern const u8 userprog_test_bss[];
extern const u64 userprog_test_bss_size;
extern const u64 userprog_ush_size;
extern const u8 userprog_mprotect_test[];
extern const u64 userprog_mprotect_test_size;

extern const u8 userprog_p3_test[];
extern const u64 userprog_p3_test_size;

/* BUG-0042 repro: per-task FPU/SSE state check. */
extern const u8 userprog_sse_test[];
extern const u64 userprog_sse_test_size;
extern const u8 userprog_pf_test[];
extern const u64 userprog_pf_test_size;
/* WP-11-crash_recovery: SIGSEGV delivery + ring-3 stack overflow. */
extern const u8 userprog_sigsegv_test[];
extern const u64 userprog_sigsegv_test_size;
extern const u8 userprog_stackovf[];
extern const u64 userprog_stackovf_size;

/* p1fix2 repro programs. */
extern const u8 userprog_fdref_test[];
extern const u64 userprog_fdref_test_size;
extern const u8 userprog_select_zero_test[];
extern const u64 userprog_select_zero_test_size;
extern const u8 userprog_int3_user[];
extern const u8 userprog_priv_test[];
extern const u64 userprog_priv_test_size;
extern const u64 userprog_int3_user_size;
extern const u8 userprog_g6_test[];
extern const u64 userprog_g6_test_size;
extern const u8 userprog_g6_exec_a[];
extern const u64 userprog_g6_exec_a_size;
extern const u8 userprog_g6_exec_b[];
extern const u64 userprog_g6_exec_b_size;
extern const u8 userprog_g6_oom[];
extern const u64 userprog_g6_oom_size;

/* Direct serial output via I/O port 0x3F8 (COM1). */
static inline void outb(u16 port, u8 v) {
    __asm__ volatile("outb %0, %1" :: "a"(v), "Nd"(port));
}
static inline u8 inb(u16 port) {
    u8 v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
#define COM1 0x3F8
static void screen_serial_init(void) {
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x01);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x0B);
}
static void screen_serial_putc(char c) {
    while ((inb(COM1 + 5) & 0x20) == 0) ;
    outb(COM1, (u8)c);
}
static void screen_serial_puts(const char *s) {
    while (*s) screen_serial_putc(*s++);
}
static int screen_serial_hook(void* ctx, u8 ch) {
    (void)ctx;
    if (ch == '\n') screen_serial_putc('\r');
    screen_serial_putc((char)ch);
    return 0;
}

/* Draw the splash banner. All ASCII - no em dash. */
static void draw_banner(void) {
    u32 band_color = screen_fb_rgb(0x18, 0x18, 0x28);
    screen_fb_fill_rect(0, 0, screen_fb_get_info()->width, 80, band_color);
    screen_console_move_cursor(2, 1);
    screen_console_putc(' ');
    screen_console_putc(' ');
    u32 saved_fg = screen_console_get()->fg_pixel;
    u32 saved_bg = screen_console_get()->bg_pixel;
    screen_console_get()->fg_pixel = screen_fb_rgb(0xFF, 0xFF, 0xFF);
    screen_console_get()->bg_pixel = band_color;
    screen_console_puts("Open Cube OS  [" OC_RELEASE_VERSION "]");
    screen_console_get()->fg_pixel = saved_fg;
    screen_console_get()->bg_pixel = saved_bg;
    screen_console_move_cursor(0, 4);
}

/* WP-02 boot log with real timestamps. */
static void lib_log_stage_ts(const char *stage, const char *status) {
    char tmp[80];
    char ts[16];
    core_timer_format_hms(core_timer_now_ms(), ts);
    int p = 0;
    tmp[p++] = '[';
    strcpy(tmp + p, ts); p += strlen(ts);
    tmp[p++] = ']'; tmp[p++] = ' ';
    strcpy(tmp + p, stage); p += strlen(stage);
    usize stage_len = strlen(stage);
    usize dot_count = 50 > stage_len ? (50 - stage_len) : 1;
    for (usize i = 0; i < dot_count; i++) tmp[p++] = '.';
    tmp[p++] = ' ';
    strcpy(tmp + p, status); p += strlen(status);
    tmp[p] = 0;
    screen_console_puts(tmp);
    screen_console_putc('\n');
    /* WP-12: mirror the boot stage into the unified klog ring (record
     * only - this line is already on the console) so dmesg shows the
     * full boot sequence. */
    klog_record(lib_log_status_is_failure(status)
                    ? KLOG_ERROR : KLOG_INFO, "boot", tmp);
}
#define OC_LOG_OK2(stage)    lib_log_stage_ts((stage), "OK")
#define OC_LOG_FAIL2(stage) lib_log_stage_ts((stage), "FAIL")

/* ---- Exception self-test ---- */
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

void kmain(u64 magic, u64 mbi_phys) {
    /* ---- 0. Serial console ---- */
    screen_serial_init();
    screen_serial_putc('\r'); screen_serial_putc('\n');
    screen_serial_puts("[oc] Open Cube OS " OC_RELEASE_VERSION " kmain entered\r\n");

    /* ---- 1. Validate multiboot2 ---- */
    if (magic != OC_MB2_MAGIC) {
        screen_serial_puts("[oc] FATAL: bad multiboot2 magic\r\n");
        for (;;) { __asm__ volatile("hlt"); }
    }
    arch_multiboot2_info_t mbi;
    if (arch_multiboot2_parse(&mbi, (uintptr_t)mbi_phys) != 0) {
        screen_serial_puts("[oc] FATAL: multiboot2 parse failed\r\n");
        for (;;) { __asm__ volatile("hlt"); }
    }

    /* ---- 2. Framebuffer ---- */
    if (screen_fb_init(mbi.fb) != 0) {
        screen_serial_puts("[oc] FATAL: fb_init failed\r\n");
        for (;;) { __asm__ volatile("hlt"); }
    }

    /* ---- 3. Console + log + serial hook ---- */
    screen_console_init();
    /* WP-10-chinese_support: activate the mixed-width renderer (ASCII
     * output stays byte-identical; CJK codepoints render 16x16) and
     * load the embedded GB2312 font. */
    render_chinese_init();
    console_cn_init();
    lib_log_init();
    klog_init();     /* WP-12: unified kernel log, before any entry */
    screen_console_reset(screen_fb_rgb(0xE0, 0xE0, 0xE0),
                     screen_fb_rgb(0x10, 0x10, 0x14));
    core_kcap_set_base_hook(screen_serial_hook, NULL);

    /* G5 boot chain marker: phase 1/4 - kernel core mechanisms (the
     * console/log stack above IS phase 1's first bring-up; the marker
     * prints the moment it can be seen) */
    OC_LOG_OK2("boot chain 1/4: kernel core mechanisms");

    /* ---- 3b. 3.0.0-WP03 G4: OCIRE embed facts ----
     * Parse the oc.* command-line facts the recovery supervisor
     * handed over (kexec) and capture the injected log-agent module.
     * On a normal GRUB boot nothing matches and the embed service
     * stays dormant.  Runs before the banner so a supervised boot is
     * distinguishable from the very first console lines. */
    ocembed_early_init(mbi.cmdline);

    /* ---- 4. Banner ---- */
    draw_banner();

    /* ---- 5. Boot log (WP-01 stages) ---- */
    lib_log_info("Open Cube OS - L0 kernel (" OC_RELEASE_VERSION ")");
    lib_log_info("Apache 2.0 licensed. See LICENSE.");
    screen_console_putc('\n');

    OC_LOG_OK("multiboot2 handshake");
    OC_LOG_OK("long mode entry");
    OC_LOG_OK("framebuffer 800x600x32");

    {
        char line[128];
        const screen_fb_info_t* fb = screen_fb_get_info();
        char hex[32], dec[16];
        strcpy(line, "fb: addr=0x");
        u64_to_hex((u64)(uintptr_t)fb->addr, hex, 12);
        strcpy(line + strlen(line), hex);
        strcpy(line + strlen(line), " pitch=");
        u64_to_str(fb->pitch, dec); strcpy(line + strlen(line), dec);
        strcpy(line + strlen(line), " mask=R");
        u64_to_str(fb->red_size,   dec); strcpy(line + strlen(line), dec);
        strcpy(line + strlen(line), "G");
        u64_to_str(fb->green_size, dec); strcpy(line + strlen(line), dec);
        strcpy(line + strlen(line), "B");
        u64_to_str(fb->blue_size,  dec); strcpy(line + strlen(line), dec);
        lib_log_info(line);
    }

    OC_LOG_OK("console grid");
    OC_LOG_OK("default 8x16 font engine");
    l1_ext_register_font_engine(l1_ext_default_font_engine());
    OC_LOG_OK("WP-01 ext: fb access / renderer / font / console hook");

    /* ---- 6. WP-02: IDT + PIC + exceptions ---- */
    arch_idt_init();
    OC_LOG_OK2("IDT + GDT + TSS (256 gates)");
    OC_LOG_OK2("8259 PIC remap (IRQ0-15 -> vec 32-47)");

    /* ---- 7. WP-02: PIT timer ---- */
    core_timer_init();
    OC_LOG_OK2("PIT @ 100 Hz + tick counter");

    /* ---- 8. WP-02: Keyboard + serial input ---- */
    driver_input_keyboard_init();
    OC_LOG_OK2("PS/2 keyboard (scancode set 1 -> ASCII)");
    screen_serial_in_init();
    OC_LOG_OK2("COM1 serial RX -> keyboard queue");
    screen_console_in_init();
    OC_LOG_OK2("console input line editor");

    /* ---- 9. Enable interrupts ---- */
    __asm__ volatile("sti");
    OC_LOG_OK2("interrupts enabled (sti)");

    /* ---- 10. WP-01 extension self-test (boot/l1_selftest.c) ---- */
    int passed = l1_ext_self_test();
    {
        char line[64]; char dec[8];
        strcpy(line, "WP-01 ext self-test: ");
        u64_to_str((u64)passed, dec); strcpy(line + strlen(line), dec);
        strcpy(line + strlen(line), "/4 points reachable");
        lib_log_info(line);
    }
    if (passed == 4) OC_LOG_OK2("WP-01 extension self-test");
    else             OC_LOG_FAIL2("WP-01 extension self-test");

    /* Re-install serial hook (WP-01 self-test clears it in step 4). */
    core_kcap_set_base_hook(screen_serial_hook, NULL);

    /* ---- 11. WP-02 exception self-test ---- */
    {
        u64 before = g_exc_test_pass_count;
        arch_exc_tests();
        u64 after = g_exc_test_pass_count;
        char line[80]; char n[20];
        strcpy(line, "exception self-test: ");
        u64_to_str(after - before, n); strcpy(line + strlen(line), n);
        strcpy(line + strlen(line), "/3 (#DE/#UD/#PF) caught, kernel alive");
        lib_log_info(line);
        if (after - before == 3) OC_LOG_OK2("exception self-test");
        else                     OC_LOG_FAIL2("exception self-test");
    }

    /* ---- 12. WP-03: PMM + VMM + Heap ---- */
    mem_pmm_init(&mbi);
    OC_LOG_OK2("PMM (physical memory manager)");

    mem_vmm_init();
    OC_LOG_OK2("VMM (virtual memory manager)");

    mem_heap_init();
    klog_grow();     /* WP-12: swap the bootstrap ring for the full 1024 */
    OC_LOG_OK2("kernel heap (kmalloc/kfree)");

    /* ---- 12b. WP-04: Scheduler + Sync ---- */
    core_sched_init();
    OC_LOG_OK2("scheduler (preemptive, priority + round-robin)");
    OC_LOG_OK2("sync primitives (spinlock/sem/mutex/cond)");

    /* ---- 12c. WP-04: Userspace ---- */
    usermode_init();
    OC_LOG_OK2("userspace (ring 3, syscalls, ELF loader)");

    /* ---- 12c-2. declaration registry collection ----
     * Walk the .oc_drvs/.oc_svcs/.oc_apps linker sections once,
     * early: every later consumer (the driver probe passes, the
     * SYS_L1SVC dispatch, the L1 launcher) reads the collected
     * tables.  (3.0.0-WP03: the .oc_cmds section died with the oc>
     * command surface.) */
    core_registry_collect();
    {
        char line[96]; char n[12];
        strcpy(line, "registry: drv=");
        u64_to_str((u64)core_registry_drvs_count(), n); strcat(line, n);
        strcat(line, " svc=");  u64_to_str((u64)core_registry_svcs_count(), n); strcat(line, n);
        strcat(line, " app=");  u64_to_str((u64)core_registry_apps_count(), n); strcat(line, n);
        lib_log_info(line);
    }

    /* ---- WP-10-input_chipset: ACPI + chipset + input ----
     * Order matters: AML evaluation needs the kernel heap (values are
     * kmalloc-backed); the PCI cache scan must run BEFORE the storage
     * drivers probe devices (BAR sizing rewrites config BARs); the
     * ACPI power module registers the SCI on the PIC (IRQ9). */
    (void)driver_acpi_aml_selftest();   /* throwaway namespace, cached */
    if (driver_acpi_init(mbi.rsdp_phys) == 0) {
        if (driver_acpi_dsdt_phys())
            driver_acpi_aml_load_table(driver_acpi_dsdt_phys());
        for (int i = 0; i < driver_acpi_ssdt_count(); i++)
            driver_acpi_aml_load_table(driver_acpi_ssdt_phys(i));
        if (driver_acpi_aml_loaded()) OC_LOG_OK2("AML namespace (DSDT + SSDTs)");
        driver_acpi_power_init();
    } else {
        OC_LOG_FAIL2("ACPI: RSDP not found (no ACPI power paths)");
    }
    (void)driver_pcie_scan();
    (void)driver_pcie_assign_bars();
    watchdog_init();   /* WP-12: WDAT/TCO probe + soft slots + 1s tick */
    if (watchdog_hw_present()) OC_LOG_OK2("hardware watchdog (TCO/WDAT probe)");
    if (driver_i2c_i801_init() == 0) OC_LOG_OK2("i801/PIIX4 SMBus controller");
    (void)driver_smbus_init();
    (void)driver_input_i2chid_init();
    OC_LOG_OK2("chipset layer (PCIe/MSI/I2C/SMBus/touchpad)");

    /* ---- WP-10-timer_rtc: time sources ----
     * ACPI tables (FADT/HPET) are parsed above; the PIT tick is already
     * running (WP-02), so the CMOS RTC probe can resolve the FADT
     * century register and HPET/PM-timer see their ACPI bases. */
    if (driver_cmos_rtc_init() == 0)
        OC_LOG_OK2("CMOS RTC (ports 0x70/0x71, VRT set)");
    else
        OC_LOG_FAIL2("CMOS RTC: probe inconclusive (VRT not set)");
    if (driver_hpet_init() == 0)
        OC_LOG_OK2("HPET (ACPI table + MMIO caps)");
    else
        OC_LOG_FAIL2("HPET: no usable ACPI HPET table");
    /* PM timer init follows lib_config_init() below so the runtime
     * pmtimer.width override is readable (see driver_pmtimer.c). */

    /* ---- G5 boot chain marker: phase 2/4 - drivers + driver-free
     * services (VFS/ramfs need no driver; the driver families need
     * no service - the halves are independent, their order is free) */
    OC_LOG_OK2("boot chain 2/4: drivers + driver-free services");

    /* ---- 12d. WP-05: VFS + ramfs + ATA + FAT32 ---- */
    fs_vfs_init();
    OC_LOG_OK2("VFS (virtual file system)");

    fs_ramfs_init();
    OC_LOG_OK2("ramfs (in-memory file system, mounted at /)");
    klog_on_mount();   /* WP-12: dump the boot log to /var/log/kernel.log */

    block_svc_init();
    driver_block_cache_init();
    /* v2.0.0: storage bring-up is COLLECTED - each block driver file
     * declares OC_DRIVER_DECL("block", ...) and block_svc_probe_all()
     * walks them in BMDMA-first order (see services/block). */
    block_svc_probe_all();
    OC_LOG_OK2("ATA/IDE PIO driver (LBA28)");

    /* WP-10-camera_misc: SCSI core + LSI 53C895A HBA.  Runs BEFORE the
     * partition scan below so discovered disks (sda..) get their
     * MBR/GPT children; without the chip (most machines) nothing
     * attaches and the scsi command reports zero targets honestly. */
    driver_scsi_lsi_init();
    OC_LOG_OK2("virtio-blk + AHCI + NVMe drivers");

    /* ---- WP-10d: full USB stack ----
     * Class drivers register FIRST so the enumeration probes them:
     * HID keyboards/mice (boot protocol), MSC storage (BOT + SCSI,
     * lands in the blk table as usda/usdb before the partition scan
     * below), CDC-ACM + FTDI serial, UAC audio, UVC video.  Then all
     * four host controller backends probe PCI (UHCI + OHCI + EHCI +
     * XHCI) and enumerate their root hubs.  A poll kernel thread
     * keeps hot-plug, input and serial RX alive for the whole session.
     * 3.0.0-WP03 G3: the class drivers above are COLLECTED - each
     * class-driver file declares OC_DRIVER_DECL("usb", ...) and
     * driver_usb_probe_all() walks the section before probing hosts
     * (the explicit kmain registration calls are gone). */
    {
        int uh = driver_usb_probe_all();
        char line[80]; char num[12];
        strcpy(line, "USB hosts up: ");
        u64_to_str((u64)(uh > 0 ? uh : 0), num);
        strcat(line, num);
        OC_LOG_OK2(line);
    }

    /* WP-10-tui: start the resident USB poll thread - hot-plug, HID
     * and serial RX plus MSC test-unit-ready now run without a test
     * command having to drive them (closes the "comment claimed a
     * resident poller, code never started one" gap). */
    driver_usb_poll_start();

    OC_LOG_OK2("USB stack (4 HC backends, HID/MSC/serial/audio)");

    /* ---- G5 boot chain marker: phase 3/4 - driver-dependent
     * services (everything below mounts or starts ONLY after its
     * driver family probed: the volume drivers + config + A/B need
     * the block family, wireless needs mounted firmware files, the
     * net stack needs the NIC family, snd needs the snd family) */
    OC_LOG_OK2("boot chain 3/4: driver-dependent services");

    fs_fat32_init();
    fs_exfat_init();
    fs_ext4_init();
    OC_LOG_OK2("FAT32 (R/W) + exFAT (R/W) + ext4 (RO) drivers");

    /* ---- WP-09-fix5: system configuration (/etc/opencube.conf) ----
     * Mounts the FAT32 /etc volume when a disk is attached (persistent
     * config), falls back to a ramfs /etc seeded with defaults, and
     * makes sure the config file exists. */
    /* 3.0.0-WP03 G5: the config store's STORAGE is an installed
     * backend (the fs side lives in services/fs; core/lib keeps only
     * the key-value mechanism). */
    lib_config_init_with(fs_config_backend());

    /* ---- WP-10-timer_rtc: ACPI PM timer ----
     * Initialized after lib_config_init() so the pmtimer.width config
     * key is readable when the counter mask is chosen. */
    if (driver_pmtimer_init() == 0)
        OC_LOG_OK2("ACPI PM timer (FADT PM_TMR_BLK)");
    else
        OC_LOG_FAIL2("ACPI PM timer: no usable PM_TMR_BLK");

    /* ---- WP-10u: A/B slot framework + in-system update ----
     * Reads oc.slot= from the multiboot2 command line, scans the disks
     * for the A/B layout, registers the partition block devices and
     * mounts /ab/boot + /ab/a + /ab/b + /data.  For slot-B boots a
     * pessimistic bootfail marker is written here (removed again by
     * ota_update_confirm_boot() once the system is fully up). */
    ota_ab_set_boot_slot_arg(mbi.cmdline);
    driver_block_part_scan_register_all();   /* WP-10d-pre: expose <disk>pN for every
                                   partition before the A/B scan so
                                   install disks are mountable too */
    ota_ab_init();

    /* WP-10-wireless_bluetooth: 802.11 core + family probes.  Each
     * family runs its real PCI/USB enumeration; without matching
     * hardware the device stays DOWN (honest state surfaced by the
     * wifi command).  Runs after FAT mounts so firmware files under
     * /etc/firmware are loadable. */
    /* 3.0.0-WP03 G2: hand the wl framework its firmware-file provider
     * (fs reads live in the net root service now - no drivers->services
     * edge).  Installed after FAT mounts, before any family probe. */
    net_wl_fw_provider_install();
    /* 3.0.0-WP03 G3: the 802.11 families are COLLECTED - each family
     * file declares OC_DRIVER_DECL("wireless", ...) and the core
     * walks the section inside driver_wireless_init() (the explicit
     * kmain per-family register calls are gone). */
    driver_wireless_init();
    /* SDHCI hosts (real emulated hardware in QEMU via sdhci-pci) run
     * before the wireless probe so brcmfmac-SDIO cards can be bound
     * (the SDIO client drivers are collected at driver_sdhci_init
     * from their OC_DRIVER_DECL("sdio", ...) declarations).  */
    driver_sdhci_init();
    wl_device_probe_all();
    /* WP-10-tui: bridge the wireless STA into the NIC framework so
     * its RX ring feeds the TCP/IP stack (no carrier -> honest DOWN
     * link; associated -> data frames flow). */
    wl_nic_bridge_register();
    /* Bluetooth stack: HCI over the first available transport (USB
     * dongle, else the COM2 H4 UART).  Without either, the bt command
     * reports "no transport" honestly. */
    bt_stack_init();

    /* WP-10-camera_misc: camera core.  The UVC class driver is
     * collected with the other USB class drivers before host
     * enumeration (WP-03 G3); without hardware the uvc command
     * reports zero cameras honestly. */
    camera_init();
    /* WP-10-camera_misc: legacy ports (IEEE 1284 parallel, game
     * adapter) + infrared probe.  Missing hardware reports honestly. */
    parallel_init();
    parallel_probe_all();
    gameport_init();
    gameport_probe_all();
    ir_init();
    ir_probe_all();

    /* fix3 note (kept for history): core_power_suspend() stays compiled
     * (a retained kernel capability) - its command entry points left
     * with the user plane in WP-03. */
    arch_lapic_init();
    OC_LOG_OK2("LAPIC (local APIC)");

    /* ---- 12e. WP-06: Network stack ---- */
    net_init();
    net_start_timer();
    OC_LOG_OK2("network stack (e1000 + TCP/IP + socket API)");
    health_init();   /* WP-12: 1 s sampler + alarms + JSON export */
    OC_LOG_OK2("health monitor (cpu/mem/tasks/disk/net + alarms)");

    /* ---- WP-10c: sound card drivers (HDA/AC'97/SB16/ES1370/virtio/USB) ----
     * Probes all six families; those without hardware simply stay
     * unregistered.  Then the status/test commands. */
    snd_svc_init();
    if (snd_svc_probe_all() == 0) {
        char line[80]; char num[8];
        strcpy(line, "sound: ");
        u64_to_str((u64)driver_snd_num_devices(), num); strcat(line, num);
        strcat(line, " card(s) registered");
        OC_LOG_OK2(line);
    } else {
        OC_LOG_OK2("sound: no sound card present (probe OK)");
    }
    /* ---- 12f. WP-07: Disk subsystem summary ---- */
    {
        char line[120]; char n[24];
        int nd = driver_block_num_devices();
        strcpy(line, "block devices: "); u64_to_str((u64)nd, n); strcat(line, n);
        strcat(line, " registered");
        lib_log_info(line);
        driver_block_cache_stats_t cs;
        driver_block_cache_get_stats(&cs);
        strcpy(line, "disk cache: 64 slots, LRU write-back");
        lib_log_info(line);
    }
    OC_LOG_OK2("disk subsystem (block dev + cache + partition + FS)");

    /* ---- 14. WP-03: Verify PMM + heap work ---- */
    {
        mem_pmm_stats_t ps;
        mem_pmm_get_stats(&ps);
        char line[100]; char n[20];
        strcpy(line, "pmm: ");
        u64_to_str(ps.total_pages, n); strcpy(line+strlen(line), n);
        strcpy(line+strlen(line), " pages, ");
        u64_to_str(ps.free_pages, n); strcpy(line+strlen(line), n);
        strcpy(line+strlen(line), " free");
        lib_log_info(line);
    }
    {
        void *test = kmalloc(256);
        if (test) {
            memset(test, 0x55, 256);
            kfree(test);
            OC_LOG_OK2("kmalloc/kfree self-test");
        } else {
            OC_LOG_FAIL2("kmalloc/kfree self-test");
        }
    }

    /* ---- 15a. WP-08cd L1 extension self-test ---- */
    extern void ext_wp8cd_selftest(void);
    ext_wp8cd_selftest();

    /* ---- 16. Boot complete ---- */
    screen_console_putc('\n');
    OC_LOG_OK2("boot complete");

    /* ---- G5 boot chain marker: phase 4/4 - interfaces ready (the
     * crash/health/embed infrastructure arms on a fully-up system) */
    OC_LOG_OK2("boot chain 4/4: interfaces ready");

    /* ---- 16a. WP-11-crash_recovery: panic infrastructure ----
     * The core-dump buffer is allocated ONCE here - after this point
     * kmalloc may fail or the heap may corrupt, but the panic path
     * only ever touches the already-reserved buffer. The soft
     * watchdog registers its 1 s tick now but starts DISARMED (long
     * synchronous commands own the shell task and cannot kick; see
     * crash_wdt_kick / crashtest wd). */
    core_dump_alloc();
    crash_install_panic_handlers();   /* 3.0.0-WP03 G5: route the core panic slot here */
    crashlog_boot_alloc();
    crash_wdt_init();
    OC_LOG_OK2("crash recovery armed (panic + dump + crashlog)");

    /* ---- 16b. WP-09-fix5: config-driven auto update check ----
     * Runs in its own kernel thread; never blocks the boot path and
     * skips (with a log entry) when the network is not ready. */
    if (lib_config_autocheck_enabled()) {
        ota_update_check_async();
    }

    /* ---- 16c. WP-10u: confirm a slot-B boot ----
     * The system is fully up: clear the pessimistic bootfail marker
     * and promote slot B to the confirmed default (ok_B). */
    ota_update_confirm_boot();

    /* ---- 16d. 3.0.0-WP03 G4: OCIRE boot confirmation ----
     * The whole L0 is up.  On a supervised boot: write SIGNAL_OK into
     * the recovery mailbox (+boot_count), DISARM the i6300esb the
     * supervisor armed (inside its 60 s window - we are ~2 s in),
     * ARM the whole-disk write protection on the recovery disk and
     * spawn the injected log agent as the protected ocire_guard
     * process.  No-op on a normal boot. */
    ocembed_boot_confirm();

    /* ---- 17. 3.0.0-WP03 boot flow: L0 is a pure service kernel ----
     * L0 finishes init, announces readiness, and hands the machine to
     * the DEFAULT L1 entry (ring 3, declared via OC_L1_APP_DECL -
     * ush).  There is no interactive kernel shell anymore: the boot
     * task parks in the launcher's scheduler yield loop below (and in
     * the idle halt when the app exits).  The "L0 ready, waiting for
     * L1" line is the expected terminal state of the L0 console. */
    OC_LOG_OK2("L0 ready, waiting for L1");
    {
        extern void l1_launcher_boot_default(void);
        l1_launcher_boot_default();
    }

    for (;;) __asm__ volatile("hlt");
}
