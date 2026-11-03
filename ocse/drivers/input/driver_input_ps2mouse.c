/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP06c-installer_gui
 * File: ocse/drivers/input/driver_input_ps2mouse.c
 * Purpose: i8042 auxiliary-port mouse (IRQ12) - see the header for
 * the protocol notes and the handshake traps live-caught in the
 * QEMU battery (IRQ1 byte stealing, the RESET device-id byte).
 * Port IO and irqsave follow the vendored-tree convention (each
 * driver declares its own inb/outb; cli via pushfq/popfq as in
 * core_sched.c).
 */
#include "driver_input_ps2mouse.h"
#include "arch_irq.h"
#include <stdint.h>

/* ---- ports + helpers (tree convention: driver-local inlines) ---- */
#define PS2_DATA        0x60
#define PS2_STATUS      0x64
#define PS2_CMD         0x64
#define CMD_WRITE_AUX   0xD4        /* next DATA byte goes to aux    */
#define STAT_OUT_FULL   0x01
#define STAT_AUX_FULL   0x20        /* byte came from the aux port    */
#define CMD_ENABLE_AUX  0xA8
#define CMD_READ_CFG    0x20
#define CMD_WRITE_CFG   0x60
#define CFG_AUX_CLK_DIS 0x20        /* bit 5 - aux clock disabled     */
#define CFG_AUX_IRQ_EN  0x02        /* bit 1 - aux interrupt enable    */

static inline u8 inb(u16 p)  { u8 v; __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(p)); return v; }
static inline void outb(u16 p, u8 v) { __asm__ volatile("outb %0, %1" :: "a"(v), "Nd"(p)); }

/* irqsave idiom (core_sched.c): caller's RFLAGS + cli, restore pops
 * them back (IF restores only if it was set). */
static inline u64 irq_save(void) {
    u64 f;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(f) :: "memory");
    return f;
}
static inline void irq_restore(u64 f) {
    __asm__ volatile("pushq %0; popfq" :: "r"(f) : "memory");
}

/* ---- mouse commands ---- */
#define MSE_RESET        0xFF
#define MSE_SET_DEFAULTS 0xF6
#define MSE_ENABLE       0xF4
#define MSE_ACK          0xFA
#define MSE_SELFTEST_OK  0xAA

/* Every wait is a bounded spin: the ack paths run with interrupts
 * OFF (IRQ1 steals controller responses otherwise), so no scheduler
 * tick can save us from a dead port. */
#define IO_WAIT_LOOPS   200000

static int wait_input_timeout(void) {
    for (int i = 0; i < IO_WAIT_LOOPS; i++)
        if (inb(PS2_STATUS) & STAT_OUT_FULL) return 1;
    return 0;
}

static void wait_output_empty(void) {
    for (int i = 0; i < IO_WAIT_LOOPS; i++)
        if ((inb(PS2_STATUS) & 0x02) == 0) return;
}

static void send_cmd(u8 c)  { wait_output_empty(); outb(PS2_CMD, c); }
static void send_data(u8 d) { wait_output_empty(); outb(PS2_DATA, d); }

static int read_byte(void) {
    if (!wait_input_timeout()) return -1;
    return inb(PS2_DATA);
}

/* Send a command to the AUX DEVICE and wait for its ACK. Two traps:
 * (1) aux commands need the 0xD4 prefix on the command port - plain
 *     data writes land on the KEYBOARD port (live-caught: the keyboard
 *     ACKed our mouse reset and the handshake read garbage);
 * (2) interrupts off while waiting - the controller raises IRQ1 for
 *     every controller-side reply and the keyboard handler would eat
 *     the byte first (live-caught in the first session). */
static int device_cmd(u8 cmd) {
    u64 flags = irq_save();
    send_cmd(CMD_WRITE_AUX);
    send_data(cmd);
    int r = read_byte();
    irq_restore(flags);
    return r == MSE_ACK ? 0 : -1;
}

/* ---- event ring (polled by the GUI) ---- */
#define MSE_RING 32
static ps2mouse_event_t g_ring[MSE_RING];
static volatile int g_head, g_tail;      /* head=write, tail=read */
static int g_present = 0;

