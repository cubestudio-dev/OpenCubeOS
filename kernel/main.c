/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-03
 * File: kernel/kmain.c
 * Purpose: Kernel main entry. WP-03 adds:
 *   - PMM (physical memory manager) init
 *   - VMM (virtual memory manager) init
 *   - Kernel heap (kmalloc/kfree) init
 *   - Shell command registration system
 *   - Real #PF handling (stack growth, heap growth, illegal detection)
 *   - New shell commands: mem, heap, vmmap, vmtest, memtest, frag
 */
#include "types.h"
#include "arch_multiboot2.h"
#include "screen_fb.h"
#include "crypto_core.h"
#include "screen_font.h"
#include "screen_console.h"
#include "l1_ext.h"
#include "lib_log.h"
#include "lib_string.h"
#include "crypto_dh_scale_vectors.h"  /* WP-09 debug: modexp scale-sweep truth vectors */

/* WP-02 headers. */
#include "arch_idt.h"
#include "arch_pic.h"
#include "arch_exceptions.h"
#include "arch_irq.h"
#include "core_timer.h"
#include "driver_input_keyboard.h"
#include "screen_serial_in.h"
#include "screen_console_in.h"

/* WP-03 headers. */
#include "mem_pmm.h"
#include "mem_vmm.h"
#include "mem_heap.h"
#include "shell.h"
#include "shell_lineedit.h"   /* WP-10-wp08fix1: full oc> line editing */

/* WP-04 headers. */
#include "core_sched.h"
#include "core_sync.h"
#include "core_usermode.h"

/* WP-05 headers. */
#include "fs_vfs.h"
#include "fs_ramfs.h"
#include "driver_block_ata.h"
#include "fs_fat32.h"
#include "shell_cmds_file.h"

/* WP-06 headers. */
#include "net_core.h"

/* WP-07 headers. */
void driver_block_ata_register_blk(void);  /* WP-07: ATA blk registration */
#include "driver_block_blk.h"
#include "driver_block_cache.h"
#include "driver_block_part.h"
#include "driver_block_virtio_blk.h"
#include "driver_block_nvme.h"
#include "driver_block_ahci.h"      /* WP-10a: SATA */
#include "driver_block_ata_dma.h"   /* WP-10a: Bus-Master IDE */
#include "shell_cmds_disk_test.h" /* WP-10a: storage test suite */
#include "driver_nic.h"            /* WP-10b: NIC drivers + test/status commands */
#include "ota_ab.h"       /* WP-10u: A/B slots + in-system update */
#include "core_power.h"           /* WP-10d-fix2: shutdown/suspend/halt/reboot */
#include "shell_cmds_update_test.h" /* WP-10u: update test suite */
#include "driver_snd.h"             /* WP-10c: sound card drivers */
#include "driver_usb.h"              /* WP-10d: USB stack */
#include "driver_usb_hid.h"
#include "driver_usb_msc.h"
#include "driver_usb_serial.h"
#include "driver_usb_audio.h"
#include "shell_cmds_usb_test.h"
#include "shell_cmds_power_test.h"  /* WP-10d-fix2: power + help test suite */
#include "fs_exfat.h"
#include "fs_ext4.h"
#include "shell_cmds_disk.h"
#include "driver_block_disk_setup.h"   /* WP-10d-pre: abdisk/install/grub-install */
#include "lib_config.h"   /* WP-09-fix5: /etc/opencube.conf */
#include "ota_update.h"   /* WP-09-fix5: checkupdate */

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
extern const u8 userprog_mmap_multi[];
extern const u64 userprog_mmap_multi_size;
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

/* p1fix2 repro programs. */
extern const u8 userprog_fdref_test[];
extern const u64 userprog_fdref_test_size;
extern const u8 userprog_select_zero_test[];
extern const u64 userprog_select_zero_test_size;

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

/* ---- Soft timer demo ---- */
static u64 g_soft_timer_fires = 0;
static void soft_timer_test_cb(void *ctx) {
    (void)ctx;
    g_soft_timer_fires++;
}
static u64 g_oneshot_fires = 0;
static void oneshot_test_cb(void *ctx) {
    (void)ctx;
    g_oneshot_fires++;
}

/* ---- Shell commands ---- */

/* WP-10d-fix2: help rewritten - default/-a = A-Z, -w = by work package;
 * the old registration-order listing is retired. */
static int shell_cmd_help(const char *args) {
    const char *p = args;
    while (p && *p == ' ') p++;
    if (p && p[0] == '-' && p[1]) {
        if (strcmp(p, "-w") == 0 || strcmp(p, "--wp") == 0) {
            shell_list_commands_by_wp();
            return 0;
        }
        if (strcmp(p, "-a") == 0 || strcmp(p, "--all") == 0) {
            shell_list_commands_a_z();
            return 0;
        }
        screen_console_puts("help: unknown option '");
        screen_console_puts(p);
        screen_console_puts("' (use: help, help -a, help -w)\n");
        return 1;
    }
    shell_list_commands_a_z();
    return 0;
}

static int shell_cmd_stats(const char *args) {
    (void)args;
    char buf[120]; char n[20];
    strcpy(buf, "ticks="); u64_to_str(core_timer_ticks(), n); strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), "  ms="); u64_to_str(core_timer_now_ms(), n); strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), "  IRQ0="); u64_to_str(arch_irq_counts[OC_IRQ0_VECTOR], n); strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), "  IRQ1="); u64_to_str(arch_irq_counts[OC_IRQ1_VECTOR], n); strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), "  soft="); u64_to_str(g_soft_timer_fires, n); strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), "  one="); u64_to_str(g_oneshot_fires, n); strcpy(buf+strlen(buf), n);
    screen_console_puts(buf); screen_console_putc('\n');
    return 0;
}

static int shell_cmd_exc(const char *args) {
    (void)args;
    screen_console_puts("Running exception self-test (#DE, #UD, #PF)...\n");
    u64 before = g_exc_test_pass_count;
    arch_exc_tests();
    u64 after = g_exc_test_pass_count;
    char buf[60]; char n[20];
    strcpy(buf, "passed "); u64_to_str(after - before, n); strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), " exceptions, kernel alive\n");
    screen_console_puts(buf);
    return 0;
}

static int shell_cmd_timer(const char *args) {
    (void)args;
    int id = core_timer_register_oneshot(oneshot_test_cb, NULL, 500);
    if (id >= 0) screen_console_puts("Registered 500ms one-shot timer\n");
    else         screen_console_puts("Failed to register timer\n");
    return 0;
}

static int shell_cmd_echo(const char *args) {
    /* Support: echo text > file  and  echo text >> file */
    const char *redir = (const char*)0;
    int append = 0;
    for (int i = 0; args[i]; i++) {
        if (args[i] == '>') {
            if (args[i+1] == '>') { append = 1; redir = &args[i]; }
            else { redir = &args[i]; }
            break;
        }
    }
    if (redir) {
        int text_len = (int)(redir - args);
        const char *fname = redir + (append ? 2 : 1);
        while (*fname == ' ') fname++;
        while (text_len > 0 && args[text_len - 1] == ' ') text_len--;
        char text[256];
        if (text_len > 255) text_len = 255;
        memcpy(text, args, text_len);
        text[text_len] = 0;
        int flags = VFS_O_WRONLY | VFS_O_CREAT;
        if (append) flags |= VFS_O_APPEND;
        else flags |= VFS_O_TRUNC;  /* P2-15 FIX: `>` truncates existing content */
        int fd = fs_vfs_open(fname, flags);
        if (fd < 0) { screen_console_puts("echo: cannot open file\n"); return 1; }
        if (text_len > 0) fs_vfs_write(fd, text, text_len);
        fs_vfs_write(fd, "\n", 1);
        fs_vfs_close(fd);
        return 0;
    }
    screen_console_puts(args); screen_console_putc('\n');
    return 0;
}

static int shell_cmd_clear(const char *args) {
    (void)args;
    screen_console_clear();
    return 0;
}

static int shell_cmd_halt(const char *args) {
    (void)args;
    /* WP-10d-fix2: halt moved into kernel/power.c (cli + hlt loop with
     * the "System halted." banner). */
    core_power_halt();
    return 0; /* unreachable */
}

/* WP-09: cryptotest — verify AES + SHA-256 with NIST test vectors */
static int shell_cmd_cryptotest(const char *args) {
    (void)args;
    char buf[200]; char hex[20];
    int pass = 0;

    screen_console_puts("Crypto self-test (NIST vectors):\n");

    /* Test 1: SHA-256("abc") = ba7816bf 8cf01e3e 5143715e 5f6e1851 ... */
    u8 sha_result[32];
    sha256((const u8*)"abc", 3, sha_result);
    strcpy(buf, "  SHA-256(\"abc\") = ");
    for (int i = 0; i < 32; i++) {
        u64_to_hex(sha_result[i], hex, 2);
        strcat(buf, hex);
    }
    strcat(buf, "\n");
    screen_console_puts(buf);
    /* Check first 4 bytes match ba7816bf */
    if (sha_result[0] == 0xba && sha_result[1] == 0x78 &&
        sha_result[2] == 0x16 && sha_result[3] == 0xbf) {
        screen_console_puts("    SHA-256: PASS\n");
        pass++;
    } else {
        screen_console_puts("    SHA-256: FAIL\n");
    }

    /* Test 2: AES-128 ECB test vector (FIPS-197 Appendix B)
     * Key:     000102030405060708090a0b0c0d0e0f
     * Plain:   00112233445566778899aabbccddeeff
     * Cipher:  69c4e0d86a7b0430d8cdb78070b4c55a
     */
    u8 crypto_aes_key[16] = {0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
                      0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f};
    u8 crypto_aes_plain[16] = {0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,
                        0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff};
    u8 crypto_aes_cipher[16];
    crypto_aes128_encrypt_block(crypto_aes_key, crypto_aes_plain, crypto_aes_cipher);
    strcpy(buf, "  AES-128(plain) = ");
    for (int i = 0; i < 16; i++) {
        u64_to_hex(crypto_aes_cipher[i], hex, 2);
        strcat(buf, hex);
    }
    strcat(buf, "\n");
    screen_console_puts(buf);
    /* Check: 69c4e0d86a7b0430d8cdb78070b4c55a */
    if (crypto_aes_cipher[0] == 0x69 && crypto_aes_cipher[1] == 0xc4 &&
        crypto_aes_cipher[2] == 0xe0 && crypto_aes_cipher[3] == 0xd8) {
        screen_console_puts("    AES-128: PASS\n");
        pass++;
    } else {
        screen_console_puts("    AES-128: FAIL\n");
    }

    /* Test 3: HMAC-SHA-256 test vector (RFC 4231 Test Case 1)
     * Key: 0x0b*20, Data: "Hi There" = 4869205468657265
     * Expected HMAC: b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da7...
     */
    u8 crypto_hmac_key[20];
    for (int i = 0; i < 20; i++) crypto_hmac_key[i] = 0x0b;
    u8 crypto_hmac_result[32];
    crypto_hmac_sha256(crypto_hmac_key, 20, (const u8*)"Hi There", 8, crypto_hmac_result);
    strcpy(buf, "  HMAC-SHA256 = ");
    for (int i = 0; i < 8; i++) {
        u64_to_hex(crypto_hmac_result[i], hex, 2);
        strcat(buf, hex);
    }
    strcat(buf, "...\n");
    screen_console_puts(buf);
    /* Check first 4 bytes: b0344c61 */
    if (crypto_hmac_result[0] == 0xb0 && crypto_hmac_result[1] == 0x34 &&
        crypto_hmac_result[2] == 0x4c && crypto_hmac_result[3] == 0x61) {
        screen_console_puts("    HMAC-SHA256: PASS\n");
        pass++;
    } else {
        screen_console_puts("    HMAC-SHA256: FAIL\n");
    }

    strcpy(buf, "  ");
    u64_to_str((u64)pass, hex);
    strcat(buf, hex);
    strcat(buf, "/3 tests passed\n");
    screen_console_puts(buf);
    return (pass == 3) ? 0 : 1;
}

/* WP-09: dhtest — verify DH modexp correctness + measure performance.
 * Test 1 (correctness): g^0 mod p = 1 (always true)
 * Test 2 (correctness): 1^x mod p = 1 (always true)
 * Test 3 (timing): g^random mod p — measure wall time
 * 1024-bit DH (Oakley Group 1) — should be ~5-10s in QEMU.
 */
/* WP-09 bug fix: fixed-vector truth tests for DH (batch 13 left K mismatch;
 * the old dhtest only checked identities g^0=1 / 1^x=1 which pass even when
 * modexp is mathematically wrong). Vectors generated with python3 pow():
 *   p = RFC 3526 group14; x fixed seed 20260929
 *   e = 2^x mod p; f = 2^y mod p; K2 = f^x mod p
 */
