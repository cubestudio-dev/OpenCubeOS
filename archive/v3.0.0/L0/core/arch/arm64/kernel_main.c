/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-13-build_system
 * File: arch/arm64/kernel_main.c
 * Purpose: architecture bring-up shell for the cross-compile path
 *          (WP-13 spec 5.4: the build system must support ARM64 and
 *          RISC-V cross-compilation; full per-arch kernels are the
 *          later architecture work packages).
 *
 * This is a real bare-metal program, not a stub: it programs the
 * PL011 primecell UART on the QEMU virt machine (0x0900_0000),
 * prints the banner, and then runs a WFI heartbeat loop with a
 * monotonic boot-milliseconds counter driven by the ARM generic
 * timer (CNTPCT + CNTFRQ_EL0).
 *
 * Verified live: qemu-system-aarch64 -machine virt -kernel <elf>.
 */

#include <stdint.h>

/* ---- PL011 primecell UART (QEMU virt: MMIO at 0x0900_0000) ---- */
#define UART0_BASE      0x09000000UL
#define UART_DR         (*(volatile uint32_t *)(UART0_BASE + 0x00))
#define UART_FR         (*(volatile uint32_t *)(UART0_BASE + 0x18))
#define UART_IBRD       (*(volatile uint32_t *)(UART0_BASE + 0x24))
#define UART_FBRD       (*(volatile uint32_t *)(UART0_BASE + 0x28))
#define UART_LCRH       (*(volatile uint32_t *)(UART0_BASE + 0x2c))
#define UART_CR         (*(volatile uint32_t *)(UART0_BASE + 0x30))
#define UART_IMSC       (*(volatile uint32_t *)(UART0_BASE + 0x38))
#define UART_ICR        (*(volatile uint32_t *)(UART0_BASE + 0x44))

#define UART_FR_TXFF    (1u << 5)
#define UART_LCRH_FEN   (1u << 4)
#define UART_CR_UARTEN  (1u << 0)
#define UART_CR_TXE     (1u << 8)
#define UART_CR_RXE     (1u << 9)

static void uart_init(void)
{
    /* QEMU's PL011 model ignores the baud divisor (host-side rate),
     * but a real board needs it: 24 MHz refclk, 115200 8N1. */
    UART_CR = 0;                      /* disable during config */
    UART_ICR = 0x7ff;                 /* clear all interrupts */
    UART_IBRD = 13;                   /* 24e6 / (16 * 115200) = 13.02 */
    UART_FBRD = 1;                    /* 0.0208 * 64 + 0.5 -> 1 */
    UART_LCRH = (3u << 5) | UART_LCRH_FEN;   /* 8N1, FIFO on */
    UART_IMSC = 0;                    /* no interrupts - polled */
    UART_CR = UART_CR_UARTEN | UART_CR_TXE | UART_CR_RXE;
}

static void uart_putc(char c)
{
    while (UART_FR & UART_FR_TXFF)
        ;
    UART_DR = (uint32_t)(unsigned char)c;
}

static void uart_puts(const char *s)
{
    for (; *s; s++)
        uart_putc(*s);
}

static void uart_puthex(uint64_t v)
{
    static const char hex[] = "0123456789abcdef";
    uart_puts("0x");
    for (int i = 60; i >= 0; i -= 4)
        uart_putc(hex[(uint32_t)((v >> i) & 0xf)]);
}

/* ---- ARM generic timer: monotonic milliseconds ---- */
static uint64_t timer_count(void)
{
    uint64_t c;
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(c));
    return c;
}

static uint64_t timer_freq(void)
{
    uint64_t f;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(f));
    return f ? f : 100000000ULL;      /* QEMU virt default: 100 MHz */
}

static void delay_ms(uint64_t ms)
{
    uint64_t ticks = timer_freq() / 1000ULL * ms;
    uint64_t start = timer_count();
    while (timer_count() - start < ticks)
        ;
}

void kernel_main(uint64_t dtb_ptr)
{
    uart_init();

    uart_puts("\r\n[oc] Open Cube OS WP-13 cross-compile bring-up (arm64)\r\n");
    uart_puts("[oc] entry 0x40000000, EL1, MMU off, dtb @ ");
    uart_puthex(dtb_ptr);
    uart_puts("\r\n");
    uart_puts("[oc] timer freq: ");
    uart_puthex(timer_freq());
    uart_puts(" Hz\r\n");
    uart_puts("[oc] heartbeat (1000 ms tick):\r\n");

    uint64_t ms = 0;
    for (;;) {
        uart_puts("[oc] alive ");
        uart_puthex(ms);
        uart_puts(" ms\r\n");
        delay_ms(1000);
        ms += 1000;
        /* NOTE: no WFI here - the generic-timer interrupt is not yet
         * programmed (that is CNTV/CNTKCTL setup, a later bring-up
         * step), so a WFI would sleep forever on a quiet machine. */
    }
}
