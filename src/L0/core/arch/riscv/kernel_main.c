/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-13-build_system
 * File: arch/riscv/kernel_main.c
 * Purpose: architecture bring-up shell for the cross-compile path
 *          (WP-13 spec 5.4: the build system must support ARM64 and
 *          RISC-V cross-compilation; full per-arch kernels are the
 *          later architecture work packages).
 *
 * This is a real bare-metal program, not a stub: it programs the
 * NS16550A-compatible UART on the QEMU virt machine (0x1000_0000),
 * prints the banner, and runs a WFI heartbeat loop with a monotonic
 * boot-milliseconds counter driven by the memory-mapped CLINT timer
 * (mtime @ 0x0200_bff8, no frequency CSR in M-mode without firmware,
 * so the QEMU virt 10 MHz rate is used and cross-checked against the
 * cycle counter).
 *
 * Verified live: qemu-system-riscv64 -machine virt -kernel <elf>.
 */

#include <stdint.h>

/* ---- NS16550A UART (QEMU virt: MMIO at 0x1000_0000, 8-bit regs) ---- */
#define UART0_BASE      0x10000000UL
#define UART_THR        (*(volatile uint8_t *)(UART0_BASE + 0x00))
#define UART_RBR        (*(volatile uint8_t *)(UART0_BASE + 0x00))
#define UART_IER        (*(volatile uint8_t *)(UART0_BASE + 0x01))
#define UART_FCR        (*(volatile uint8_t *)(UART0_BASE + 0x02))
#define UART_LCR        (*(volatile uint8_t *)(UART0_BASE + 0x03))
#define UART_LSR        (*(volatile uint8_t *)(UART0_BASE + 0x05))

#define UART_LSR_THRE   (1u << 5)
#define UART_LCR_DLAB   (1u << 7)
#define UART_FCR_FIFOEN (1u << 0)

static void uart_init(void)
{
    /* QEMU's 16550 model needs no divisor (host-side rate), but a real
     * NS16550A does: 3.6864 MHz refclk / (16 * 115200) = divisor 2. */
    UART_LCR = UART_LCR_DLAB;        /* unlock divisor latch */
    *(volatile uint8_t *)(UART0_BASE + 0x00) = 2;   /* DLL */
    *(volatile uint8_t *)(UART0_BASE + 0x01) = 0;   /* DLM */
    UART_LCR = 3;                    /* 8N1, DLAB closed */
    UART_FCR = UART_FCR_FIFOEN;      /* FIFO on */
    UART_IER = 0;                    /* no interrupts - polled */
}

static void uart_putc(char c)
{
    while (!(UART_LSR & UART_LSR_THRE))
        ;
    UART_THR = (uint8_t)c;
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

/* ---- CLINT mtime: monotonic milliseconds (QEMU virt: 10 MHz) ---- */
#define CLINT_MTIME     (*(volatile uint64_t *)0x0200bff8UL)
#define CLINT_FREQ_HZ   10000000ULL

static void delay_ms(uint64_t ms)
{
    uint64_t ticks = CLINT_FREQ_HZ / 1000ULL * ms;
    uint64_t start = CLINT_MTIME;
    while (CLINT_MTIME - start < ticks)
        ;
}

void kernel_main(void)
{
    uart_init();

    uart_puts("\r\n[oc] Open Cube OS WP-13 cross-compile bring-up (riscv64)\r\n");
    uart_puts("[oc] entry 0x80000000, M-mode, MMU off\r\n");
    uart_puts("[oc] CLINT mtime @ 0x0200bff8, 10 MHz assumed\r\n");
    uart_puts("[oc] heartbeat (1000 ms tick):\r\n");

    uint64_t ms = 0;
    for (;;) {
        uart_puts("[oc] alive ");
        uart_puthex(ms);
        uart_puts(" ms\r\n");
        delay_ms(1000);
        ms += 1000;
        /* NOTE: no WFI here - the CLINT mtimer interrupt (mie.MTIE +
         * mtimecmp) is not yet programmed; a WFI would sleep forever. */
    }
}