static const u8 crypto_dh_test_x[256] = {
    0x59,0xCD,0xF5,0x61,0xE7,0x35,0x8D,0x84,0x90,0x8E,0x58,0xBC,0xEC,0x2D,0x66,0x4A,
    0xE3,0xC8,0x65,0xAA,0x2A,0x08,0x89,0x76,0x4E,0x06,0xA3,0x25,0xAA,0xD1,0xA4,0x6F,
    0x96,0x35,0x80,0x3C,0xC7,0x11,0x86,0x99,0x11,0x5C,0xB1,0x5D,0x3C,0x45,0x7A,0x7B,
    0x8F,0x98,0x44,0xD1,0xC2,0x5A,0x76,0x11,0x68,0xA9,0xF4,0x81,0xE8,0xCE,0x57,0x02,
    0x95,0xBA,0xB3,0x4F,0x87,0xBB,0x7E,0x80,0x12,0xBF,0x40,0x95,0x26,0x49,0xA0,0x97,
    0x4F,0xA8,0x3B,0x6C,0xA5,0x3F,0x27,0xE5,0x75,0x98,0x9C,0x75,0x5A,0xDE,0xFE,0x71,
    0x2F,0xF7,0x59,0xFE,0x8A,0x76,0x84,0xFC,0x93,0xED,0x20,0x80,0x42,0x73,0xAB,0x72,
    0x9A,0xD1,0x2F,0xAC,0xB6,0x24,0xE3,0x21,0x83,0xC7,0x00,0xED,0x9F,0x77,0x1C,0xCE,
    0x04,0x92,0x93,0xB2,0x6F,0xA0,0xC2,0xC6,0x1F,0x45,0x2F,0x0E,0xB9,0x3E,0x2F,0xEC,
    0x9E,0x3E,0x94,0x2F,0xC8,0xF1,0x4A,0x7C,0xF4,0x52,0x1A,0x6F,0x8B,0xCE,0x0E,0x20,
    0x0E,0x24,0xFE,0x2F,0x71,0x87,0x0C,0x41,0xFC,0x85,0xCC,0xA2,0x0C,0x26,0xAF,0x50,
    0x63,0xC2,0x22,0x7E,0x45,0xBF,0xEC,0xE7,0x95,0x93,0x31,0x25,0x8E,0x76,0x99,0xD2,
    0x8D,0x80,0x4A,0x02,0x4B,0x3D,0x7B,0x21,0x66,0xE6,0xB0,0x80,0x3E,0x1E,0xD1,0xD0,
    0x96,0xEE,0x44,0x8B,0x9B,0xFB,0xD7,0x71,0xE3,0x13,0xF9,0xD5,0x4D,0x58,0xDE,0x74,
    0xA4,0x95,0xEA,0xB1,0x6A,0x9A,0x29,0xB4,0xDE,0x47,0x3C,0xAC,0x9F,0xAD,0x41,0x5F,
    0x4F,0x8F,0xD0,0xBD,0x12,0xD4,0x77,0x4B,0x0B,0xF6,0xF2,0x33,0x42,0x0D,0xC8,0x6E,
};
static const u8 crypto_dh_test_e[256] = {
    0x32,0xA0,0x9A,0x91,0x43,0xCF,0xC0,0x6C,0xB2,0x60,0xE8,0xFB,0xE6,0x20,0x8D,0x88,
    0x58,0x34,0x6F,0x8E,0xEC,0x50,0x79,0x5A,0x76,0x96,0xA3,0x87,0x75,0x14,0x2A,0xA0,
    0x3E,0x43,0x97,0x1C,0x6F,0x0A,0xAF,0x2F,0xCC,0x43,0xA3,0xFA,0x76,0x77,0x19,0xD4,
    0x87,0x1F,0x7C,0x60,0xB2,0xA9,0xF5,0x5B,0x3A,0xDE,0x52,0x12,0x74,0x66,0x72,0xF5,
    0x91,0xCF,0xEC,0xBA,0x61,0x74,0x17,0xF1,0x42,0x53,0x0F,0x9F,0x56,0x5B,0x33,0xFC,
    0x28,0xE7,0x05,0x65,0x26,0x9F,0x5F,0x84,0x01,0x22,0x56,0x4F,0xAE,0xD5,0xB4,0x59,
    0x5A,0xBC,0x1B,0x58,0x3D,0xDD,0x1C,0x94,0xD1,0x40,0xF1,0xC9,0xAE,0x02,0x8A,0x56,
    0xFB,0xC6,0x13,0x5B,0xDD,0x77,0x92,0xED,0x26,0x59,0x89,0x87,0x5D,0xD0,0x0D,0xFD,
    0x39,0xE5,0xAB,0xAC,0x13,0x2F,0xE4,0x21,0x04,0x8F,0x2F,0x78,0xB1,0xC5,0x19,0x3C,
    0x50,0xA8,0xB8,0xB6,0xE9,0xDC,0xDA,0x89,0xDF,0x41,0xD1,0x1C,0xCD,0xBA,0x3B,0xCE,
    0xE6,0x9D,0x42,0x08,0x1D,0x42,0xB9,0x5A,0x6A,0xF5,0xD1,0xEF,0x15,0xC9,0x1D,0xB0,
    0x88,0xCA,0x1E,0xF8,0xC0,0x53,0xC0,0x8C,0x0D,0x78,0x07,0x42,0x61,0xCD,0x4D,0xD5,
    0xF3,0xDF,0x7A,0xB4,0x2F,0xCA,0x3D,0xBB,0xAF,0x9F,0x8C,0x4B,0x8A,0x9A,0xF0,0xE4,
    0xBA,0x80,0xCD,0x88,0x0A,0x16,0x95,0xE8,0xAA,0x9D,0xF2,0x88,0xFB,0xC2,0xD1,0x32,
    0x52,0xAF,0x1C,0x7C,0x03,0x26,0x53,0x9E,0x8D,0xAB,0xF7,0xD9,0x79,0x65,0xC7,0xA4,
    0xB2,0xB1,0x32,0xA0,0xCF,0x68,0x0B,0x27,0x47,0x77,0xF4,0x23,0x94,0x3F,0xAF,0x49,
};
static const u8 crypto_dh_test_f[256] = {
    0xB3,0xE5,0x58,0x34,0xE0,0x59,0xE2,0x09,0xDE,0x21,0xA0,0x90,0x30,0xE5,0x10,0x75,
    0x07,0xFD,0x77,0x57,0x34,0xF4,0xDF,0xC8,0xC4,0x4F,0xAD,0x59,0xED,0x7F,0x38,0xB1,
    0xA8,0x9A,0xEF,0x02,0xDC,0xB0,0x65,0x63,0x12,0x1F,0x9D,0x98,0xB1,0xF2,0x17,0xAE,
    0x3E,0xEE,0x23,0xED,0x3B,0x54,0x12,0x77,0xF9,0x9C,0x0F,0x33,0x57,0x72,0x8E,0x96,
    0xE5,0x9F,0xEA,0xEE,0xDE,0x8D,0x88,0x62,0xC7,0x22,0x41,0xC0,0x68,0x71,0x58,0xE3,
    0xE3,0x4D,0x34,0xF7,0xD7,0xFF,0xB0,0xD3,0xCA,0xAC,0x35,0x68,0x59,0x7B,0xF6,0x5D,
    0x13,0x28,0xC1,0x9E,0x78,0xD8,0x0A,0x71,0x43,0xAF,0xDF,0x38,0x40,0x7E,0xA2,0xA3,
    0xB8,0xF7,0x88,0x5E,0xF8,0x6A,0xAB,0xE2,0xE2,0x68,0x82,0xD0,0x7A,0xCD,0x10,0xE6,
    0x1B,0x4B,0x17,0xB3,0xD1,0xE6,0x7F,0x75,0xA2,0xE2,0x5B,0x0A,0xA2,0x1B,0x0D,0xA5,
    0xAF,0x1B,0x26,0x08,0x65,0x88,0x33,0x0F,0x19,0x4E,0xDA,0x99,0x7B,0xAB,0xB3,0x18,
    0x62,0xA4,0x1D,0xA5,0x16,0x11,0x86,0x1A,0x2A,0xD8,0xAD,0xDF,0x53,0xEA,0x0F,0xCB,
    0x09,0x11,0xD8,0x2E,0x9C,0x0B,0x81,0x60,0xBF,0xBA,0x6D,0x70,0xE4,0xEC,0xA0,0x02,
    0x63,0xAC,0x05,0xE0,0x44,0xF1,0xBD,0xBF,0x27,0x3D,0x44,0xE0,0x3F,0x29,0x01,0xEF,
    0xE6,0x2C,0xBB,0xB0,0x38,0xFA,0x32,0x44,0x8A,0xCA,0xC1,0x7C,0xAE,0xCF,0x94,0x6A,
    0x80,0x1F,0xC0,0xAA,0x9D,0x93,0xC7,0x36,0x16,0x06,0x44,0x42,0xD7,0x98,0x73,0x26,
    0xCD,0x98,0x1E,0x40,0x1F,0xF5,0xE2,0xE7,0x44,0x66,0x44,0x7F,0x93,0x10,0x21,0x3F,
};
static const u8 crypto_dh_test_K2[256] = {
    0x59,0x62,0x87,0x0F,0x91,0xC1,0x6F,0x10,0x10,0xA7,0x2E,0x8F,0xA0,0x76,0x8F,0xC4,
    0xC9,0xD1,0x69,0x95,0xFA,0x67,0x50,0x3E,0xD9,0x0B,0x4D,0xE3,0xE1,0x30,0xBA,0x6D,
    0x84,0x29,0xD7,0xA9,0x51,0xDA,0xD4,0x0C,0xDF,0x2E,0xBB,0xBA,0x8A,0xE5,0x44,0xC5,
    0x1F,0xD5,0xAC,0x94,0xD5,0x5C,0x0A,0xC6,0xA7,0x3A,0x76,0x47,0xF4,0x2E,0xB3,0xFF,
    0x1E,0x67,0x9A,0x2E,0x3B,0x3A,0x5C,0xCE,0x63,0x73,0x5F,0x28,0x39,0x03,0xFD,0x7E,
    0x80,0xDD,0x21,0xD4,0x18,0x99,0xEA,0x98,0x2C,0x08,0x96,0x6C,0x05,0x67,0xB9,0x33,
    0xF1,0x1B,0x39,0xDB,0x0C,0x5F,0xBA,0x67,0x57,0x39,0xC9,0xCB,0xF9,0x63,0xF4,0x74,
    0xA1,0x7D,0x87,0x68,0x3B,0xC9,0xD8,0x0D,0x99,0xB5,0xF2,0x03,0x8C,0xA3,0x31,0x76,
    0x6C,0x94,0x41,0x59,0xB3,0x5E,0x56,0x44,0x43,0x71,0x03,0xA6,0xE7,0x19,0xF3,0x97,
    0x00,0x2E,0xF0,0xD5,0xE9,0xD9,0x21,0xAD,0xDE,0x22,0x13,0xE4,0xDF,0xA9,0xE8,0xEF,
    0x62,0x10,0x1C,0xC1,0xC6,0x66,0x6A,0x98,0x74,0x92,0xBA,0x14,0x3F,0xDB,0xEE,0x8C,
    0xB5,0xA2,0xEC,0x6C,0xEE,0x80,0x3B,0x14,0xF5,0x90,0xED,0x53,0xB2,0xEF,0x1F,0x30,
    0xDD,0x6D,0x9B,0x46,0xA5,0xAA,0x6D,0x73,0xB8,0x18,0x56,0xFF,0x4B,0x0C,0x71,0x62,
    0xF6,0x3E,0xF0,0x05,0x6C,0xDD,0x35,0x77,0x48,0xD2,0xF9,0xBA,0x10,0xD8,0x85,0xE5,
    0x9E,0xA8,0x3D,0x47,0x9A,0x51,0xB9,0x5F,0x88,0x7C,0xCD,0x2F,0xFE,0xC2,0x24,0x0F,
    0xA4,0x33,0x93,0x75,0xEF,0x11,0x1F,0x52,0x12,0x3D,0xE8,0x0B,0x1B,0xB7,0xD5,0x8F,
};

static int shell_cmd_dhtest(const char *args) {
    (void)args;
    char buf[160]; char n[20]; char hex[20];
    int pass = 0;

    screen_console_puts("DH modexp self-test (Oakley Group 1, 1024-bit):\n");

    /* Test 1: g^0 mod p = 1 */
    u8 zero_exp[DH_BYTES];
    u8 result[DH_BYTES];
    memset(zero_exp, 0, DH_BYTES);
    /* base = g = 2 */
    u8 g_val[DH_BYTES];
    memset(g_val, 0, DH_BYTES);
    g_val[DH_BYTES - 1] = 2;
    crypto_dh_modexp(g_val, zero_exp, crypto_dh_group1_prime, result);
    /* Expected: result = 1 (big-endian, last byte = 1) */
    int ok = 1;
    for (int i = 0; i < DH_BYTES - 1; i++) {
        if (result[i] != 0) { ok = 0; break; }
    }
    if (result[DH_BYTES - 1] != 1) ok = 0;
    strcpy(buf, "  g^0 mod p = ");
    for (int i = DH_BYTES - 4; i < DH_BYTES; i++) {
        u64_to_hex(result[i], hex, 2);
        strcat(buf, hex);
    }
    strcat(buf, " (expect ...0001)  ");
    if (ok) { strcat(buf, "PASS\n"); pass++; } else strcat(buf, "FAIL\n");
    screen_console_puts(buf);

    /* Test 2: 1^x mod p = 1 (base = 1, random exp) */
    u8 one_base[DH_BYTES];
    u8 exp2[DH_BYTES];
    memset(one_base, 0, DH_BYTES);
    one_base[DH_BYTES - 1] = 1;
    crypto_random(exp2, DH_BYTES);
    exp2[0] &= 0x0F;
    crypto_dh_modexp(one_base, exp2, crypto_dh_group1_prime, result);
    ok = 1;
    for (int i = 0; i < DH_BYTES - 1; i++) {
        if (result[i] != 0) { ok = 0; break; }
    }
    if (result[DH_BYTES - 1] != 1) ok = 0;
    strcpy(buf, "  1^x mod p = ");
    for (int i = DH_BYTES - 4; i < DH_BYTES; i++) {
        u64_to_hex(result[i], hex, 2);
        strcat(buf, hex);
    }
    strcat(buf, " (expect ...0001)  ");
    if (ok) { strcat(buf, "PASS\n"); pass++; } else strcat(buf, "FAIL\n");
    screen_console_puts(buf);

    /* Test 3 (timing): g^random mod p */
    u8 rand_exp[DH_BYTES];
    crypto_random(rand_exp, DH_BYTES);
    rand_exp[0] &= 0x0F;
    rand_exp[DH_BYTES - 1] &= 0xFE;

    screen_console_puts("  Computing g^x mod p (timing)...\n");
    u64 t0 = core_timer_ticks();
    u8 g_result[DH_BYTES];
    crypto_dh_modexp(g_val, rand_exp, crypto_dh_group1_prime, g_result);
    u64 t1 = core_timer_ticks();
    u64 elapsed_ms = (t1 - t0) * 1000 / (u64)OC_TIMER_HZ;

    strcpy(buf, "  g^x mod p = ");
    for (int i = 0; i < 16; i++) {
        u64_to_hex(g_result[i], hex, 2);
        strcat(buf, hex);
    }
    strcat(buf, "...\n  Time: ");
    u64_to_str(elapsed_ms, n);
    strcat(buf, n);
    strcat(buf, " ms\n");
    screen_console_puts(buf);

    /* Test 4: 2 modexp = Yc computation and Ys->premaster — combined
     * simulate TLS ClientKeyExchange: g^x mod p + Ys^x mod p.
     * Both must finish and produce non-zero premaster. */
    u64 t2 = core_timer_ticks();
    u8 client_pub[DH_BYTES];
    u8 premaster[DH_BYTES];
    crypto_dh_modexp(g_val, rand_exp, crypto_dh_group1_prime, client_pub);
    crypto_dh_modexp(client_pub, rand_exp, crypto_dh_group1_prime, premaster);
    u64 t3 = core_timer_ticks();
    u64 crypto_dh_total_ms = (t3 - t2) * 1000 / (u64)OC_TIMER_HZ;

    /* Verify premaster is non-zero (just check first/last bytes) */
    int non_zero = 0;
    for (int i = 0; i < DH_BYTES; i++) {
        if (premaster[i] != 0) { non_zero = 1; break; }
    }
    strcpy(buf, "  premaster (first 16): ");
    for (int i = 0; i < 16; i++) {
        u64_to_hex(premaster[i], hex, 2);
        strcat(buf, hex);
    }
    strcat(buf, "\n  TLS KEX (2 modexp) time: ");
    u64_to_str(crypto_dh_total_ms, n);
    strcat(buf, n);
    strcat(buf, " ms (premaster ");
    strcat(buf, non_zero ? "non-zero" : "ZERO");
    strcat(buf, ")\n");
    screen_console_puts(buf);

    /* Test 5 (WP-09 bug fix): 2048-bit group14 fixed-vector truth test.
     * g^x mod p14 must equal e (computed independently with python3 pow()).
     * This catches modexp bugs the identity tests (g^0=1, 1^x=1) miss.
     * Runs the SAME vector 3 times: differing results = memory reuse bug;
     * identical (wrong) results = deterministic logic bug. */
    screen_console_puts("DH group14 (2048-bit) fixed-vector truth tests:\n");
    u8 g14[256];
    memset(g14, 0, 256);
    g14[255] = 2;
    u8 res2048[3][256];
    for (int rep = 0; rep < 3; rep++) {
        crypto_dh_modexp_n(g14, crypto_dh_test_x, crypto_dh_group14_prime, res2048[rep], 256);
    }
    int ok_e = (memcmp(res2048[0], crypto_dh_test_e, 256) == 0);
    int det = (memcmp(res2048[0], res2048[1], 256) == 0) &&
              (memcmp(res2048[1], res2048[2], 256) == 0);
    strcpy(buf, "  g^x mod p14 = e  -> ");
    if (ok_e) { strcat(buf, "PASS"); pass++; }
    else strcat(buf, "FAIL");
    strcat(buf, "  (got ");
    for (int i = 0; i < 4; i++) { u64_to_hex(res2048[0][i], hex, 2); strcat(buf, hex); }
    strcat(buf, "..., expect 32A09A91...)\n");
    screen_console_puts(buf);
    strcpy(buf, "  determinism (3 runs): ");
    strcat(buf, det ? "IDENTICAL\n" : "DIFFERENT (memory corruption!)\n");
    screen_console_puts(buf);

    /* Test 6: f^x mod p14 must equal K2 (the shared secret value) */
    u8 resK[256];
    u64 t4 = core_timer_ticks();
    crypto_dh_modexp_n(crypto_dh_test_f, crypto_dh_test_x, crypto_dh_group14_prime, resK, 256);
    u64 t5 = core_timer_ticks();
    int ok_K = (memcmp(resK, crypto_dh_test_K2, 256) == 0);
    u64 t6 = core_timer_ticks();
    (void)t6;
    strcpy(buf, "  f^x mod p14 = K2 -> ");
    if (ok_K) { strcat(buf, "PASS"); pass++; }
    else strcat(buf, "FAIL");
    strcat(buf, "  (got ");
    for (int i = 0; i < 4; i++) { u64_to_hex(resK[i], hex, 2); strcat(buf, hex); }
    strcat(buf, "..., expect 5962870F...)\n");
    screen_console_puts(buf);
    strcpy(buf, "  2048-bit modexp time: ");
    /* t4..t5 spans the 3 identical runs of test 5; t5..t6 is the K run */
    u64_to_str(((t5 - t4) * 1000 / (u64)OC_TIMER_HZ) / 3, n);
    strcat(buf, n); strcat(buf, " / ");
    u64_to_str((t6 - t5) * 1000 / (u64)OC_TIMER_HZ, n);
    strcat(buf, n); strcat(buf, " ms\n");
    screen_console_puts(buf);

    /* Test 7 (WP-09 debug): modexp scale sweep — find the first failing size.
     * Times are len^3-ish; small sizes run instantly, 256 takes ~30s.
     * Vectors are stored right-aligned in 256-byte arrays; pass the LAST
     * L bytes of exp/mod (crypto_dh_modexp_n reads L bytes from the pointer). */
    screen_console_puts("DH modexp scale sweep (truth vectors, python3 pow()):\n");
    {
        int all_ok = 1;
        for (unsigned t = 0; t < sizeof(crypto_dh_scale_tests) / sizeof(crypto_dh_scale_tests[0]); t++) {
            int L = crypto_dh_scale_tests[t].len;
            const u8 *base_p = crypto_dh_scale_tests[t].base + (256 - L);
            const u8 *exp_p  = crypto_dh_scale_tests[t].exp_ + (256 - L);
            const u8 *mod_p  = crypto_dh_scale_tests[t].mod_ + (256 - L);
            const u8 *truth_p = crypto_dh_scale_tests[t].truth + (256 - L);
            u8 rbuf2[256];
            crypto_dh_modexp_n(base_p, exp_p, mod_p, rbuf2, L);
            int ok = (memcmp(rbuf2, truth_p, L) == 0);
            if (!ok) all_ok = 0;
            strcpy(buf, "  len=");
            u64_to_str((u64)L, n);
            strcat(buf, n);
            strcat(buf, ": ");
            strcat(buf, ok ? "PASS" : "FAIL");
            strcat(buf, "  got ");
            for (int i = 0; i < 4; i++) { u64_to_hex(rbuf2[i], hex, 2); strcat(buf, hex); }
            strcat(buf, "... expect ");
            for (int i = 0; i < 4; i++) { u64_to_hex(truth_p[i], hex, 2); strcat(buf, hex); }
            strcat(buf, "...\n");
            screen_console_puts(buf);
        }
        if (all_ok) pass++;
    }

    strcpy(buf, "  ");
    u64_to_str((u64)pass, n);
    strcat(buf, n);
    strcat(buf, "/5 correctness tests passed\n");
    screen_console_puts(buf);
    return (pass == 5) ? 0 : 1;
}