/* 3-byte packet reassembly state (IRQ context). */
static u8  g_pkt[3];
static int g_pkt_n = 0;

static void push_event(i32 dx, i32 dy, u8 btn) {
    int nh = (g_head + 1) % MSE_RING;
    if (nh == g_tail) return;            /* full: drop (GUI polls fast) */
    g_ring[g_head].dx = dx;
    g_ring[g_head].dy = dy;
    g_ring[g_head].buttons = btn;
    g_head = nh;
}

static void ps2mouse_irq_handler(void* ctx, arch_irq_frame_t* f) {
    (void)ctx; (void)f;
    u8 st = inb(PS2_STATUS);
    if (!(st & STAT_OUT_FULL)) return;
    if (!(st & STAT_AUX_FULL)) return;   /* keyboard byte: leave it */
    u8 b = inb(PS2_DATA);

    if (g_pkt_n == 0 && !(b & 0x08)) {
        return;                          /* sync-bit resync (bit 3) */
    }
    g_pkt[g_pkt_n++] = b;
    if (g_pkt_n < 3) return;
    g_pkt_n = 0;

    if (g_pkt[0] & 0xC0) return;         /* overflow packet: drop */
    i32 dx = (i32)(i8)g_pkt[1];
    i32 dy = (i32)(i8)g_pkt[2];
    u8 btn = (u8)(g_pkt[0] & 0x07);
    push_event(dx, dy, btn);
}

/* ---- init handshake ---- */
int driver_input_ps2mouse_init(void) {
    g_head = g_tail = 0;
    g_pkt_n = 0;
    g_present = 0;

    /* 1. enable the aux port (no reply: interrupts stay on) */
    send_cmd(CMD_ENABLE_AUX);

    /* 2. config byte: clear the aux clock-disable bit AND set the
     * aux interrupt-enable bit. The keyboard-translate bit stays
     * exactly as the BIOS left it - clearing it turns the keyboard
     * raw set-2 and every hotkey dies (live-caught in the T4
     * battery). Bit 1 is the live-caught WP06c trap: the BIOS boots
     * with aux IRQs DISABLED, and without it the mouse streams into
     * the output buffer but IRQ12 never fires - the handshake (pure
     * status polling) still succeeds and the pointer just never
     * moves. cli/sti around the read-modify-write: the config-byte
     * reply arrives on IRQ1. */
    u64 flags = irq_save();
    send_cmd(CMD_READ_CFG);
    int cfg = read_byte();
    if (cfg < 0) { irq_restore(flags); return -1; }
    cfg &= (u8)~CFG_AUX_CLK_DIS;          /* enable aux clock */
    cfg |= (u8)CFG_AUX_IRQ_EN;            /* raise IRQ12 on aux bytes */
    send_cmd(CMD_WRITE_CFG);
    send_data((u8)cfg);
    irq_restore(flags);

    /* 3. RESET: ACK + selftest(0xAA) + device-id - the id byte MUST
     * be consumed or it poisons the next ACK read (live-caught:
     * "aux ack missing (defaults)"). */
    if (device_cmd(MSE_RESET) != 0) return -1;
    {
        u64 f2 = irq_save();
        int st = read_byte();             /* expect 0xAA */
        int id = read_byte();             /* device id (0x00) */
        irq_restore(f2);
        if (st != MSE_SELFTEST_OK || id < 0) return -1;
    }

    /* 4. defaults + enable streaming */
    if (device_cmd(MSE_SET_DEFAULTS) != 0) return -1;
    if (device_cmd(MSE_ENABLE) != 0) return -1;

    arch_irq_register_handler(12, ps2mouse_irq_handler, NULL);
    g_present = 1;
    return 0;
}

int driver_input_ps2mouse_read_event(ps2mouse_event_t* out) {
    if (g_tail == g_head) return 0;
    out->dx = g_ring[g_tail].dx;
    out->dy = g_ring[g_tail].dy;
    out->buttons = g_ring[g_tail].buttons;
    g_tail = (g_tail + 1) % MSE_RING;
    return 1;
}

int driver_input_ps2mouse_present(void) {
    return g_present;
}
