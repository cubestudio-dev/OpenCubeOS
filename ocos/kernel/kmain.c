/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS NAK - kernel C entry point (WP-01-tree).
 *
 * Skeleton scope only: take over the VGA text screen, initialise the
 * COM1 UART, print the four-line boot banner on BOTH outputs and
 * halt. No memory management, no interrupts, no drivers - those
 * arrive in later work packages.
 *
 * The banner a user sees on a real machine is the VGA copy. The
 * kernel clears the whole 80x25 text screen first, so no firmware or
 * bootloader leftovers (the SeaBIOS boot lines, GRUB's
 * `Booting '<title>'`) survive, and the first visible line is the
 * kernel's own first line. The hardware cursor is parked right after
 * the banner once it is printed.
 *
 * OCOS_BUILD_TIMESTAMP is provided by ocos/Makefile and derives from
 * SOURCE_DATE_EPOCH (already converted to UTC+8 by the Makefile),
 * which is what makes the banner reproducible.
 */

#include <stdint.h>
#include <stddef.h>

#ifndef OCOS_BUILD_TIMESTAMP
#define OCOS_BUILD_TIMESTAMP "unknown"
#endif

#define OCOS_VERSION "WP-01-tree (0.1.0-dev)"
#define OCOS_ARCH    "x86_64"

/* COM1 - first 16550 UART at I/O port 0x3F8. */
#define COM1 0x3F8U

#define COM1_DATA (COM1 + 0U)   /* transmit / receive buffer */
#define COM1_IER  (COM1 + 1U)   /* interrupt enable (DLAB high byte) */
#define COM1_FCR  (COM1 + 2U)   /* FIFO control */
#define COM1_LCR  (COM1 + 3U)   /* line control */
#define COM1_MCR  (COM1 + 4U)   /* modem control */
#define COM1_LSR  (COM1 + 5U)   /* line status */

#define COM1_LSR_THRE 0x20U     /* transmitter holding register empty */

/* VGA colour text mode: 80x25 cells at physical 0xB8000, each cell a
 * 16-bit word (ASCII byte in the low byte, attribute in the high
 * byte). The boot trampoline identity-maps the first 1 GiB, so the
 * linear address equals the physical one. 0x0F = light grey on
 * black. */
#define VGA_MEM_BASE 0xB8000
#define VGA_COLS     80U
#define VGA_ROWS     25U
#define VGA_ATTR     0x0F00U

/* CRTC index/data ports, used only to place the hardware cursor. */
#define CRTC_INDEX 0x3D4U
#define CRTC_DATA  0x3D5U

static inline void io_outb(uint16_t port, uint8_t value)
{
    /* %w1 forces the %dx register form (`outb %al, %dx`): the only
     * encoding every gcc/binutils combination accepts (imm8 port
     * operands do not exist above 0xFF). */
    __asm__ __volatile__("outb %b0, %w1"
                         :
                         : "a"(value), "d"(port));
}

static inline uint8_t io_inb(uint16_t port)
{
    uint8_t value;
    __asm__ __volatile__("inb %w1, %b0"
                         : "=a"(value)
                         : "d"(port));
    return value;
}

static void com1_init(void)
{
    io_outb(COM1_IER, 0x00U);   /* no UART interrupts */
    io_outb(COM1_LCR, 0x80U);   /* DLAB on */
    io_outb(COM1_DATA, 0x01U);  /* divisor 1 -> 115200 baud */
    io_outb(COM1_IER, 0x00U);
    io_outb(COM1_LCR, 0x03U);   /* 8 data bits, no parity, DLAB off */
    io_outb(COM1_FCR, 0xC7U);   /* FIFO on, cleared, 14-byte trigger */
    io_outb(COM1_MCR, 0x0BU);   /* DTR + RTS + OUT2 */
}

static void com1_putc(char c)
{
    while ((io_inb(COM1_LSR) & COM1_LSR_THRE) == 0U) {
        /* wait until the transmitter holding register is empty */
    }
    io_outb(COM1_DATA, (uint8_t)c);
}

static void com1_puts(const char *s)
{
    for (size_t i = 0U; s[i] != '\0'; ++i) {
        com1_putc(s[i]);
    }
}

static uint16_t *const vga_mem = (uint16_t *)VGA_MEM_BASE;
static size_t vga_row;
static size_t vga_col;

/* Blank every cell and park the print position at the top-left, so
 * the banner starts at screen line 1 no matter what the firmware or
 * the bootloader left behind. */
static void vga_clear(void)
{
    for (size_t i = 0U; i < VGA_COLS * VGA_ROWS; ++i) {
        vga_mem[i] = (uint16_t)' ' | VGA_ATTR;
    }
    vga_row = 0U;
    vga_col = 0U;
}

static void vga_newline(void)
{
    vga_col = 0U;
    if (vga_row + 1U < VGA_ROWS) {
        ++vga_row;
    }
    /* the four-line banner never reaches the bottom row; staying put
     * there is the defined fallback */
}

static void vga_putc(char c)
{
    if (c == '\n') {
        vga_newline();
        return;
    }
    if (vga_col >= VGA_COLS) {
        vga_newline();
    }
    vga_mem[vga_row * VGA_COLS + vga_col] =
        (uint16_t)(unsigned char)c | VGA_ATTR;
    ++vga_col;
}

static void vga_puts(const char *s)
{
    for (size_t i = 0U; s[i] != '\0'; ++i) {
        vga_putc(s[i]);
    }
}

/* Park the blinking hardware cursor right after the last banner
 * character, so no stray cursor is left mid-screen. */
static void vga_sync_cursor(void)
{
    uint16_t pos = (uint16_t)(vga_row * VGA_COLS + vga_col);

    io_outb(CRTC_INDEX, 0x0FU);                    /* cursor loc low */
    io_outb(CRTC_DATA, (uint8_t)(pos & 0x00FFU));
    io_outb(CRTC_INDEX, 0x0EU);                    /* cursor loc high */
    io_outb(CRTC_DATA, (uint8_t)((pos >> 8) & 0x00FFU));
}

/* One banner line to both sinks: the serial log and the screen show
 * byte-identical text. */
static void banner_puts(const char *s)
{
    com1_puts(s);
    vga_puts(s);
}

void kmain(uint32_t mb2_magic, uint32_t mb2_info_addr)
{
    (void)mb2_magic;
    (void)mb2_info_addr;

    com1_init();
    vga_clear();

    banner_puts("Open Cube OS NAK kernel booted\n");
    banner_puts("Version: " OCOS_VERSION "\n");
    banner_puts("Arch: " OCOS_ARCH "\n");
    banner_puts("Build: " OCOS_BUILD_TIMESTAMP "\n");

    vga_sync_cursor();

    for (;;) {
        __asm__ __volatile__("hlt");
    }
}