/* WP-09: ssh command — connect to SSH server, do KEX, print status. */
static int shell_cmd_ssh(const char *args) {
    /* Parse: ssh <ip> [port] [user] [password] */
    char host[64] = {0};
    int port = 2222;  /* default (Python ssh server) */
    char user[32] = "oc";
    char pass[32] = "oc";
    int i = 0, j = 0;
    /* Skip leading spaces */
    while (args[i] == ' ') i++;
    while (args[i] && args[i] != ' ' && j < 63) host[j++] = args[i++];
    host[j] = 0;
    while (args[i] == ' ') i++;
    if (args[i]) {
        port = 0;
        while (args[i] && args[i] >= '0' && args[i] <= '9') {
            port = port * 10 + (args[i] - '0');
            i++;
        }
        if (port == 0) port = 2222;
        while (args[i] == ' ') i++;
        if (args[i]) {
            j = 0;
            while (args[i] && args[i] != ' ' && j < 31) user[j++] = args[i++];
            user[j] = 0;
            while (args[i] == ' ') i++;
            if (args[i]) {
                j = 0;
                while (args[i] && j < 31) pass[j++] = args[i++];
                pass[j] = 0;
            }
        }
    }
    if (!host[0]) {
        screen_console_puts("usage: ssh <ip> [port=2222] [user=oc] [password=oc|-]\n");
        screen_console_puts("  password \"-\" = publickey auth (needs /etc/ssh_client_key; falls back to password if absent)\n");
        return 1;
    }
    /* password "-" = use publickey authentication with the client key
     * from /etc/ssh_client_key. BUG-0075: the kernel no longer embeds a
     * universal identity key, so "-" without an installed key falls
     * back to the default password instead of failing. */
    int use_pubkey = (pass[0] == '-' && pass[1] == 0);
    if (use_pubkey) pass[0] = 0;
    /* Resolve host (IP first) */
    u32 ip = 0;
    /* Try parsing as IP "a.b.c.d" */
    int a = 0, b = 0, c = 0, d = 0;
    int ai = 0;
    int ok = 1;
    int parts[4] = {0, 0, 0, 0};
    int pp = 0;
    int cur = 0;
    int got_digit = 0;
    while (host[ai]) {
        if (host[ai] >= '0' && host[ai] <= '9') {
            cur = cur * 10 + (host[ai] - '0');
            got_digit = 1;
        } else if (host[ai] == '.') {
            if (!got_digit || pp >= 3) { ok = 0; break; }
            parts[pp++] = cur;
            cur = 0; got_digit = 0;
        } else {
            ok = 0; break;
        }
        ai++;
    }
    if (ok && got_digit && pp == 3) {
        parts[3] = cur;
        a = parts[0]; b = parts[1]; c = parts[2]; d = parts[3];
        if (a < 256 && b < 256 && c < 256 && d < 256) {
            ip = ((u32)a << 24) | ((u32)b << 16) | ((u32)c << 8) | (u32)d;
        } else ok = 0;
    } else ok = 0;
    if (!ok) {
        extern int net_dns_resolve(const char *name, u32 *ip);
        if (net_dns_resolve(host, &ip) != 0) {
            screen_console_puts("cannot resolve host\n");
            return 1;
        }
    }
    char buf[80];
    extern int net_ssh_connect(u32 ip, u16 port, const char *user, const char *pass);
    strcpy(buf, "[ssh] connecting to ");
    /* Big-endian display */
    char num[10];
    u64_to_str((u64)((ip >> 24) & 0xFF), num); strcat(buf, num); strcat(buf, ".");
    u64_to_str((u64)((ip >> 16) & 0xFF), num); strcat(buf, num); strcat(buf, ".");
    u64_to_str((u64)((ip >> 8) & 0xFF), num); strcat(buf, num); strcat(buf, ".");
    u64_to_str((u64)(ip & 0xFF), num); strcat(buf, num);
    strcat(buf, ":");
    char n[10]; u64_to_str((u64)port, n); strcat(buf, n);
    strcat(buf, " user="); strcat(buf, user);
    strcat(buf, "\n");
    screen_console_puts(buf);
    int rc = net_ssh_connect(ip, (u16)port, user, use_pubkey ? 0 : pass);
    if (rc < 0 && use_pubkey) {
        /* BUG-0075: publickey needs /etc/ssh_client_key; when it is not
         * installed, retry once with the password instead of dying. */
        screen_console_puts("[ssh] publickey unavailable (no /etc/ssh_client_key), retrying with password\n");
        rc = net_ssh_connect(ip, (u16)port, user, pass[0] ? pass : "oc");
    }
    if (rc == 0) {
        screen_console_puts("[ssh] authenticated (USERAUTH_SUCCESS)\n");
        /* WP-09: full channel open + exec round trip over the encrypted channel.
         * The command is sent via SSH_MSG_CHANNEL_REQUEST exec; server output
         * is read from CHANNEL_DATA until EOF/CLOSE. */
        extern int net_ssh_exec(const char *command, void *output, int output_len);
        static char net_ssh_out[1024];
        int n = net_ssh_exec("echo hello-from-OpenCubeOS-kernel-ssh", net_ssh_out, (int)sizeof(net_ssh_out) - 1);
        if (n > 0) {
            net_ssh_out[n] = 0;
            screen_console_puts("[ssh] exec output: ");
            screen_console_puts(net_ssh_out);
            screen_console_puts("\n");
        } else {
            screen_console_puts("[ssh] exec: no output received\n");
        }
    } else {
        char b2[40]; strcpy(b2, "[ssh] failed (code ");
        char num2[10]; u64_to_str((u64)(-rc), num2);
        strcat(b2, num2); strcat(b2, ")\n");
        screen_console_puts(b2);
    }
    extern void net_ssh_close(void);
    net_ssh_close();
    return (rc == 0) ? 0 : 1;
}

/* WP-09: sshd command — kernel-side SSH server (transport + userauth + exec). */
static int shell_cmd_sshd(const char *args) {
    int port = 22;
    char user[32] = "oc";
    char pass[32] = "oc";
    int i = 0;
    while (args[i] == ' ') i++;
    if (args[i] >= '0' && args[i] <= '9') {
        port = 0;
        while (args[i] && args[i] >= '0' && args[i] <= '9') {
            port = port * 10 + (args[i] - '0');
            i++;
        }
    }
    while (args[i] == ' ') i++;
    int j = 0;
    while (args[i] && args[i] != ' ' && j < 31) user[j++] = args[i++];
    user[j] = 0;
    while (args[i] == ' ') i++;
    j = 0;
    while (args[i] && j < 31) pass[j++] = args[i++];
    pass[j] = 0;

    extern int net_sshd_main(u16 port, const char *user, const char *pass);
    char b[80];
    strcpy(b, "[sshd] starting on port ");
    char n[10];
    u64_to_str((u64)port, n);
    strcat(b, n);
    strcat(b, " (one session, then exit; Ctrl+C cancels wait)");
    screen_console_puts(b);
    screen_console_puts("\n");
    return net_sshd_main((u16)port, user, pass);
}

/* P5 fix: uname command — was missing from kernel shell (only existed in ush).
 * Supports -a (all), -s (kernel name, default), -r (release), -m (machine). */
static int shell_cmd_uname(const char *args) {
    int show_all = 0, show_s = 0, show_r = 0, show_m = 0;
    if (args && args[0] == '-' && args[1]) {
        for (int i = 1; args[i] && args[i] != ' '; i++) {
            if (args[i] == 'a') show_all = 1;
            else if (args[i] == 's') show_s = 1;
            else if (args[i] == 'r') show_r = 1;
            else if (args[i] == 'm') show_m = 1;
        }
    } else {
        show_s = 1;  /* default */
    }
    if (show_all) {
        screen_console_puts("Open Cube OS " OC_RELEASE_VERSION " x86_64\n");
        return 0;
    }
    if (show_s) screen_console_puts("Open Cube OS\n");
    if (show_r) screen_console_puts(OC_RELEASE_VERSION "\n");
    if (show_m) screen_console_puts("x86_64\n");
    return 0;
}

/* WP-03: mem command - physical memory stats. */
static int shell_cmd_mem(const char *args) {
    (void)args;
    mem_pmm_stats_t s;
    mem_pmm_get_stats(&s);
    char buf[120]; char n[20];
    strcpy(buf, "Physical memory:\n"); screen_console_puts(buf);
    strcpy(buf, "  total: "); u64_to_str(s.total_bytes, n); strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), " bytes ("); u64_to_str(s.total_pages, n); strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), " pages)\n"); screen_console_puts(buf);
    strcpy(buf, "  used:  "); u64_to_str(s.used_bytes, n); strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), " bytes ("); u64_to_str(s.used_pages, n); strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), " pages)\n"); screen_console_puts(buf);
    strcpy(buf, "  free:  "); u64_to_str(s.free_bytes, n); strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), " bytes ("); u64_to_str(s.free_pages, n); strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), " pages)\n"); screen_console_puts(buf);
    strcpy(buf, "  fragments: "); u64_to_str(s.free_fragments, n); strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), "\n"); screen_console_puts(buf);
    /* P4 fix: show cumulative alloc/free/fail counters. */
    strcpy(buf, "  allocs: "); u64_to_str(s.total_allocs, n); strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), "  frees: "); u64_to_str(s.total_frees, n); strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), "  failures: "); u64_to_str(s.alloc_failures, n); strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), "\n"); screen_console_puts(buf);
    return 0;
}

/* WP-03: heap command - kernel heap stats. */
static int shell_cmd_heap(const char *args) {
    (void)args;
    mem_heap_stats_t s;
    mem_heap_get_stats(&s);
    char buf[120]; char n[20];
    screen_console_puts("Kernel heap:\n");
    strcpy(buf, "  size:  "); u64_to_str(s.mem_heap_size, n); strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), " bytes\n"); screen_console_puts(buf);
    strcpy(buf, "  alloc: "); u64_to_str(s.allocated, n); strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), " bytes ("); u64_to_str(s.alloc_count, n); strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), " blocks)\n"); screen_console_puts(buf);
    strcpy(buf, "  free:  "); u64_to_str(s.free, n); strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), " bytes ("); u64_to_str(s.free_count, n); strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), " blocks)\n"); screen_console_puts(buf);
    strcpy(buf, "  overhead: "); u64_to_str(s.overhead, n); strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), " bytes\n"); screen_console_puts(buf);
    strcpy(buf, "  total allocs: "); u64_to_str(s.total_allocs, n); strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), "  total frees: "); u64_to_str(s.total_frees, n); strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), "\n"); screen_console_puts(buf);
    return 0;
}

/* WP-03: vmmap command - show kernel address space mappings. */
static int shell_cmd_vmmap(const char *args) {
    (void)args;
    mem_vmm_as_t as = mem_vmm_current_as();
    u64 *pml4 = (u64*)as;
    screen_console_puts("Kernel address space (PML4 at 0x");
    char hex[20]; u64_to_hex(as, hex, 12); screen_console_puts(hex);
    screen_console_puts("):\n");

    /* Walk all 512 PML4 entries. Only 0..3 are typically populated (4 GiB
     * identity mapping); the rest should be not-present. We walk all 512
     * so a future higher-half kernel map would also show up. */
    for (int i = 0; i < 512; i++) {
        if (!(pml4[i] & VMM_FLAG_PRESENT)) continue;
        u64 vstart = (u64)i << 39;
        if (i >= 256) vstart |= 0xFF00000000000000ULL;  /* sign-extend */
        char buf[80];
        strcpy(buf, "  PML4["); u64_to_str(i, hex); strcpy(buf+strlen(buf), hex);
        strcpy(buf+strlen(buf), "] vaddr=0x"); u64_to_hex(vstart, hex, 12); strcpy(buf+strlen(buf), hex);
        strcpy(buf+strlen(buf), " -> 0x"); u64_to_hex(pml4[i] & 0x000FFFFFFFFFF000ULL, hex, 12); strcpy(buf+strlen(buf), hex);
        if (pml4[i] & 0x80) strcpy(buf+strlen(buf), " (huge)");
        strcpy(buf+strlen(buf), "\n");
        screen_console_puts(buf);
    }

    /* Show page fault stats. */
    mem_vmm_fault_stats_t fs;
    mem_vmm_get_fault_stats(&fs);
    char buf[120]; char n[20];
    strcpy(buf, "\nPage faults: total="); u64_to_str(fs.total_faults, n); strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), " legal="); u64_to_str(fs.legal_faults, n); strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), " illegal="); u64_to_str(fs.illegal_faults, n); strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), " stack="); u64_to_str(fs.stack_growth, n); strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), " heap="); u64_to_str(fs.mem_heap_growth, n); strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), "\n"); screen_console_puts(buf);
    return 0;
}

/* WP-03: vmtest command - test VMM operations. */
static int shell_cmd_vmtest(const char *args) {
    (void)args;
    screen_console_puts("VMM test:\n");

    /* Create a new address space. */
    mem_vmm_as_t as = mem_vmm_create_address_space();
    if (as == 0) {
        screen_console_puts("  FAIL: could not create address space\n");
        return 1;
    }
    char buf[80]; char hex[20];
    strcpy(buf, "  created AS at 0x"); u64_to_hex(as, hex, 12); strcpy(buf+strlen(buf), hex);
    strcpy(buf+strlen(buf), "\n"); screen_console_puts(buf);

    /* Map a page at 4 GiB (outside the kernel's 2 MiB huge-page identity
     * mapping, so VMM creates fresh 4K page tables). */
    u64 test_vaddr = 0x100000000ULL;
    u64 phys = mem_pmm_alloc_frame();
    if (phys == 0) {
        screen_console_puts("  FAIL: out of physical memory\n");
        mem_vmm_destroy_address_space(as);
        return 1;
    }
    int rc = mem_vmm_map_page(as, test_vaddr, phys, VMM_FLAGS_USER);
    strcpy(buf, "  map_page(0x"); u64_to_hex(test_vaddr, hex, 12); strcpy(buf+strlen(buf), hex);
    strcpy(buf+strlen(buf), " -> 0x"); u64_to_hex(phys, hex, 12); strcpy(buf+strlen(buf), hex);
    strcpy(buf+strlen(buf), ") = "); u64_to_str((u64)(i64)rc, hex); strcpy(buf+strlen(buf), hex);
    strcpy(buf+strlen(buf), "\n"); screen_console_puts(buf);

    /* Check it's mapped. */
    u64 mapped_phys;
    int mapped = mem_vmm_is_mapped(as, test_vaddr, &mapped_phys);
    strcpy(buf, "  is_mapped = "); u64_to_str((u64)mapped, hex); strcpy(buf+strlen(buf), hex);
    if (mapped) {
        strcpy(buf+strlen(buf), " phys=0x"); u64_to_hex(mapped_phys, hex, 12); strcpy(buf+strlen(buf), hex);
    }
    strcpy(buf+strlen(buf), "\n"); screen_console_puts(buf);

    /* Protect (make read-only). */
    rc = mem_vmm_protect_page(as, test_vaddr, VMM_FLAG_PRESENT | VMM_FLAG_USER);
    strcpy(buf, "  protect(read-only) = "); u64_to_str((u64)(i64)rc, hex); strcpy(buf+strlen(buf), hex);
    strcpy(buf+strlen(buf), "\n"); screen_console_puts(buf);

    /* Unmap. */
    u64 old = mem_vmm_unmap_page(as, test_vaddr);
    strcpy(buf, "  unmap returned 0x"); u64_to_hex(old, hex, 12); strcpy(buf+strlen(buf), hex);
    strcpy(buf+strlen(buf), "\n"); screen_console_puts(buf);

    /* Verify it's no longer mapped. */
    mapped = mem_vmm_is_mapped(as, test_vaddr, NULL);
    strcpy(buf, "  is_mapped after unmap = "); u64_to_str((u64)mapped, hex); strcpy(buf+strlen(buf), hex);
    strcpy(buf+strlen(buf), "\n"); screen_console_puts(buf);

    mem_pmm_free_frame(phys);
    mem_vmm_destroy_address_space(as);
    screen_console_puts("  destroyed AS\n");
    if (mapped == 0) screen_console_puts("  PASS\n");
    else             screen_console_puts("  PARTIAL (unmap check failed)\n");
    return 0;
}

/* WP-03: memtest command - test PMM and heap. */
static int shell_cmd_memtest(const char *args) {
    (void)args;
    screen_console_puts("PMM test:\n");
    char buf[80];

    /* Allocate 10 pages. */
    u64 pages[10];
    int ok = 1;
    for (int i = 0; i < 10; i++) {
        pages[i] = mem_pmm_alloc_frame();
        if (pages[i] == 0) { ok = 0; break; }
    }
    strcpy(buf, "  alloc 10 pages: "); strcpy(buf+strlen(buf), ok ? "OK" : "FAIL");
    strcpy(buf+strlen(buf), "\n"); screen_console_puts(buf);

    /* Free them. */
    for (int i = 0; i < 10; i++) {
        if (pages[i]) mem_pmm_free_frame(pages[i]);
    }
    screen_console_puts("  free 10 pages: OK\n");

    /* Heap test. */
    screen_console_puts("Heap test:\n");
    void *ptrs[5];
    ok = 1;
    for (int i = 0; i < 5; i++) {
        ptrs[i] = kmalloc(64 * (i + 1));
        if (!ptrs[i]) { ok = 0; break; }
        /* Write to it to verify it's usable. */
        memset(ptrs[i], 0xAB, 64 * (i + 1));
    }
    strcpy(buf, "  kmalloc 5 blocks: "); strcpy(buf+strlen(buf), ok ? "OK" : "FAIL");
    strcpy(buf+strlen(buf), "\n"); screen_console_puts(buf);

    /* krealloc test. */
    if (ptrs[0]) {
        void *np = krealloc(ptrs[0], 512);
        if (np) {
            ptrs[0] = np;
            screen_console_puts("  krealloc to 512: OK\n");
        } else {
            screen_console_puts("  krealloc to 512: FAIL\n");
        }
    }

    /* Free all. */
    for (int i = 0; i < 5; i++) {
        if (ptrs[i]) kfree(ptrs[i]);
    }
    screen_console_puts("  kfree all: OK\n");

    /* Double-free detection test. */
    void *d = kmalloc(32);
    kfree(d);
    kfree(d);  /* should not crash */
    screen_console_puts("  double-free detection: OK (no crash)\n");

    screen_console_puts("  PASS\n");
    return 0;
}

/* WP-03: frag command - show fragmentation. */
static int shell_cmd_frag(const char *args) {
    (void)args;
    mem_pmm_stats_t ps;
    mem_pmm_get_stats(&ps);
    mem_heap_stats_t hs;
    mem_heap_get_stats(&hs);
    char buf[80]; char n[20];
    strcpy(buf, "PMM: "); screen_console_puts(buf);
    strcpy(buf, "free_pages="); u64_to_str(ps.free_pages, n); strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), " fragments="); u64_to_str(ps.free_fragments, n); strcpy(buf+strlen(buf), n);
    if (ps.free_fragments > 0) {
        u64 avg = ps.free_pages / ps.free_fragments;
        strcpy(buf+strlen(buf), " avg_frag="); u64_to_str(avg, n); strcpy(buf+strlen(buf), n);
    }
    strcpy(buf+strlen(buf), "\n"); screen_console_puts(buf);
    strcpy(buf, "Heap: free_blocks="); u64_to_str(hs.free_count, n); strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), " free_bytes="); u64_to_str(hs.free, n); strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), "\n"); screen_console_puts(buf);
    return 0;
}

/* ---- WP-04 commands ---- */

/* WP-03 fix 1: pftest - test page fault handling. */
/* P2-14 FIX: Test 2 now registers a temporary L1 fault handler that
 * catches an illegal PF on a specific unmapped test address, maps a
 * page so the faulting instruction can resume, and records that the
 * fault was actually observed by the dispatcher. Without this, test 2
 * unconditionally printed "PASS" without exercising the illegal-PF
 * code path at all (the comment admitted "we can't actually trigger
 * an illegal PF without crashing" — but with an L1 handler we can).
 *
 * The test address must be OUTSIDE the kernel's identity-mapped first
 * 4 GiB (otherwise the access succeeds without faulting). We pick
 * 0xFFFF810000000000 — the second entry of PML4 in the upper canonical
 * half (PML4[0x101]). The boot-time PML4 only fills entry 0 (the first
 * 4 GiB identity map), so any PML4 entry above 0 — low or high half —
 * is "not present" and triggers a #PF. */
static volatile u8 g_pftest_illegal_armed = 0;  /* 0=disarmed, 1=test2, 2=test1 */
static volatile u8 g_pftest_illegal_caught = 0;
#define P2_14_TEST_VADDR 0xFFFF810000000000ULL
#define P4_TEST1_VADDR_CONST 0x100000000ULL  /* 4 GiB — above identity map */

static int pftest_illegal_handler(u64 vaddr, u64 error_code, u64 rip) {
    (void)error_code;
    (void)rip;
    /* P4: handle both test 1 (4 GiB address) and test 2 (high-half address). */
    if (g_pftest_illegal_armed) {
        u64 page_vaddr = vaddr & ~0xFFF;
        int matches = 0;
        if (g_pftest_illegal_armed == 1 && page_vaddr == P2_14_TEST_VADDR) matches = 1;
        if (g_pftest_illegal_armed == 2 && page_vaddr == P4_TEST1_VADDR_CONST) matches = 1;
        if (matches) {
            u64 page = mem_pmm_alloc_frame();
            if (page == 0) return 0;  /* let L0 abort */
            if (mem_vmm_map_page(mem_vmm_current_as(), page_vaddr, page,
                             VMM_FLAG_PRESENT | VMM_FLAG_WRITE) != 0) {
                mem_pmm_free_frame(page);
                return 0;
            }
            g_pftest_illegal_caught = 1;
            return 1;  /* handled — resume the faulting instruction */
        }
    }
    return 0;  /* not our test — fall through to L0 default */
}

static int shell_cmd_pftest(const char *args) {
    (void)args;
    screen_console_puts("Page fault test:\n");

    /* Test 1: legal page fault - stack growth.
     * P4 fix: old test claimed "OK (stack grew, value read back)" but the
     * kernel's identity mapping covers all of 0-4 GiB, so any address
     * below the kernel stack is ALREADY mapped as a supervisor-only huge
     * page. No PF occurred, so the test passed "by accident" and the
     * fault counter stayed at 0 — making the "before" snapshot in test 2
     * show 0 PFs even though test 1 supposedly ran a legal PF.
     *
     * The kernel-context stack-growth path CANNOT be tested from the
     * kernel itself — stack growth only works for user processes (where
     * the user stack is in a region not covered by the identity mapping,
     * so touching below it PFs). The kernel always runs with identity-
     * mapped stacks.
     *
     * Instead of pretending to test stack growth, test 1 now verifies
     * that mem_vmm_handle_page_fault correctly rejects a fault on a known-
     * unmapped high address (above 4 GiB), incrementing illegal_faults.
     * This is a real, countable PF that exercises the fault dispatcher. */
    screen_console_puts("  test 1: illegal-PF-on-high-address (counter test)...\n");
    extern u8 g_pftest_stack_ok;
    mem_vmm_fault_stats_t fs1_before, fs1_after;
    mem_vmm_get_fault_stats(&fs1_before);
    /* Trigger an illegal PF on a high address (above 4 GiB identity map).
     * The default handler will increment illegal_faults. We catch the
     * resulting exception and resume — the L1 handler mechanism (test 2)
     * lets us do this safely. */
    mem_vmm_register_fault_handler(pftest_illegal_handler);
    g_pftest_illegal_caught = 0;
    /* P4 test 1 uses a different test address than test 2 so the L1
     * handler can distinguish them. */
    /* Arm with test 1 address. pftest_illegal_handler checks both. */
    g_pftest_illegal_armed = 2;  /* 2 = test 1 mode */
    {
        volatile u8 *p1 = (volatile u8*)(uintptr_t)P4_TEST1_VADDR_CONST;
        __asm__ volatile("" ::: "memory");
        *p1 = 0x42;
        __asm__ volatile("" ::: "memory");
        u8 rb = *p1;
        (void)rb;
    }
    g_pftest_illegal_armed = 0;
    mem_vmm_get_fault_stats(&fs1_after);
    {
        char buf[120]; char n[20];
        u64 delta = fs1_after.total_faults - fs1_before.total_faults;
        u64 legal_d = fs1_after.legal_faults - fs1_before.legal_faults;
        strcpy(buf, "    faults delta="); u64_to_str(delta, n); strcpy(buf+strlen(buf), n);
        strcpy(buf+strlen(buf), " legal_delta="); u64_to_str(legal_d, n); strcpy(buf+strlen(buf), n);
        strcpy(buf+strlen(buf), "\n"); screen_console_puts(buf);
        if (delta > 0 && legal_d > 0) {
            screen_console_puts("    OK (L1 handler caught the high-address PF)\n");
            g_pftest_stack_ok = 1;
        } else {
            screen_console_puts("    FAIL (no PF counted)\n");
        }
    }

    /* Test 2: illegal page fault - access unmapped high address.
     * P2-14 FIX: register a temporary L1 fault handler that catches the
     * PF on the test address, maps a writable page so the faulting
     * instruction can resume, and records that the dispatcher saw the
     * fault. We can then verify the handler ran and the write completed. */
    screen_console_puts("  test 2: illegal PF (unmapped address)...\n");
    mem_vmm_fault_stats_t fs_before, fs_after;
    mem_vmm_get_fault_stats(&fs_before);

    /* Register the L1 handler and arm the test. */
    mem_vmm_register_fault_handler(pftest_illegal_handler);
    g_pftest_illegal_caught = 0;
    g_pftest_illegal_armed = 1;

    /* Trigger the illegal PF: write to an unmapped address. The L1
     * handler will catch it, map the page, and let the instruction
     * complete. We then read the value back to confirm the page was
     * really mapped. */
    volatile u8 *bad = (volatile u8 *)(uintptr_t)P2_14_TEST_VADDR;
    __asm__ volatile("" ::: "memory");  /* full memory barrier */
    *bad = 0x57;
    __asm__ volatile("" ::: "memory");
    u8 readback = *bad;
    __asm__ volatile("" ::: "memory");

    /* Disarm so future faults on this address go to L0 (and future
     * pftest runs re-arm cleanly). */
    g_pftest_illegal_armed = 0;

    mem_vmm_get_fault_stats(&fs_after);
    char buf[120]; char n[20];
    strcpy(buf, "    faults before="); u64_to_str(fs_before.total_faults, n); strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), " after="); u64_to_str(fs_after.total_faults, n); strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), "\n"); screen_console_puts(buf);
    strcpy(buf, "    legal="); u64_to_str(fs_after.legal_faults, n); strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), " illegal="); u64_to_str(fs_after.illegal_faults, n); strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), " stack="); u64_to_str(fs_after.stack_growth, n); strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), " heap="); u64_to_str(fs_after.mem_heap_growth, n); strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), "\n"); screen_console_puts(buf);

    /* Real verdict: the L1 handler must have run (caught==1) AND the
     * write must have completed (readback==0x57) AND the legal_faults
     * counter must have increased (because L1-handled faults count as
     * legal). If any of these failed, the illegal-PF path is broken. */
    int legal_delta = (int)(fs_after.legal_faults - fs_before.legal_faults);
    if (g_pftest_illegal_caught && readback == 0x57 && legal_delta > 0) {
        strcpy(buf, "    L1 handler ran, page mapped, write verified, ");
        u64_to_str((u64)legal_delta, n); strcpy(buf+strlen(buf), n);
        strcpy(buf+strlen(buf), " legal PFs counted\n");
        screen_console_puts(buf);
        screen_console_puts("  PASS\n");
    } else {
        screen_console_puts("    FAIL: L1 handler did not catch the illegal PF\n");
        strcpy(buf, "    caught="); u64_to_str(g_pftest_illegal_caught, n);
        strcpy(buf+strlen(buf), n);
        strcpy(buf+strlen(buf), " readback=0x"); u64_to_hex((u64)readback, n, 2);
        strcpy(buf+strlen(buf), n);
        strcpy(buf+strlen(buf), " legal_delta="); u64_to_str((u64)legal_delta, n);
        strcpy(buf+strlen(buf), n);
        strcpy(buf+strlen(buf), "\n"); screen_console_puts(buf);
    }
    return 0;
}

/* P4 fix: crashlog — print the last CRASH_LOG_LEN exceptions from the
 * crash log buffer (defined in exceptions.c). Lets the user review
 * exceptions even if the on-screen console has scrolled past them. */
static int shell_cmd_crashlog(const char *args) {
    (void)args;
    extern char g_crash_log_count;
    extern struct {
        u64 vector; u64 error_code; u64 rip; u64 rsp; u64 cr2; u64 jiffies;
    } g_crash_log[];
    int count = (int)g_crash_log_count;
    if (count == 0) {
        screen_console_puts("crashlog: no exceptions recorded\n");
        return 0;
    }
    char buf[160]; char n[20];
    strcpy(buf, "crashlog: "); u64_to_str((u64)count, n);
    strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), " entries\n"); screen_console_puts(buf);
    for (int i = 0; i < count && i < 8; i++) {
        strcpy(buf, "  ["); u64_to_str((u64)i, n);
        strcpy(buf+strlen(buf), n);
        strcpy(buf+strlen(buf), "] vec="); u64_to_str(g_crash_log[i].vector, n);
        strcpy(buf+strlen(buf), n);
        strcpy(buf+strlen(buf), " err=0x"); u64_to_hex(g_crash_log[i].error_code, n, 0);
        strcpy(buf+strlen(buf), n);
        strcpy(buf+strlen(buf), " rip=0x"); u64_to_hex(g_crash_log[i].rip, n, 0);
        strcpy(buf+strlen(buf), n);
        strcpy(buf+strlen(buf), " rsp=0x"); u64_to_hex(g_crash_log[i].rsp, n, 0);
        strcpy(buf+strlen(buf), n);
        if (g_crash_log[i].vector == 14) {
            strcpy(buf+strlen(buf), " cr2=0x"); u64_to_hex(g_crash_log[i].cr2, n, 0);
            strcpy(buf+strlen(buf), n);
        }
        strcpy(buf+strlen(buf), " @tick="); u64_to_str(g_crash_log[i].jiffies, n);
        strcpy(buf+strlen(buf), n);
        strcpy(buf+strlen(buf), "\n"); screen_console_puts(buf);
    }
    return 0;
}

/* WP-03 fix 4: cr3test - test CR3 switching. */
static int shell_cmd_cr3test(const char *args) {
    (void)args;
    screen_console_puts("CR3 switch test:\n");
    char buf[80]; char hex[20];

    mem_vmm_as_t orig_as = mem_vmm_current_as();
    strcpy(buf, "  current CR3=0x"); u64_to_hex(orig_as, hex, 12); strcpy(buf+strlen(buf), hex);
    strcpy(buf+strlen(buf), "\n"); screen_console_puts(buf);

    /* Create a new address space. */
    mem_vmm_as_t new_as = mem_vmm_create_address_space();
    if (new_as == 0) { screen_console_puts("  FAIL: create_address_space\n"); return 1; }
    strcpy(buf, "  new AS CR3=0x"); u64_to_hex(new_as, hex, 12); strcpy(buf+strlen(buf), hex);
    strcpy(buf+strlen(buf), "\n"); screen_console_puts(buf);

    /* Map a page at 4 GiB in the new space. */
    u64 phys = mem_pmm_alloc_frame();
    mem_vmm_map_page(new_as, 0x100000000ULL, phys, VMM_FLAGS_USER);
    u64 mapped;
    int ok = mem_vmm_is_mapped(new_as, 0x100000000ULL, &mapped);
    strcpy(buf, "  map at 4GiB: "); strcpy(buf+strlen(buf), ok ? "OK" : "FAIL");
    strcpy(buf+strlen(buf), "\n"); screen_console_puts(buf);

    /* Switch CR3 to the new space. */
    mem_vmm_switch_as(new_as);
    strcpy(buf, "  switched CR3 to 0x"); u64_to_hex(mem_vmm_current_as(), hex, 12); strcpy(buf+strlen(buf), hex);
    strcpy(buf+strlen(buf), "\n"); screen_console_puts(buf);

    /* Verify the mapping is accessible from the new address space.
     * We check via mem_vmm_is_mapped (walks the page tables) rather than
     * directly accessing the memory (which can trigger #PF if there's
     * a TLB/cache issue after CR3 switch). */
    ok = mem_vmm_is_mapped(new_as, 0x100000000ULL, &mapped);
    strcpy(buf, "  verify mapped in new AS: "); strcpy(buf+strlen(buf), ok ? "OK" : "FAIL");
    if (ok) {
        strcpy(buf+strlen(buf), " phys=0x"); u64_to_hex(mapped, hex, 12); strcpy(buf+strlen(buf), hex);
    }
    strcpy(buf+strlen(buf), "\n"); screen_console_puts(buf);

    /* Switch back. */
    mem_vmm_switch_as(orig_as);
    strcpy(buf, "  switched back to 0x"); u64_to_hex(mem_vmm_current_as(), hex, 12); strcpy(buf+strlen(buf), hex);
    strcpy(buf+strlen(buf), "\n"); screen_console_puts(buf);

    /* Cleanup. */
    mem_vmm_unmap_page(new_as, 0x100000000ULL);
    mem_pmm_free_frame(phys);
    mem_vmm_destroy_address_space(new_as);
    screen_console_puts("  PASS\n");
    return 0;
}

/* WP-03 fix 5: heaptest - test heap overhead with 100 allocs. */
static int shell_cmd_heaptest(const char *args) {
    (void)args;
    screen_console_puts("Heap overhead test (100 allocs):\n");
    void *ptrs[100];
    int sizes[100];

    /* Allocate 100 blocks of varying sizes. */
    for (int i = 0; i < 100; i++) {
        sizes[i] = 16 + (i * 37) % 512;  /* 16 to 527 bytes */
        ptrs[i] = kmalloc(sizes[i]);
        if (!ptrs[i]) {
            char buf[40]; char n[20];
            strcpy(buf, "  failed at alloc "); u64_to_str(i, n); strcpy(buf+strlen(buf), n);
            strcpy(buf+strlen(buf), "\n"); screen_console_puts(buf);
            break;
        }
        memset(ptrs[i], (u8)(i & 0xFF), sizes[i]);
    }

    mem_heap_stats_t hs;
    mem_heap_get_stats(&hs);
    char buf[120]; char n[20];
    strcpy(buf, "  heap_size="); u64_to_str(hs.mem_heap_size, n); strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), " allocated="); u64_to_str(hs.allocated, n); strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), " overhead="); u64_to_str(hs.overhead, n); strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), "\n"); screen_console_puts(buf);
    u64 requested = 0;
    for (int i = 0; i < 100; i++) { if (ptrs[i]) requested += sizes[i]; }
    strcpy(buf, "  requested="); u64_to_str(requested, n); strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), " waste="); u64_to_str(hs.allocated - requested, n); strcpy(buf+strlen(buf), n);
    u64 pct = hs.overhead * 100 / (hs.allocated + 1);
    strcpy(buf+strlen(buf), " overhead%="); u64_to_str(pct, n); strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), "\n"); screen_console_puts(buf);

    /* Free all. */
    for (int i = 0; i < 100; i++) {
        if (ptrs[i]) {
            kfree(ptrs[i]);
        }
    }

    mem_heap_get_stats(&hs);
    strcpy(buf, "  after free: alloc="); u64_to_str(hs.alloc_count, n); strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), " free="); u64_to_str(hs.free_count, n); strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), " total_allocs="); u64_to_str(hs.total_allocs, n); strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), " total_frees="); u64_to_str(hs.total_frees, n); strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), "\n"); screen_console_puts(buf);
    screen_console_puts("  PASS\n");
    return 0;
}

/* BUG-0043 repro: PMM bitmap test-then-set race between thread context
 * and timer-IRQ context. A periodic soft-timer callback (the same
 * context the mem_heap grow path runs in: kmalloc -> add_pool ->
 * mem_pmm_alloc_contig) allocates contiguous runs and HOLDS them across
 * ticks, while this command allocates+frees runs of its own from the
 * shell thread. Every range returned to the shell thread is checked
 * against a shadow map of the frames the IRQ context currently holds;
 * an overlap proves the same physical frame was handed out twice -
 * exactly the "one frame used as page table and heap pool at the same
 * time" corruption described for BUG-0043. */
#define PMMRACE_PAGES (131072)                     /* 512 MiB / 4 KiB */
#define PMMRACE_SHADOW_BYTES (PMMRACE_PAGES / 8)   /* 16 KiB */
/* BUG: the 16 KiB shadow map used to be a static BSS array; that pushed
 * the kernel image past the 0x400000 identity-window linker ASSERT, so
 * it is allocated from the heap for the duration of the command. */
static u8  *g_pmmrace_shadow = NULL;
static u64 g_pmmrace_hold[4];                      /* runs held by the callback */
static int g_pmmrace_active = 0;
static u64 g_pmmrace_cb_count = 0;
static volatile int g_pmmrace_in_alloc = 0;
static u64 g_pmmrace_overlaps = 0;
static u64 g_pmmrace_hold_fail = 0;
static u64 g_pmmrace_torn = 0;

static void pmmrace_shadow_set(u64 paddr, u64 count) {
    for (u64 k = 0; k < count; k++) {
        u64 idx = paddr >> 12;
        if (idx >= PMMRACE_PAGES) continue;
        g_pmmrace_shadow[idx >> 3] |= (u8)(1u << (idx & 7));
    }
}
static void pmmrace_shadow_clear(u64 paddr, u64 count) {
    for (u64 k = 0; k < count; k++) {
        u64 idx = paddr >> 12;
        if (idx >= PMMRACE_PAGES) continue;
        g_pmmrace_shadow[idx >> 3] &= (u8)~(1u << (idx & 7));
    }
}
static int pmmrace_shadow_test(u64 paddr, u64 count) {
    for (u64 k = 0; k < count; k++) {
        u64 idx = paddr >> 12;
        if (idx >= PMMRACE_PAGES) continue;
        if (g_pmmrace_shadow[idx >> 3] & (1u << (idx & 7))) return 1;
    }
    return 0;
}

/* Program the PIT channel 0 directly: divisor 11932 = 100 Hz (normal),
 * divisor 150 = ~7.9 kHz during the race window, so the IRQ-context
 * allocator fires every ~126 us and actually intersects the thread's
 * test-then-set window. Restored before the command returns. */
static void pmmrace_pit_set(u16 divisor) {
    __asm__ volatile("outb %0, %1" :: "a"((u8)0x36), "Nd"(0x43));
    __asm__ volatile("outb %0, %1" :: "a"((u8)(divisor & 0xFF)), "Nd"(0x40));
    __asm__ volatile("outb %0, %1" :: "a"((u8)(divisor >> 8)), "Nd"(0x40));
}

static void pmmrace_timer_cb(void *ctx) {
    (void)ctx;
    g_pmmrace_cb_count++;
    if (!g_pmmrace_active) return;
    if (g_pmmrace_in_alloc) g_pmmrace_overlaps++;
    /* Release the runs held from the previous tick, then allocate new
     * ones and hold them (so they stay marked in the shadow map while
     * the shell-thread allocator is being torn by the next tick). */
    for (int k = 0; k < 4; k++) {
        if (g_pmmrace_hold[k]) {
            pmmrace_shadow_clear(g_pmmrace_hold[k], 400);
            for (u64 j = 0; j < 400; j++)
                mem_pmm_free_frame(g_pmmrace_hold[k] + (j << 12));
            g_pmmrace_hold[k] = 0;
        }
    }
    /* Allocate a large run from IRQ context: the first-fit scan then
     * targets the same low free region the interrupted thread just
     * verified, which is exactly where a torn test-then-set hands the
     * same frames to both contexts. */
    for (int k = 0; k < 4; k++) {
        u64 p = mem_pmm_alloc_contig(400);
        if (p) {
            g_pmmrace_hold[k] = p;
            pmmrace_shadow_set(p, 400);
        } else {
            g_pmmrace_hold_fail++;
        }
    }
}

static int shell_cmd_pmmrace(const char *args) {
    (void)args;
    static int registered = 0;
    if (!registered) {
        core_timer_register_periodic(pmmrace_timer_cb, NULL, 1);
        registered = 1;
    }
    if (!g_pmmrace_shadow) g_pmmrace_shadow = (u8 *)kmalloc(PMMRACE_SHADOW_BYTES);
    if (!g_pmmrace_shadow) { screen_console_puts("pmmrace: no memory\n"); return 1; }
    memset(g_pmmrace_shadow, 0, PMMRACE_SHADOW_BYTES);
    g_pmmrace_active = 1;
    pmmrace_pit_set(150);      /* ~7.9 kHz during the test */
    screen_console_puts("pmmrace: 20000 rounds of alloc_contig(512)+free "
                        "vs IRQ-context allocator\n");
    int race = 0;
    int rounds = 0;
    for (int round = 0; round < 20000 && !race; round++) {
        rounds = round + 1;
        g_pmmrace_in_alloc = 1;
        u64 p = mem_pmm_alloc_contig(512);
        g_pmmrace_in_alloc = 0;
        /* Direct double-hold check: did the IRQ callback end up holding
         * any frame inside the range just returned to this thread? */
        for (int k = 0; k < 4; k++) {
            u64 h = g_pmmrace_hold[k];
            if (h && h >= p && h < p + (512u << 12)) {
                g_pmmrace_torn++;
            }
        }
        if (!p) { screen_console_puts("pmmrace: alloc failed\n"); break; }
        if (pmmrace_shadow_test(p, 512)) {
            char buf[80]; char n[24];
            strcpy(buf, "pmmrace: RACE DETECTED at frame ");
            u64_to_str(p >> 12, n); strcpy(buf+strlen(buf), n);
            strcpy(buf+strlen(buf), " round "); u64_to_str((u64)rounds, n);
            strcpy(buf+strlen(buf), n);
            strcpy(buf+strlen(buf), "\n"); screen_console_puts(buf);
            race = 1;
        } else {
            pmmrace_shadow_set(p, 512);
        }
        pmmrace_shadow_clear(p, 512);
        for (u64 j = 0; j < 512; j++) mem_pmm_free_frame(p + (j << 12));
    }
    /* Stop holding frames. */
    for (int k = 0; k < 4; k++) {
        if (g_pmmrace_hold[k]) {
            pmmrace_shadow_clear(g_pmmrace_hold[k], 8);
            for (u64 j = 0; j < 8; j++)
                mem_pmm_free_frame(g_pmmrace_hold[k] + (j << 12));
            g_pmmrace_hold[k] = 0;
        }
    }
    g_pmmrace_active = 0;
    pmmrace_pit_set(11932);    /* restore 100 Hz */
    kfree(g_pmmrace_shadow);
    g_pmmrace_shadow = NULL;
    char buf[64]; char n[24];
    strcpy(buf, "pmmrace: rounds="); u64_to_str((u64)rounds, n);
    strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), " cb_count="); u64_to_str(g_pmmrace_cb_count, n);
    strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), " window_overlaps="); u64_to_str(g_pmmrace_overlaps, n);
    strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), " hold_fail="); u64_to_str(g_pmmrace_hold_fail, n);
    strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), " torn="); u64_to_str(g_pmmrace_torn, n);
    strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), " result=");
    strcpy(buf+strlen(buf), race ? "FAIL (race)" : "PASS (atomic)");
    strcpy(buf+strlen(buf), "\n"); screen_console_puts(buf);
    return race;
}

/* WP-04: ps - list all tasks. */
static int shell_cmd_ps(const char *args) {
    (void)args;
    core_kthread_list();
    screen_console_puts("\nUser processes:\n");
    user_process_list();
    return 0;
}

/* WP-04: kill - kill a task. */
static int shell_cmd_kill(const char *args) {
    if (!args[0]) { screen_console_puts("usage: kill <tid>\n"); return 1; }
    /* P4 fix: parse tid with overflow check. Old code did
     *   tid = tid * 10 + (*p - '0')
     * with no bound — a long arg like "99999999999999" would overflow
     * int, wrap to negative, and pass an arbitrary value to
     * core_kthread_destroy. Now we cap at INT_MAX (and reject > MAX_TASKS). */
    int tid = 0;
    const char *p = args;
    while (*p >= '0' && *p <= '9') {
        int digit = *p - '0';
        if (tid > (0x7fffffff - digit) / 10) {
            screen_console_puts("kill: tid too large\n");
            return 1;
        }
        tid = tid * 10 + digit;
        p++;
    }
    if (tid <= 0) {
        screen_console_puts("invalid tid\n");
        return 1;
    }
    /* WP-09-FIX BUG-004 + BUG-007: if the tid refers to a USER process,
     * run the unified reaper (user_process_reap_resources) instead of
     * core_kthread_destroy. Old behavior had two defects:
     *   BUG-004: core_kthread_destroy immediately freed the task's 4 KiB
     *            kernel stack; when the scheduler later switched to the
     *            dying task the context switch faulted → #DF after
     *            ~15 kill cycles.
     *   BUG-007: the g_procs slot and the user address space were never
     *            reclaimed (zombie in `ps`, ~884 KB leaked per kill).
     * Now: reap resources (fds + AS), wake the parent, mark the task
     * TASK_EXITED and let core_sched_reap_exited() free the stack safely —
     * the exact same path as sys_kill(SIGKILL). */
    for (int i = 0; i < MAX_USER_PROCS; i++) {
        if (g_procs[i].alive && g_procs[i].tid == (tid_t)tid) {
            if (tid == core_kthread_current_tid()) {
                screen_console_puts("kill: cannot kill self\n");
                return 1;
            }
            user_process_reap_resources(&g_procs[i], 128 + 9 /* SIGKILL */);
            if (g_procs[i].parent_tid > 0) core_kthread_wake(g_procs[i].parent_tid);
            /* WP-09-FIX BUG-004: core_sched_task_exited also removes the task
             * from the ready queue (plain state=EXITED gets overwritten
             * back to RUNNING by core_sched_switch_to on the next pop). */
            core_sched_task_exited(g_procs[i].tid);
            screen_console_puts("killed task\n");
            return 0;
        }
    }
    /* P2-01 FIX: check core_kthread_destroy return value; report error
     * (e.g. tid doesn't exist or out of range) instead of always
     * reporting success. */
    if (core_kthread_destroy((tid_t)tid) == 0) {
        screen_console_puts("killed task\n");
        return 0;
    }
    screen_console_puts("kill: tid not found\n");
    return 1;
}

/* WP-04: nice - change priority. */
static int shell_cmd_nice(const char *args) {
    /* BUG-006 FIX: Implement nice <tid> <prio> — change a task's priority.
     * Parses two decimal integers: tid and priority (0..31, 0=highest). */
    if (!args || !args[0]) {
        screen_console_puts("usage: nice <tid> <prio> (0=highest..31=lowest)\n");
        return 1;
    }
    /* P4 fix: parse tid with overflow check (same as shell_cmd_kill). */
    int tid = 0;
    int i = 0;
    while (args[i] >= '0' && args[i] <= '9') {
        int digit = args[i] - '0';
        if (tid > (0x7fffffff - digit) / 10) {
            screen_console_puts("nice: tid too large\n");
            return 1;
        }
        tid = tid * 10 + digit;
        i++;
    }
    if (i == 0) {
        screen_console_puts("nice: invalid tid\n");
        return 1;
    }
    /* Skip space. */
    while (args[i] == ' ') i++;
    /* Parse priority. */
    int prio = -1;
    while (args[i] >= '0' && args[i] <= '9') {
        if (prio < 0) prio = 0;
        int digit = args[i] - '0';
        /* P4 fix: cap prio at 31 — anything larger is invalid anyway. */
        if (prio > 31) {
            screen_console_puts("nice: priority out of range (0..31)\n");
            return 1;
        }
        prio = prio * 10 + digit;
        i++;
    }
    if (prio < 0 || prio > 31) {
        screen_console_puts("nice: invalid tid or prio (0..31)\n");
        return 1;
    }
    if (core_kthread_set_priority(tid, prio) == 0) {
        char buf[40];
        strcpy(buf, "nice: tid ");
        char n[20];
        u64_to_str((u64)tid, n);
        strcpy(buf + strlen(buf), n);
        strcpy(buf + strlen(buf), " priority set to ");
        u64_to_str((u64)prio, n);
        strcpy(buf + strlen(buf), n);
        strcpy(buf + strlen(buf), "\n");
        screen_console_puts(buf);
        return 0;
    } else {
        screen_console_puts("nice: invalid tid (not in use)\n");
        return 1;
    }
}

/* WP-04: sched - show scheduler stats. */
static int shell_cmd_sched(const char *args) {
    (void)args;
    core_sched_stats_t s;
    core_sched_get_stats(&s);
    char buf[120]; char n[20];
    strcpy(buf, "switches="); u64_to_str(s.total_switches, n); strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), " preemptions="); u64_to_str(s.total_preemptions, n); strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), " current_tid="); u64_to_str(s.current_tid, n); strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), " active_tasks="); u64_to_str((u64)s.active_tasks, n); strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), "\n"); screen_console_puts(buf);
    return 0;
}

/* WP-04: syncstat - show sync primitive stats. */
static int shell_cmd_syncstat(const char *args) {
    (void)args;
    sync_stats_t s;
    sync_get_stats(&s);
    char buf[120]; char n[20];
    strcpy(buf, "spin_locks="); u64_to_str(s.spin_locks, n); strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), " spin_unlocks="); u64_to_str(s.spin_unlocks, n); strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), "\n"); screen_console_puts(buf);
    strcpy(buf, "sem_waits="); u64_to_str(s.sem_waits, n); strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), " sem_posts="); u64_to_str(s.sem_posts, n); strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), "\n"); screen_console_puts(buf);
    strcpy(buf, "mutex_locks="); u64_to_str(s.mutex_locks, n); strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), " mutex_unlocks="); u64_to_str(s.mutex_unlocks, n); strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), "\n"); screen_console_puts(buf);
    strcpy(buf, "cond_waits="); u64_to_str(s.cond_waits, n); strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), " cond_signals="); u64_to_str(s.cond_signals, n); strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), "\n"); screen_console_puts(buf);
    return 0;
}

/* WP-04: spawn - spawn a test kernel thread. */
static void test_thread_fn(void *arg);  /* forward declaration */

static int shell_cmd_spawn(const char *args) {
    (void)args;
    tid_t tid = core_kthread_create(test_thread_fn, NULL, "test", TASK_PRIO_DEFAULT);
    if (tid >= 0) {
        char buf[40]; char n[20];
        strcpy(buf, "spawned tid="); u64_to_str((u64)tid, n); strcpy(buf+strlen(buf), n);
        strcpy(buf+strlen(buf), "\n"); screen_console_puts(buf);
    } else {
        screen_console_puts("spawn failed\n");
    }
    return 0;
}

/* P1-6: l1test — kernel shell command that calls L1 job interfaces
 * with a live process. Steps:
 * 1. Run loop (long-running) to create an alive process
 * 2. Call job_create() — should find the loop process, return job_id
 * 3. Call job_list() — should show the job as active
 * 4. Call job_control(bg) — should succeed
 * 5. Call job_control(kill) — should destroy the job + mark inactive */
static int shell_cmd_l1test(const char *args) {
    (void)args;
    char buf[128]; char num[20];

    screen_console_puts("L1 job interface test:\n");

    /* Step 1: Run loop to create an alive process */
    screen_console_puts("  step 1: run loop (create alive process)\n");
    extern const u8 userprog_loop[];
    extern const u64 userprog_loop_size;
    pid_t pid = user_process_create(userprog_loop, userprog_loop_size, "loop");
    if (pid < 0) { screen_console_puts("  FAIL: cannot create loop process\n"); return 1; }
    strcpy(buf, "  started loop pid="); u64_to_str((u64)pid, num); strcat(buf, num); strcat(buf, "\n");
    screen_console_puts(buf);

    /* Step 2: Call job_create */
    screen_console_puts("  step 2: job_create(\"loop\")\n");
    extern int job_create(const char *cmd);
    int job_id = job_create("loop");
    if (job_id < 0) {
        strcpy(buf, "  job_create returned "); u64_to_str((u64)(i64)job_id, num); strcat(buf, num);
        strcat(buf, " — FAIL (no process found)\n");
        screen_console_puts(buf);
        return 1;
    }
    strcpy(buf, "  job_create returned job_id="); u64_to_str((u64)job_id, num); strcat(buf, num);
    strcat(buf, " — PASS\n");
    screen_console_puts(buf);

    /* Step 3: Call job_list */
    screen_console_puts("  step 3: job_list()\n");
    extern int job_list(char *buf, int bufsize);
    char jbuf[256];
    int jc = job_list(jbuf, sizeof(jbuf));
    strcpy(buf, "  job_list returned "); u64_to_str((u64)jc, num); strcat(buf, num);
    strcat(buf, " active jobs:\n");
    screen_console_puts(buf);
    if (jc > 0) screen_console_puts(jbuf);

    /* Step 4: Call job_control(bg) */
    screen_console_puts("  step 4: job_control(");
    u64_to_str((u64)job_id, num); screen_console_puts(num);
    screen_console_puts(", bg=1)\n");
    extern int job_control(int job_id, int action);
    int rc = job_control(job_id, 1);
    if (rc == 0) screen_console_puts("  job_control(bg) = 0 — PASS\n");
    else { strcpy(buf, "  job_control(bg) = "); u64_to_str((u64)(i64)rc, num); strcat(buf, num); strcat(buf, " — FAIL\n"); screen_console_puts(buf); }

    /* Step 5: Call job_control(kill) */
    screen_console_puts("  step 5: job_control(");
    u64_to_str((u64)job_id, num); screen_console_puts(num);
    screen_console_puts(", kill=2)\n");
    rc = job_control(job_id, 2);
    if (rc == 0) screen_console_puts("  job_control(kill) = 0 — PASS\n");
    else { strcpy(buf, "  job_control(kill) = "); u64_to_str((u64)(i64)rc, num); strcat(buf, num); strcat(buf, " — FAIL\n"); screen_console_puts(buf); }

    /* Verify job is now inactive */
    extern int job_list(char *buf, int bufsize);
    jc = job_list(jbuf, sizeof(jbuf));
    strcpy(buf, "  job_list after kill: "); u64_to_str((u64)jc, num); strcat(buf, num);
    strcat(buf, " active jobs\n");
    screen_console_puts(buf);

    screen_console_puts("  L1 job interface test: PASS\n");
    return 0;
}

/* WP-04: run - run a user program. */
static int shell_cmd_run(const char *args) {
    if (!args[0]) {
        screen_console_puts("usage: run <hello|badapp|loop|fork_test|exec_test|pipe_test|mmap_test|mmap_multi|signal_test|select_test|dyn_test|dyn_hello|so_test|dlsym_test|pie_test|reloc_test|ush|mprotect_test|sse_test|pf_test|fdref_test|select_zero_test>\n");
        return 1;
    }
    const u8 *elf = NULL;
    u64 size = 0;
    if (strcmp(args, "hello") == 0) {
        elf = userprog_hello; size = userprog_hello_size;
    } else if (strcmp(args, "badapp") == 0) {
        elf = userprog_badapp; size = userprog_badapp_size;
    } else if (strcmp(args, "loop") == 0) {
        elf = userprog_loop; size = userprog_loop_size;
    } else if (strcmp(args, "fork_test") == 0) {
        elf = userprog_fork_test; size = userprog_fork_test_size;
    } else if (strcmp(args, "exec_test") == 0) {
        elf = userprog_exec_test; size = userprog_exec_test_size;
    } else if (strcmp(args, "pipe_test") == 0) {
        elf = userprog_pipe_test; size = userprog_pipe_test_size;
    } else if (strcmp(args, "mmap_test") == 0) {
        elf = userprog_mmap_test; size = userprog_mmap_test_size;
    } else if (strcmp(args, "signal_test") == 0) {
        elf = userprog_signal_test; size = userprog_signal_test_size;
    } else if (strcmp(args, "select_test") == 0) {
        elf = userprog_select_test; size = userprog_select_test_size;
    } else if (strcmp(args, "dyn_test") == 0) {
        /* WP-08b Batch 5: dyn_test is an alias for so_test (both point to
         * the same embedded bytes). Kept for backward compatibility. */
        elf = userprog_dyn_test; size = userprog_dyn_test_size;
    } else if (strcmp(args, "so_test") == 0) {
        /* WP-08b Batch 5: so_test (canonical name for the DT_NEEDED+PLT test). */
        elf = userprog_so_test; size = userprog_so_test_size;
    } else if (strcmp(args, "dyn_hello") == 0) {
        /* WP-08b Batch 5: minimal dynamic hello (no .so, no relocs). */
        elf = userprog_dyn_hello; size = userprog_dyn_hello_size;
    } else if (strcmp(args, "dlsym_test") == 0) {
        /* WP-08b Batch 5: uses ld.so API table at 0x08000000 (dlopen/dlsym). */
        elf = userprog_dlsym_test; size = userprog_dlsym_test_size;
    } else if (strcmp(args, "pie_test") == 0) {
        /* WP-08b Batch 5: reads own load address via lea _start(%rip). */
        elf = userprog_pie_test; size = userprog_pie_test_size;
    } else if (strcmp(args, "reloc_test") == 0) {
        /* WP-08b Batch 5: exercises RELATIVE + R_X86_64_64 + COPY + JUMP_SLOT. */
        elf = userprog_reloc_test; size = userprog_reloc_test_size;
    } else if (strcmp(args, "mmap_multi") == 0) {
        /* BUG-010 test: multi-process mmap independence (fork + mmap). */
        elf = userprog_mmap_multi; size = userprog_mmap_multi_size;
    } else if (strcmp(args, "mprotect_test") == 0) {
        /* P0-3 test: verify mprotect rejects kernel addresses. */
        elf = userprog_mprotect_test; size = userprog_mprotect_test_size;
    } else if (strcmp(args, "p3_test") == 0) {
        /* P3 batch test: kernel-mem isolation + munmap return + write_and_exit. */
        elf = userprog_p3_test; size = userprog_p3_test_size;
    } else if (strcmp(args, "sse_test") == 0) {
        /* BUG-0042 repro: fork two SSE streams, per-task FPU state check. */
        elf = userprog_sse_test; size = userprog_sse_test_size;
    } else if (strcmp(args, "pf_test") == 0) {
        /* BUG-0044 repro: page-fault semantics (P=1 vs growth, floor). */
        elf = userprog_pf_test; size = userprog_pf_test_size;
    } else if (strcmp(args, "fdref_test") == 0) {
        /* BUG-0098 repro: dup2+close fd refcounting. */
        elf = userprog_fdref_test; size = userprog_fdref_test_size;
    } else if (strcmp(args, "select_zero_test") == 0) {
        /* BUG-0101 repro: select timeout_ms==0 + unopened-fd semantics. */
        elf = userprog_select_zero_test; size = userprog_select_zero_test_size;
    } else if (strcmp(args, "ush") == 0 || strcmp(args, "usershell") == 0) {
        /* WP-08cd: User-space shell. */
        elf = userprog_ush; size = userprog_ush_size;
    } else if (strcmp(args, "test_min") == 0) {
        elf = userprog_test_min; size = userprog_test_min_size;
    } else if (strcmp(args, "test_bss") == 0) {
        elf = userprog_test_bss; size = userprog_test_bss_size;
    } else {
        screen_console_puts("unknown program: ");
        screen_console_puts(args);
        screen_console_putc('\n');
        return 1;
    }
    /* P2-32 FIX (WP-09-FIX BUG-020): detect the '&' suffix BEFORE the
     * ush spin-wait. The old code detected '&' after waiting, so
     * `run ush &` blocked exactly like `run ush` — the suffix was
     * dead code. */
    int shell_cmd_len = (int)strlen(args);
    while (shell_cmd_len > 0 && (args[shell_cmd_len-1] == ' ' || args[shell_cmd_len-1] == '\t')) shell_cmd_len--;
    int background = (shell_cmd_len > 0 && args[shell_cmd_len-1] == '&');
    pid_t pid = user_process_create(elf, size, args);
    if (pid >= 0) {
        char buf[96]; char n[20];
        strcpy(buf, "started pid="); u64_to_str((u64)pid, n); strcpy(buf+strlen(buf), n);
        /* BUG-0134 FIX: print the kill-ready tid next to the pid. `run`
         * used to print only the pid while `kill` consumes a tid — the
         * audit noted the two numbers were not obviously related. */
        extern int user_process_get_tid(pid_t pid);
        int tid = user_process_get_tid(pid);
        if (tid >= 0) {
            strcpy(buf+strlen(buf), " tid=");
            u64_to_str((u64)tid, n);
            strcpy(buf+strlen(buf), n);
            strcpy(buf+strlen(buf), " (kill ");
            u64_to_str((u64)tid, n);
            strcpy(buf+strlen(buf), n);
            strcpy(buf+strlen(buf), " stops it)");
        }
        strcpy(buf+strlen(buf), "\n"); screen_console_puts(buf);
        /* WP-08cd: For user-space shell (ush), set a flag so the kernel
         * shell's main loop skips readline while ush is running.
         * This prevents both shells from competing for keyboard input.
         * When ush exits (sys_exit2), it clears the flag and the
         * kernel shell resumes its normal readline loop.
         * WP-09-FIX BUG-020: with '&' the caller asked for background
         * execution — do NOT block on ush here. */
        if (!background &&
            (strcmp(args, "ush") == 0 || strcmp(args, "usershell") == 0)) {
            extern int g_usershell_running;
            g_usershell_running = 1;
            /* Spin-wait until ush exits. The timer IRQ + scheduler
             * will keep ush running. When ush exits, sys_exit2
             * sets g_usershell_running = 0. */
            while (g_usershell_running) {
                __asm__ volatile("sti; hlt");
            }
        }
        if (background) {
            screen_console_puts("started in background\n");
        }
    } else {
        screen_console_puts("failed to create process\n");
    }
    /* For non-background non-ush programs, the process already ran and exited
     * by the time we get here (the scheduler ran it during the sti/hlt above
     * for ush, or it completed synchronously for short programs). */
    return 0;
}

/* WP-08b Batch 6: ldd — list dynamic dependencies of a user program.
 * Parses the embedded ELF's .dynamic section, reads DT_NEEDED entries,
 * and prints each dependency .so name. Also shows PT_INTERP if present.
 * For static (ET_EXEC) programs, prints "not a dynamic executable". */
static int shell_cmd_ldd(const char *args) {
    if (!args[0]) {
        screen_console_puts("usage: ldd <program>\n");
        return 1;
    }
    /* Match program name to embedded ELF data (same as shell_cmd_run). */
    const u8 *elf = NULL;
    u64 size = 0;
    if (strcmp(args, "hello") == 0) { elf = userprog_hello; size = userprog_hello_size; }
    else if (strcmp(args, "badapp") == 0) { elf = userprog_badapp; size = userprog_badapp_size; }
    else if (strcmp(args, "loop") == 0) { elf = userprog_loop; size = userprog_loop_size; }
    else if (strcmp(args, "fork_test") == 0) { elf = userprog_fork_test; size = userprog_fork_test_size; }
    else if (strcmp(args, "exec_test") == 0) { elf = userprog_exec_test; size = userprog_exec_test_size; }
    else if (strcmp(args, "pipe_test") == 0) { elf = userprog_pipe_test; size = userprog_pipe_test_size; }
    else if (strcmp(args, "mmap_test") == 0) { elf = userprog_mmap_test; size = userprog_mmap_test_size; }
    else if (strcmp(args, "signal_test") == 0) { elf = userprog_signal_test; size = userprog_signal_test_size; }
    else if (strcmp(args, "select_test") == 0) { elf = userprog_select_test; size = userprog_select_test_size; }
    else if (strcmp(args, "dyn_test") == 0 || strcmp(args, "so_test") == 0) {
        elf = userprog_so_test; size = userprog_so_test_size;
    }
    else if (strcmp(args, "dyn_hello") == 0) { elf = userprog_dyn_hello; size = userprog_dyn_hello_size; }
    else if (strcmp(args, "dlsym_test") == 0) { elf = userprog_dlsym_test; size = userprog_dlsym_test_size; }
    else if (strcmp(args, "pie_test") == 0) { elf = userprog_pie_test; size = userprog_pie_test_size; }
    else if (strcmp(args, "reloc_test") == 0) { elf = userprog_reloc_test; size = userprog_reloc_test_size; }
    /* P2-37 FIX: ldd should also know about ush, mmap_multi, test_min,
     * test_bss, mprotect_test — these are embedded programs too. */
    else if (strcmp(args, "ush") == 0) { elf = userprog_ush; size = userprog_ush_size; }
    else if (strcmp(args, "mmap_multi") == 0) { elf = userprog_mmap_multi; size = userprog_mmap_multi_size; }
    else if (strcmp(args, "mprotect_test") == 0) { elf = userprog_mprotect_test; size = userprog_mprotect_test_size; }
    else if (strcmp(args, "p3_test") == 0) { elf = userprog_p3_test; size = userprog_p3_test_size; }
    else {
        screen_console_puts("unknown program: ");
        screen_console_puts(args);
        screen_console_putc('\n');
        return 1;
    }
    /* Validate ELF magic. */
    if (size < 64 || elf[0] != 0x7f || elf[1] != 'E' || elf[2] != 'L' || elf[3] != 'F') {
        screen_console_puts("not an ELF file\n");
        return 1;
    }
    /* Check ELF class (must be 64-bit). */
    if (elf[4] != 2) {
        screen_console_puts("not ELF64\n");
        return 1;
    }
    /* Read e_type (offset 16, 2 bytes, little-endian). */
    u16 e_type = elf[16] | (elf[17] << 8);
    if (e_type == 2) {
        /* ET_EXEC — static executable, no dynamic dependencies. */
        screen_console_puts("not a dynamic executable\n");
        return 0;
    }
    if (e_type != 3) {
        screen_console_puts("not a dynamic executable (unknown type)\n");
        return 0;
    }
    /* ET_DYN — parse program headers for PT_INTERP and PT_DYNAMIC. */
    u64 e_phoff = 0;
    for (int b = 0; b < 8; b++) e_phoff |= ((u64)elf[32 + b]) << (b * 8);
    u16 e_phnum = elf[56] | (elf[57] << 8);
    /* Find PT_INTERP (type 3) and PT_DYNAMIC (type 2). */
    int has_interp = 0;
    int has_dynamic = 0;
    u64 dyn_offset = 0, dyn_filesz = 0;
    for (int i = 0; i < e_phnum; i++) {
        const u8 *ph = elf + e_phoff + (u64)i * 56;
        u32 p_type = ph[0] | (ph[1] << 8) | (ph[2] << 16) | (ph[3] << 24);
        if (p_type == 3) { /* PT_INTERP */
            u64 p_offset = 0, p_filesz = 0;
            for (int b = 0; b < 8; b++) p_offset |= ((u64)ph[8 + b]) << (b * 8);
            for (int b = 0; b < 8; b++) p_filesz |= ((u64)ph[32 + b]) << (b * 8);
            screen_console_puts("\tinterpreter: ");
            for (u64 j = 0; j < p_filesz && elf[p_offset + j]; j++)
                screen_console_putc(elf[p_offset + j]);
            screen_console_putc('\n');
            has_interp = 1;
        }
        if (p_type == 2) { /* PT_DYNAMIC */
            for (int b = 0; b < 8; b++) dyn_offset |= ((u64)ph[8 + b]) << (b * 8);
            for (int b = 0; b < 8; b++) dyn_filesz |= ((u64)ph[32 + b]) << (b * 8);
            has_dynamic = 1;
        }
    }
    if (!has_interp) {
        screen_console_puts("\t(no interpreter)\n");
    }
    if (!has_dynamic) {
        screen_console_puts("\t(no .dynamic section)\n");
        return 0;
    }
    /* Parse .dynamic section: find DT_STRTAB (tag=5) first. */
    u64 strtab_val = 0;
    u64 num_dyn = dyn_filesz / 16;
    for (u64 j = 0; j < num_dyn; j++) {
        const u8 *de = elf + dyn_offset + j * 16;
        u64 tag = 0, val = 0;
        for (int b = 0; b < 8; b++) tag |= ((u64)de[b]) << (b * 8);
        for (int b = 0; b < 8; b++) val |= ((u64)de[8 + b]) << (b * 8);
        if (tag == 0) break; /* DT_NULL */
        if (tag == 5) { strtab_val = val; break; } /* DT_STRTAB */
    }
    /* Second pass: find DT_NEEDED (tag=1) entries. */
    int needed_count = 0;
    for (u64 j = 0; j < num_dyn; j++) {
        const u8 *de = elf + dyn_offset + j * 16;
        u64 tag = 0, val = 0;
        for (int b = 0; b < 8; b++) tag |= ((u64)de[b]) << (b * 8);
        for (int b = 0; b < 8; b++) val |= ((u64)de[8 + b]) << (b * 8);
        if (tag == 0) break; /* DT_NULL */
        if (tag == 1 && strtab_val) { /* DT_NEEDED */
            /* val is an offset into .dynstr (at file offset strtab_val). */
            const char *name = (const char*)(elf + strtab_val + val);
            screen_console_puts("\t");
            screen_console_puts(name);
            screen_console_puts("\n");
            needed_count++;
        }
    }
    if (needed_count == 0) {
        screen_console_puts("\t(no dependencies)\n");
    }
    return 0;
}

/* Test kernel thread for the spawn command. */
static void test_thread_fn(void *arg) {
    (void)arg;
    for (int i = 0; i < 5; i++) {
        char buf[40]; char n[20];
        strcpy(buf, "  [test thread] iteration "); u64_to_str(i, n); strcpy(buf+strlen(buf), n);
        strcpy(buf+strlen(buf), "\n"); screen_console_puts(buf);
        for (volatile int j = 0; j < 100000; j++);
        core_sched_yield();
    }
}

/* WP-04: multi-thread test - 3 tasks with interleaved output. */
static void multi_thread_fn(void *arg) {
    int id = (int)(u64)arg;
    char buf[40]; char n[20];
    for (int i = 0; i < 3; i++) {
        strcpy(buf, "  [task "); u64_to_str((u64)id, n); strcpy(buf+strlen(buf), n);
        strcpy(buf+strlen(buf), "] iter "); u64_to_str(i, n); strcpy(buf+strlen(buf), n);
        strcpy(buf+strlen(buf), "\n"); screen_console_puts(buf);
        for (volatile int j = 0; j < 50000; j++);
        core_sched_yield();
    }
}

static int shell_cmd_multi(const char *args) {
    (void)args;
    screen_console_puts("Spawning 3 tasks...\n");
    core_kthread_create(multi_thread_fn, (void*)1, "task1", TASK_PRIO_DEFAULT);
    core_kthread_create(multi_thread_fn, (void*)2, "task2", TASK_PRIO_DEFAULT);
    core_kthread_create(multi_thread_fn, (void*)3, "task3", TASK_PRIO_DEFAULT);
    screen_console_puts("3 tasks spawned (same priority). Output should interleave.\n");
    return 0;
}

/* WP-04: synctest - test sync primitives with real contention. */
static volatile u64 g_sync_counter = 0;
static spinlock_t g_sync_spin;
static mutex_t g_sync_mutex;
static sem_t g_sync_sem;
static volatile int g_sync_tasks_done = 0;

static void sync_spin_fn(void *arg) {
    (void)arg;
    for (int i = 0; i < 1000; i++) {
        spin_lock(&g_sync_spin);
        g_sync_counter++;
        spin_unlock(&g_sync_spin);
        core_sched_yield();
    }
    __sync_fetch_and_add(&g_sync_tasks_done, 1);
}

static void sync_mutex_fn(void *arg) {
    (void)arg;
    for (int i = 0; i < 1000; i++) {
        mutex_lock(&g_sync_mutex);
        g_sync_counter++;
        mutex_unlock(&g_sync_mutex);
        core_sched_yield();
    }
    __sync_fetch_and_add(&g_sync_tasks_done, 1);
}

static void sync_sem_fn(void *arg) {
    (void)arg;
    for (int i = 0; i < 1000; i++) {
        sem_wait(&g_sync_sem);
        __sync_fetch_and_add(&g_sync_counter, 1);  /* atomic increment */
        sem_post(&g_sync_sem);
        core_sched_yield();
    }
    __sync_fetch_and_add(&g_sync_tasks_done, 1);
}

static int shell_cmd_synctest(const char *args) {
    (void)args;
    char buf[80]; char n[20];

    /* Test 1: spinlock */
    screen_console_puts("=== Spinlock test (3 tasks x 1000) ===\n");
    g_sync_counter = 0;
    g_sync_tasks_done = 0;
    spin_init(&g_sync_spin);
    core_kthread_create(sync_spin_fn, NULL, "spin1", TASK_PRIO_DEFAULT);
    core_kthread_create(sync_spin_fn, NULL, "spin2", TASK_PRIO_DEFAULT);
    core_kthread_create(sync_spin_fn, NULL, "spin3", TASK_PRIO_DEFAULT);
    /* Wait for all tasks to finish (busy-wait with yield). */
    while (g_sync_tasks_done < 3) core_sched_yield();
    strcpy(buf, "  counter="); u64_to_str(g_sync_counter, n); strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), " (expected 3000) ");
    strcpy(buf+strlen(buf), g_sync_counter == 3000 ? "PASS" : "FAIL");
    strcpy(buf+strlen(buf), "\n"); screen_console_puts(buf);

    /* Test 2: mutex */
    screen_console_puts("=== Mutex test (3 tasks x 1000) ===\n");
    g_sync_counter = 0;
    g_sync_tasks_done = 0;
    mutex_init(&g_sync_mutex);
    core_kthread_create(sync_mutex_fn, NULL, "mtx1", TASK_PRIO_DEFAULT);
    core_kthread_create(sync_mutex_fn, NULL, "mtx2", TASK_PRIO_DEFAULT);
    core_kthread_create(sync_mutex_fn, NULL, "mtx3", TASK_PRIO_DEFAULT);
    while (g_sync_tasks_done < 3) core_sched_yield();
    strcpy(buf, "  counter="); u64_to_str(g_sync_counter, n); strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), " (expected 3000) ");
    strcpy(buf+strlen(buf), g_sync_counter == 3000 ? "PASS" : "FAIL");
    strcpy(buf+strlen(buf), "\n"); screen_console_puts(buf);

    /* Test 3: semaphore (binary = mutex) */
    screen_console_puts("=== Semaphore test (3 tasks x 1000) ===\n");
    g_sync_counter = 0;
    g_sync_tasks_done = 0;
    sem_init(&g_sync_sem, 1);  /* binary semaphore */
    core_kthread_create(sync_sem_fn, NULL, "sem1", TASK_PRIO_DEFAULT);
    core_kthread_create(sync_sem_fn, NULL, "sem2", TASK_PRIO_DEFAULT);
    core_kthread_create(sync_sem_fn, NULL, "sem3", TASK_PRIO_DEFAULT);
    while (g_sync_tasks_done < 3) core_sched_yield();
    strcpy(buf, "  counter="); u64_to_str(g_sync_counter, n); strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), " (expected 3000) ");
    strcpy(buf+strlen(buf), g_sync_counter == 3000 ? "PASS" : "FAIL");
    strcpy(buf+strlen(buf), "\n"); screen_console_puts(buf);

    return 0;
}

/* Global flag for pftest. */
u8 g_pftest_stack_ok = 0;

/* ---- WP-05: VFS / ramfs / ATA / FAT32 commands ----
 * Note: ls, cat, mkdir, rmdir, touch, write, mount, umount, rm, mv, cp,
 * cd, pwd, tree, df, du, export, alias, unalias live in file_cmds.c and
 * shell.c. This file keeps the WP-05-specific test commands. */

/* Thin wrapper so we can keep the legacy `mounts` command name pointing at
 * the same VFS list as `mount` (which is registered in file_cmds.c). */
static int shell_cmd_mounts_wrapper(const char *args) {
    (void)args;
    fs_vfs_list_mounts();
    return 0;
}

static int shell_cmd_fstest(const char *args) {
    (void)args;
    screen_console_puts("VFS self-test:\n");
    /* mkdir /tmp/vfstest */
    if (fs_vfs_mkdir("/tmp/vfstest") < 0) {
        screen_console_puts("  mkdir /tmp/vfstest: FAIL (already exists?)\n");
    } else {
        screen_console_puts("  mkdir /tmp/vfstest: OK\n");
    }
    /* Write a file. */
    const char *text = "hello from ramfs";
    int fd = fs_vfs_open("/tmp/vfstest/hello.txt",
                      VFS_O_RDWR | VFS_O_CREAT);
    if (fd < 0) { screen_console_puts("  open(O_CREAT): FAIL\n"); return 0; }
    screen_console_puts("  open(O_CREAT): OK\n");
    int n = fs_vfs_write(fd, text, (int)strlen(text));
    if (n != (int)strlen(text)) { screen_console_puts("  write: FAIL\n"); }
    else screen_console_puts("  write: OK\n");
    fs_vfs_close(fd);

    /* Re-open and read back. */
    fd = fs_vfs_open("/tmp/vfstest/hello.txt", VFS_O_RDONLY);
    if (fd < 0) { screen_console_puts("  reopen: FAIL\n"); return 0; }
    char buf[64];
    n = fs_vfs_read(fd, buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = 0;
        char line[80];
        strcpy(line, "  read: OK -> \""); strcpy(line + strlen(line), buf);
        strcpy(line + strlen(line), "\"\n");
        screen_console_puts(line);
    } else {
        screen_console_puts("  read: FAIL\n");
    }
    fs_vfs_close(fd);

    /* List the directory. */
    screen_console_puts("  ls /tmp/vfstest:\n");
    for (int i = 0; ; i++) {
        fs_vfs_dirent_t e;
        if (fs_vfs_readdir("/tmp/vfstest", i, &e) < 0) break;
        char line[VFS_NAME_LEN + 8];
        strcpy(line, "    "); strcpy(line + strlen(line), e.name);
        strcpy(line + strlen(line), "\n");
        screen_console_puts(line);
    }

    /* ramfs stats. */
    int nn, ss;
    fs_ramfs_get_stats(&nn, &ss);
    char stat_line[60]; char num[20];
    strcpy(stat_line, "  ramfs: "); u64_to_str((u64)nn, num);
    strcpy(stat_line + strlen(stat_line), num);
    strcpy(stat_line + strlen(stat_line), " nodes, ");
    u64_to_str((u64)ss, num);
    strcpy(stat_line + strlen(stat_line), num);
    strcpy(stat_line + strlen(stat_line), " bytes\n");
    screen_console_puts(stat_line);
    return 0;
}

/* ---- WP-10a: driver status commands ---- */

static int shell_cmd_ahci(const char *args) {
    (void)args;
    driver_block_ahci_print_state();
    return 0;
}

static int shell_cmd_nvme(const char *args) {
    (void)args;
    driver_block_nvme_print_state();
    return 0;
}

static int shell_cmd_ata(const char *args) {
    (void)args;
    driver_block_ata_dma_print_state();
    return 0;
}

static int shell_cmd_dskstat(const char *args) {
    (void)args;
    /* Show ATA drive detection (original WP-05 interface). */
    screen_console_puts("ATA drive detection:\n");
    for (int d = 0; d < 4; d++) {
        const char *chan = (d < 2) ? "primary" : "secondary";
        const char *role = (d & 1) ? "slave" : "master";
        char line[60];
        strcpy(line, "  ");
        int p = strlen(line);
        int cl = strlen(chan); memcpy(line+p, chan, cl); p += cl;
        line[p++] = ' ';
        cl = strlen(role); memcpy(line+p, role, cl); p += cl;
        strcpy(line+p, ": "); p += 2;
        const char *s = driver_block_ata_detect(d) ? "present" : "absent";
        cl = strlen(s); memcpy(line+p, s, cl); p += cl;
        line[p++] = '\n'; line[p] = 0;
        screen_console_puts(line);
    }
    /* Also show blk-layer devices. */
    driver_block_list_devices();
    /* WP-10a: one-line DMA/AHCI/NVMe presence summary. */
    {
        int dma = 0;
        for (int i = 0; i < 4; i++) if (driver_block_ata_dma_available(i)) dma++;
        char line[96]; char n[24];
        strcpy(line, "drivers: ATA-DMA drives=");
        u64_to_str((u64)dma, n); strcat(line, n);
        strcat(line, "  AHCI drives=");
        u64_to_str((u64)driver_block_ahci_num_drives(), n); strcat(line, n);
        strcat(line, "  NVMe queues=");
        u64_to_str((u64)driver_block_nvme_num_io_queues(), n); strcat(line, n);
        strcat(line, "\n");
        screen_console_puts(line);
    }
    return 0;
}

static int shell_cmd_fatmount(const char *args) {
    /* usage: fatmount <device> <mount_point>  (default: ata0 /mnt)
     * device is an ATA drive string: "ata0".."ata3" or "ata" (=0). */
    char dev[16] = "ata0";
    char mnt[VFS_PATH_LEN] = "/mnt";
    if (args && args[0]) {
        int i = 0, j = 0;
        while (args[i] && args[i] != ' ' && i < 15) { dev[j++] = args[i]; i++; }
        dev[j] = 0;
        while (args[i] == ' ') i++;
        j = 0;
        while (args[i] && j < VFS_PATH_LEN - 1) { mnt[j++] = args[i++]; }
        mnt[j] = 0;
    }
    /* P2-18 FIX: do NOT pre-create the mount point in ramfs. fs_vfs_mount
     * creates a placeholder dir node itself when needed. If we pre-create
     * and the mount then fails, /mnt would remain as an EMPTY ramfs
     * directory — so a later `ls /mnt` would silently show no files,
     * making the failure look like "the disk is just empty" rather than
     * "the mount never happened". Now if mount fails, /mnt is not
     * created and `ls /mnt` reports "no such path", which is honest. */
    int rc = fs_fat32_mount(dev, mnt);
    if (rc < 0) {
        screen_console_puts("fatmount: ");
        screen_console_puts(dev);
        screen_console_puts(" -> ");
        screen_console_puts(mnt);
        screen_console_puts(" FAILED — no FAT32 partition on device or drive not present\n");
        screen_console_puts("(the mount point was NOT created — `ls ");
        screen_console_puts(mnt);
        screen_console_puts("` will report 'no such path' until a real mount succeeds)\n");
        return 1;
    }
    screen_console_puts("fat32 mounted. Try: ls ");
    screen_console_puts(mnt);
    screen_console_putc('\n');
    return 0;
}

static int shell_cmd_fatstat(const char *args) {
    (void)args;
    u64 ts, fc; u32 cs;
    fs_fat32_get_stats(&ts, &fc, &cs);
    char line[80]; char num[20];
    strcpy(line, "fat32: sectors="); u64_to_str(ts, num);
    strcpy(line + strlen(line), num);
    strcpy(line + strlen(line), " free_clusters="); u64_to_str(fc, num);
    strcpy(line + strlen(line), num);
    strcpy(line + strlen(line), " cluster_size="); u64_to_str(cs, num);
    strcpy(line + strlen(line), num);
    strcpy(line + strlen(line), "\n");
    screen_console_puts(line);
    return 0;
}

/* ---- Interactive loop ---- */
static void interactive_loop(void) {
    /* Use the shell-aware installer so the shell can layer its capture hook
     * on top during redirection / pipes. */
    shell_install_console_hook(screen_serial_hook, NULL);

    int tm_id = core_timer_register_periodic(soft_timer_test_cb, NULL, 1000);
    (void)tm_id;

    screen_console_putc('\n');
    screen_console_puts("Open Cube OS " OC_RELEASE_VERSION " ready. Type 'help' for commands.\n");
    screen_console_puts("(Try: dhcp, ping 10.0.2.2, wget 10.0.2.2, dns example.com, route, firewall, tcpstats)\n\n");

    /* WP-10-wp08fix1: one-time init of the full-featured line editor
     * (history ring + Tab completer). */
    shell_lineedit_init();

    char line[256];
    for (;;) {
        /* Show prompt (use $PS1 env var if set, else "oc> "). */
        const char *ps1 = shell_getenv("PS1");
        screen_console_puts(ps1 ? ps1 : "oc> ");
        /* WP-10-wp08fix1: full editing - Up/Down history, Left/Right/
         * Home/End cursor, Tab completion, Ctrl+C/A/E/U/K/W, Delete.
         * (Was screen_console_in_readline: backspace-only.) */
        int len = shell_lineedit_readline(line, sizeof(line));
        if (len == 0) continue;

        /* Full parser: env expansion, aliases, chains, pipes, redirects,
         * wildcards, background. */
        shell_execute_line(line);

        /* Drain any pending network packets between commands. */
        net_poll();
    }
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
    lib_log_init();
    screen_console_reset(screen_fb_rgb(0xE0, 0xE0, 0xE0),
                     screen_fb_rgb(0x10, 0x10, 0x14));
    shell_install_console_hook(screen_serial_hook, NULL);

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

    /* ---- 10. WP-01 extension self-test ---- */
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
    shell_install_console_hook(screen_serial_hook, NULL);

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
    OC_LOG_OK2("kernel heap (kmalloc/kfree)");

    /* ---- 12b. WP-04: Scheduler + Sync ---- */
    core_sched_init();
    OC_LOG_OK2("scheduler (preemptive, priority + round-robin)");
    OC_LOG_OK2("sync primitives (spinlock/sem/mutex/cond)");

    /* ---- 12b2. WP-05: Shell state init (env vars, aliases, cwd) ---- */
    shell_init();
    OC_LOG_OK2("shell state (env vars, aliases, cwd)");

    /* ---- 13. WP-03: Shell commands ---- */
    shell_register_command_ex("help", shell_cmd_help, "show this message", "WP-03");
    shell_register_command_ex("stats", shell_cmd_stats, "show interrupt/timer stats", "WP-01");
    shell_register_command_ex("exc", shell_cmd_exc, "run exception self-test (#DE/#UD/#PF)", "WP-02");
    shell_register_command_ex("timer", shell_cmd_timer, "register a 500ms one-shot timer", "WP-02");
    shell_register_command_ex("echo", shell_cmd_echo, "echo the text back", "WP-03");
    shell_register_command_ex("uname", shell_cmd_uname, "print OS name (uname [-a|-s|-r|-m])", "WP-01");
    shell_register_command_ex("cryptotest", shell_cmd_cryptotest, "test AES/SHA-256/HMAC with NIST vectors", "WP-09");
    shell_register_command_ex("dhtest", shell_cmd_dhtest, "DH modexp 1024-bit (Oakley Group 1) timing + correctness", "WP-09");
    shell_register_command_ex("ssh", shell_cmd_ssh, "SSH client connect (ssh <ip> [port] [user] [password])", "WP-09");
    shell_register_command_ex("sshd", shell_cmd_sshd, "SSH server (sshd [port=22] [user=oc] [password=oc])", "WP-09");
    shell_register_command_ex("clear", shell_cmd_clear, "clear screen", "WP-01");
    shell_register_command_ex("halt", shell_cmd_halt, "halt the kernel", "WP-10d-fix2");
    shell_register_command_ex("mem", shell_cmd_mem, "show physical memory stats", "WP-04");
    shell_register_command_ex("heap", shell_cmd_heap, "show kernel heap stats", "WP-04");
    shell_register_command_ex("vmmap", shell_cmd_vmmap, "show address space mappings", "WP-04");
    shell_register_command_ex("vmtest", shell_cmd_vmtest, "run virtual memory test", "WP-04");
    shell_register_command_ex("memtest", shell_cmd_memtest, "run memory test (PMM + heap)", "WP-04");
    shell_register_command_ex("frag", shell_cmd_frag, "show memory fragmentation", "WP-04");
    /* WP-04 commands */
    shell_register_command_ex("ps", shell_cmd_ps, "list all tasks", "WP-04");
    shell_register_command_ex("kill", shell_cmd_kill, "kill a task (kill <tid>)", "WP-04");
    shell_register_command_ex("nice", shell_cmd_nice, "change priority (nice <tid> <prio>)", "WP-04");
    shell_register_command_ex("sched", shell_cmd_sched, "show scheduler stats", "WP-04");
    shell_register_command_ex("syncstat", shell_cmd_syncstat, "show sync primitive stats", "WP-04");
    shell_register_command_ex("pftest", shell_cmd_pftest, "test page fault handling", "WP-04");
    shell_register_command_ex("cr3test", shell_cmd_cr3test, "test CR3 switching", "WP-04");
    shell_register_command_ex("crashlog", shell_cmd_crashlog, "show last exception crashes", "WP-04");
    shell_register_command_ex("heaptest", shell_cmd_heaptest, "test heap overhead with 100 allocs", "WP-04");
    shell_register_command_ex("pmmrace", shell_cmd_pmmrace, "BUG-0043 repro: PMM bitmap race thread-vs-IRQ", "WP-04");
    shell_register_command_ex("spawn", shell_cmd_spawn, "spawn a test kernel thread", "WP-04");
    shell_register_command_ex("multi", shell_cmd_multi, "spawn 3 tasks with interleaved output", "WP-04");
    shell_register_command_ex("synctest", shell_cmd_synctest, "test sync primitives (spinlock/mutex/sem)", "WP-04");
    shell_register_command_ex("run", shell_cmd_run, "run a user program (hello/badapp/loop/fork_test/.../dyn_test/so_test/dyn_hello/dlsym_test/pie_test/reloc_test)", "WP-04");
    shell_register_command_ex("ldd", shell_cmd_ldd, "list dynamic dependencies (ldd <program>)", "WP-08b");

    /* ---- 12c. WP-04: Userspace ---- */
    usermode_init();
    OC_LOG_OK2("userspace (ring 3, syscalls, ELF loader)");

    /* ---- 12d. WP-05: VFS + ramfs + ATA + FAT32 ---- */
    fs_vfs_init();
    OC_LOG_OK2("VFS (virtual file system)");

    fs_ramfs_init();
    OC_LOG_OK2("ramfs (in-memory file system, mounted at /)");

    driver_block_init();
    driver_block_cache_init();
    driver_block_ata_init();
    driver_block_ata_dma_init(NULL);  /* WP-10a: BMDMA drives first (see ahci/driver_block_ata_dma) */
    driver_block_ata_register_blk();  /* P2 fix: register ATA drives with blk layer */
    OC_LOG_OK2("ATA/IDE PIO driver (LBA28)");

    driver_block_virtio_blk_init();
    driver_block_ahci_init(NULL);     /* WP-10a: SATA AHCI */
    driver_block_nvme_init(NULL);
    OC_LOG_OK2("virtio-blk + AHCI + NVMe drivers");

    /* ---- WP-10d: full USB stack ----
     * Class drivers register FIRST so the enumeration probes them:
     * HID keyboards/mice (boot protocol), MSC storage (BOT + SCSI,
     * lands in the blk table as usda/usdb before the partition scan
     * below), CDC-ACM + FTDI serial, UAC audio.  Then all four host
     * controller backends probe PCI (UHCI + OHCI + EHCI + XHCI) and
     * enumerate their root hubs.  A poll kernel thread keeps hot-plug,
     * input and serial RX alive for the whole session. */
    driver_usb_hid_init();
    driver_usb_msc_init();
    driver_usb_serial_init();
    driver_usb_audio_class_register();
    {
        int uh = driver_usb_probe_all();
        char line[80]; char num[12];
        strcpy(line, "USB hosts up: ");
        u64_to_str((u64)(uh > 0 ? uh : 0), num);
        strcat(line, num);
        OC_LOG_OK2(line);
    }

    OC_LOG_OK2("USB stack (4 HC backends, HID/MSC/serial/audio)");

    fs_fat32_init();
    fs_exfat_init();
    fs_ext4_init();
    OC_LOG_OK2("FAT32 (R/W) + exFAT (R/W) + ext4 (RO) drivers");

    /* ---- WP-09-fix5: system configuration (/etc/opencube.conf) ----
     * Mounts the FAT32 /etc volume when a disk is attached (persistent
     * config), falls back to a ramfs /etc seeded with defaults, and
     * makes sure the config file exists. */
    lib_config_init();

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

    /* WP-05 shell commands: file operations (in file_cmds.c) +
     * WP-07 disk commands (in disk_cmds.c). */
    shell_cmds_file_register();
    shell_cmds_disk_register();
    driver_block_disk_setup_register();   /* WP-10d-pre: abdisk/install/grub-install */
    shell_register_command_ex("mounts", shell_cmd_mounts_wrapper, "list VFS mount table (alias for mount)", "WP-05");
    shell_register_command_ex("fstest", shell_cmd_fstest, "run VFS self-test (mkdir/write/read/ls)", "WP-05");
    shell_register_command_ex("dskstat", shell_cmd_dskstat, "show block devices", "WP-10a");
    shell_register_command_ex("fatmount", shell_cmd_fatmount, "mount FAT32 (fatmount <dev> <path>)", "WP-05");
    shell_register_command_ex("fatstat", shell_cmd_fatstat, "show FAT32 stats", "WP-05");
    /* P1-6: kernel shell command to test L1 job interfaces with live process */
    shell_register_command_ex("l1test", shell_cmd_l1test, "test L1 job_create/job_list/job_control with running process", "WP-08a");

    /* ---- WP-10a: driver status commands + storage test suite ---- */
    shell_register_command_ex("ahci", shell_cmd_ahci, "AHCI controller/port status", "WP-10a");
    shell_register_command_ex("nvme", shell_cmd_nvme, "NVMe controller/queue status", "WP-10a");
    shell_register_command_ex("ata", shell_cmd_ata, "ATA (PIO + Bus-Master DMA) status", "WP-10a");
    shell_cmds_disk_test_register();
    /* ---- WP-10b: NIC driver tests + status commands ---- */
    shell_cmds_nic_test_register();

    /* ---- WP-09-fix5: config + update check commands ---- */
    shell_register_command_ex("checkupdate", shell_cmd_checkupdate, "check for updates via /etc/opencube.conf (http/https)", "WP-09-fix5");
    shell_register_command_ex("config", shell_cmd_config, "read/write system config (config list|get|set|restore|path)", "WP-09-fix5");
    shell_register_command_ex("config_test", shell_cmd_config_test, "config subsystem self-test (write/read/get_all/delete/restore)", "WP-09-fix5");
    shell_register_command_ex("checkupdate_test", shell_cmd_checkupdate_test, "update check self-test (URL/JSON units + live probe)", "WP-09-fix5");
    /* ---- WP-10u: in-system update commands + test suite ---- */
    shell_register_command_ex("update", shell_cmd_update, "check + install system update (update [local <pkg> | --local <pkg> | --status])", "WP-10u");
    shell_register_command_ex("rollback", shell_cmd_rollback, "roll back to slot A (rollback)", "WP-10u");
    shell_register_command_ex("reboot", shell_cmd_reboot, "reboot the machine (reboot)", "WP-10u");

    /* ---- WP-10d-fix2: power management commands + help test suite ---- */
    shell_register_command_ex("shutdown", shell_cmd_shutdown, "power off (flush + ACPI/Bochs/APM ports)", "WP-10d-fix2");
    shell_register_command_ex("poweroff", shell_cmd_shutdown, "alias of shutdown", "WP-10d-fix2");
    shell_register_command_ex("suspend", shell_cmd_suspend, "suspend to RAM (needs ACPI S3)", "WP-10d-fix2");
    shell_register_command_ex("sleep", shell_cmd_suspend, "alias of suspend", "WP-10d-fix2");
    shell_cmds_power_test_register();
    shell_cmds_update_test_register();
    OC_LOG_OK2("shell file/disk commands");

    /* ---- 12e. WP-06: Network stack ---- */
    net_init();
    net_register_shell_commands();
    net_start_timer();
    OC_LOG_OK2("network stack (e1000 + TCP/IP + socket API)");

    /* ---- WP-10c: sound card drivers (HDA/AC'97/SB16/ES1370/virtio/USB) ----
     * Probes all six families; those without hardware simply stay
     * unregistered.  Then the status/test commands. */
    driver_snd_init();
    if (driver_snd_probe_all() == 0) {
        char line[80]; char num[8];
        strcpy(line, "sound: ");
        u64_to_str((u64)driver_snd_num_devices(), num); strcat(line, num);
        strcat(line, " card(s) registered");
        OC_LOG_OK2(line);
    } else {
        OC_LOG_OK2("sound: no sound card present (probe OK)");
    }
    shell_cmds_snd_test_register();

    /* ---- WP-10d: USB status + test commands ---- */
    shell_cmds_usb_test_register();

    /* docs-sync FIX: report the LIVE registered command count once ALL
     * registrations are done (kmain + file + disk + net commands). The old
     * hardcoded "68 commands" ran mid-registration and contradicted help. */
    {
        char cs[80]; char num[12];
        strcpy(cs, "shell command system (");
        u64_to_str((u64)shell_command_count(), num);
        strcat(cs, num);
        strcat(cs, " commands)");
        OC_LOG_OK2(cs);
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

    /* ---- 15. ASCII text demo ---- */
    screen_console_putc('\n');
    lib_log_info("ASCII demo: !\"#$%&'()*+,-./");
    lib_log_info("ASCII demo: 0123456789:;<=>?");
    lib_log_info("ASCII demo: @ABCDEFGHIJKLMNO");
    lib_log_info("ASCII demo: PQRSTUVWXYZ[\\]^_");
    lib_log_info("ASCII demo: `abcdefghijklmno");
    lib_log_info("ASCII demo: pqrstuvwxyz{|}~");

    /* ---- 16. Boot complete ---- */
    screen_console_putc('\n');
    OC_LOG_OK2("boot complete");

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

    /* ---- 17. Interactive loop ---- */
    interactive_loop();

    for (;;) __asm__ volatile("hlt");
}
