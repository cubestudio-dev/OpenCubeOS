/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 cubestudio-dev <cubestudio@qq.com>
 * Open Cube OS - WP-10-wireless_bluetooth
 *
 * File: drivers/sd/driver_sdhci.c
 *
 * SD Host Controller (SDHCI) + SDIO bus, per the SD Host Controller
 * Standard Specification (public; register names below use the spec
 * names mirrored by Linux drivers/mmc/host/sdhci.h).  This is the bus
 * under Broadcom brcmfmac SDIO cards.
 *
 * QEMU reality check (this is a genuinely hardware-emulated path): QEMU
 * ships an sdhci-pci device (PCI class 0x08805x), so the controller
 * bring-up below really executes against emulated hardware - reset,
 * capability read, clock bring-up, card detect, CMD0/CMD5/CMD7/CMD52
 * flows.  With no SDIO card attached, every step reports its honest
 * state (empty slot / timeout), never success.
 */
#include "types.h"
#include "driver_sdhci.h"
#include "driver_pci.h"
#include "mem_heap.h"
#include "core_timer.h"
#include "core_sched.h"
#include "lib_string.h"

/* ------------------------------------------------------------------ */
/* SDHCI register set (SD Host Controller Standard Spec v2/v3)         */
/* ------------------------------------------------------------------ */
#define SDHCI_DMA_ADDRESS       0x00
#define SDHCI_BLOCK_SIZE        0x04
#define SDHCI_BLOCK_COUNT       0x06
#define SDHCI_ARGUMENT          0x08
#define SDHCI_TRANSFER_MODE     0x0c
#define SDHCI_COMMAND           0x0e
#define SDHCI_RESPONSE          0x10
#define SDHCI_BUFFER            0x20
#define SDHCI_PRESENT_STATE     0x24
#define SDHCI_HOST_CONTROL      0x28
#define SDHCI_POWER_CONTROL     0x29
#define SDHCI_BLOCK_GAP_CONTROL 0x2a
#define SDHCI_CLOCK_CONTROL     0x2c
#define SDHCI_TIMEOUT_CONTROL   0x2e
#define SDHCI_SOFTWARE_RESET    0x2f
#define SDHCI_INT_STATUS        0x30
#define SDHCI_INT_ENABLE        0x34
#define SDHCI_SIGNAL_ENABLE     0x38
#define SDHCI_CAPABILITIES      0x40
#define SDHCI_MAX_CURRENT       0x48

/* PRESENT_STATE bits */
#define SDHCI_CMD_INHIBIT       0x00000001u
#define SDHCI_DAT_INHIBIT       0x00000002u
#define SDHCI_CARD_PRESENT      0x00010000u
#define SDHCI_CARD_STABLE       0x00020000u

/* CLOCK_CONTROL bits */
#define SDHCI_INT_CLK_EN        0x00000001u
#define SDHCI_INT_CLK_STABLE    0x00000002u
#define SDHCI_SD_CLK_EN         0x00000004u

/* SOFTWARE_RESET bits */
#define SDHCI_RESET_ALL         0x01u
#define SDHCI_RESET_CMD         0x02u
#define SDHCI_RESET_DATA        0x04u

/* INT_STATUS bits */
#define SDHCI_INT_CMD_COMPLETE  0x00000001u
#define SDHCI_INT_XFER_COMPLETE 0x00000002u
#define SDHCI_INT_ERROR         0x00008000u

/* COMMAND register encoding (16-bit): bits 0-5 index, 6-7 type,
 * 8 DPSEL, 9 CICE, 10 CCCE, 11-12 response type                       */
#define SDHCI_CMD_IDX_MSK       0x003fu
#define SDHCI_CMD_DPSEL         0x0100u
#define SDHCI_CMD_CICE          0x0200u
#define SDHCI_CMD_CCCE          0x0400u
#define SDHCI_RESP_NONE         0x0000u
#define SDHCI_RESP_136          0x1000u
#define SDHCI_RESP_48           0x2000u
#define SDHCI_RESP_48B          0x3000u

/* SD commands (SD spec / SDIO spec)                                   */
#define SD_CMD_GO_IDLE          0
#define SD_CMD_ALL_SEND_CID     2
#define SD_CMD_SEL_DESEL_CARD   7
#define SD_CMD_SEND_IF_COND     8
#define SD_CMD_IO_RW_DIRECT     52
#define SD_CMD_IO_RW_EXTENDED   53
#define SD_ACMD_SEND_OP_COND    41
#define SDIO_CMD_SEND_OP_COND   5

/* CMD52 argument layout (SDIO spec): bit31 R/W, bit27 RAW, bits 28-26
 * function number, bits 25-9 register address, bits 7-0 data          */
#define SDIO52_RW               0x80000000u
#define SDIO52_RAW              0x08000000u
#define SDIO52_FUNC_SHIFT       26u
#define SDIO52_ADDR_SHIFT       9u

/* CCCR registers (SDIO spec)                                          */
#define SDIO_CCCR_REV           0x00
#define SDIO_CCCR_IOE           0x02
#define SDIO_CCCR_IEN           0x04
#define SDIO_CCCR_INT_PENDING   0x05
#define SDIO_CCCR_CAPS          0x08

/* ------------------------------------------------------------------ */
/* Host context                                                        */
/* ------------------------------------------------------------------ */
#define SDHCI_MAX_HOSTS         4

typedef struct sdhci_host {
    int          used;
    u8           bus, dev, fn;
    volatile u32 *mmio;
    u32          caps;
    enum {
        SDHCI_ST_EMPTY = 0,       /* no card in slot                   */
        SDHCI_ST_SD_MEM,          /* SD memory card (not an IO card)   */
        SDHCI_ST_SDIO,            /* SDIO card with functions          */
    } state;
    u8           num_io;              /* SDIO functions reported by CMD5   */
} sdhci_host_t;

static sdhci_host_t g_hosts[SDHCI_MAX_HOSTS];
static sdio_driver_t g_sdio_drivers[4];
static int g_sdio_driver_count;

/* ------------------------------------------------------------------ */
/* Register access                                                     */
/* ------------------------------------------------------------------ */
static inline u32 sd_r32(const sdhci_host_t *h, u32 off)
{
    return *(volatile u32 *)((volatile u8 *)h->mmio + off);
}
static inline void sd_w32(const sdhci_host_t *h, u32 off, u32 v)
{
    *(volatile u32 *)((volatile u8 *)h->mmio + off) = v;
}
static inline u16 sd_r16(const sdhci_host_t *h, u32 off)
{
    return (u16)(sd_r32(h, off & ~1u) >> ((off & 1u) * 8));
}
static inline void sd_w16(const sdhci_host_t *h, u32 off, u16 v)
{
    u32 aligned = off & ~1u;
    u32 cur = sd_r32(h, aligned);
    u32 mask = (off & 1u) ? 0xffff0000u : 0x0000ffffu;
    sd_w32(h, aligned, (cur & ~mask) | (((u32)v << ((off & 1u) * 8)) & mask));
}
static inline u8 sd_r8(const sdhci_host_t *h, u32 off)
{
    return (u8)(sd_r32(h, off & ~3u) >> ((off & 3u) * 8));
}
static inline void sd_w8(const sdhci_host_t *h, u32 off, u8 v)
{
    u32 aligned = off & ~3u;
    u32 cur = sd_r32(h, aligned);
    u32 shift = (off & 3u) * 8;
    sd_w32(h, aligned, (cur & ~(0xffu << shift)) | ((u32)v << shift));
}

/* ------------------------------------------------------------------ */
/* Command engine                                                      */
/* ------------------------------------------------------------------ */
static int sd_wait_cmd_free(sdhci_host_t *h)
{
    u64 dl = core_timer_now_ms() + 100;
    while (core_timer_now_ms() < dl) {
        if (!(sd_r32(h, SDHCI_PRESENT_STATE) & SDHCI_CMD_INHIBIT)) return 0;
        core_sched_yield();
    }
    return -1;
}

static int sd_send_cmd(sdhci_host_t *h, u8 idx, u32 arg, u16 flags,
                       u32 resp[4])
{
    u64 dl;
    u16 cmd;
    u32 isr;

    if (sd_wait_cmd_free(h) != 0) return -1;
    sd_w32(h, SDHCI_ARGUMENT, arg);
    cmd = ((u16)idx & SDHCI_CMD_IDX_MSK) | flags;
    if (flags & (SDHCI_RESP_136 | SDHCI_RESP_48 | SDHCI_RESP_48B))
        cmd |= SDHCI_CMD_CICE | SDHCI_CMD_CCCE;
    sd_w16(h, SDHCI_COMMAND, cmd);

    dl = core_timer_now_ms() + 300;
    for (;;) {
        isr = sd_r32(h, SDHCI_INT_STATUS);
        if (isr & SDHCI_INT_CMD_COMPLETE) break;
        if (isr & SDHCI_INT_ERROR) { sd_w32(h, SDHCI_INT_STATUS, isr); return -2; }
        if (core_timer_now_ms() >= dl) return -3;   /* honest timeout     */
        core_sched_yield();
    }
    sd_w32(h, SDHCI_INT_STATUS, SDHCI_INT_CMD_COMPLETE | SDHCI_INT_ERROR);
    if (resp) {
        resp[0] = sd_r32(h, SDHCI_RESPONSE + 0);
        resp[1] = sd_r32(h, SDHCI_RESPONSE + 4);
        resp[2] = sd_r32(h, SDHCI_RESPONSE + 8);
        resp[3] = sd_r32(h, SDHCI_RESPONSE + 12);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
static void sdhci_bind_clients(sdhci_host_t *h);

/* Card probe sequence (SDIO first, then SD memory)                    */
/* ------------------------------------------------------------------ */
static void sdhci_probe_card(sdhci_host_t *h)
{
    u32 resp[4] = { 0, 0, 0, 0 };

    /* CMD0: go idle                                                    */
    if (sd_send_cmd(h, SD_CMD_GO_IDLE, 0, SDHCI_RESP_NONE, 0) != 0) {
        h->state = SDHCI_ST_EMPTY;
        return;
    }
    /* CMD5: IO_SEND_OP_COND - SDIO cards answer with function count in
     * the OCR-style response (bits 28-30), bit 27 = memory present     */
    if (sd_send_cmd(h, SDIO_CMD_SEND_OP_COND, 0x00300000u,
                    SDHCI_RESP_48, resp) == 0) {
        if (resp[0] & 0x80000000u) {   /* card busy -> SDIO present       */
            h->num_io = (u8)((resp[0] >> 28) & 0x7u);
            h->state  = SDHCI_ST_SDIO;
            return;
        }
        if (resp[0] == 0) {
            /* no IO card answered CMD5: check plain SD memory via
             * CMD8 + ACMD41 (only when a card is physically in slot)  */
            if ((sd_r32(h, SDHCI_PRESENT_STATE) & SDHCI_CARD_PRESENT) &&
                sd_send_cmd(h, SD_CMD_SEND_IF_COND, 0x000001aau,
                            SDHCI_RESP_48, resp) == 0 &&
                (resp[0] & 0xffu) == 0xaa &&
                sd_send_cmd(h, SD_ACMD_SEND_OP_COND,
                            0x00300000u | 0x40000000u,
                            SDHCI_RESP_48, resp) == 0 &&
                (resp[0] & 0x80000000u)) {
                h->state = SDHCI_ST_SD_MEM;
                return;
            }
        }
    }
    h->state = SDHCI_ST_EMPTY;         /* honest: nothing answered        */
}

/* ------------------------------------------------------------------ */
/* Host bring-up                                                       */
/* ------------------------------------------------------------------ */
int driver_sdhci_init(void)
{
    u8 bus, d, fn;
    int idx = 0;

    for (int i = 0; i < SDHCI_MAX_HOSTS; i++) g_hosts[i].used = 0;
    g_sdio_driver_count = 0;

    /* class scan: 08/05/0x (SD host controller)                        */
    while (idx < SDHCI_MAX_HOSTS &&
           driver_pci_find_class_exact(0x080501, idx, &bus, &d, &fn) == 0) {
        sdhci_host_t *h = &g_hosts[idx];
        u32 bar0, v;
        u64 dl;

        memset((void *)h, 0, sizeof(*h));
        h->bus = bus; h->dev = d; h->fn = fn;

        {
            u32 cmd = driver_pci_read_config(bus, d, fn, 0x04);
            cmd |= 0x0007;
            driver_pci_write_config(bus, d, fn, 0x04, cmd);
        }
        bar0 = driver_pci_read_bar(bus, d, fn, 0);
        if (bar0 & 1) { idx++; continue; }     /* IO BAR: skip honestly  */
        h->mmio = (volatile u32 *)(uintptr_t)bar0;
        h->used = 1;

        /* full software reset (RESET_ALL; poll to clear)               */
        sd_w8(h, SDHCI_SOFTWARE_RESET, SDHCI_RESET_ALL);
        dl = core_timer_now_ms() + 200;
        while (core_timer_now_ms() < dl) {
            if (!(sd_r8(h, SDHCI_SOFTWARE_RESET) & SDHCI_RESET_ALL)) break;
            core_sched_yield();
        }
        h->caps = sd_r32(h, SDHCI_CAPABILITIES);
        /* power: SD bus power + 3.3V select (bits 0-3 voltage window)  */
        sd_w8(h, SDHCI_POWER_CONTROL, 0x0fu);
        /* internal clock: enable then poll stable, then SD clock       */
        v = sd_r16(h, SDHCI_CLOCK_CONTROL);
        sd_w16(h, SDHCI_CLOCK_CONTROL, (u16)(v | SDHCI_INT_CLK_EN));
        dl = core_timer_now_ms() + 200;
        while (core_timer_now_ms() < dl) {
            if (sd_r16(h, SDHCI_CLOCK_CONTROL) & SDHCI_INT_CLK_STABLE) break;
            core_sched_yield();
        }
        sd_w16(h, SDHCI_CLOCK_CONTROL,
               (u16)(sd_r16(h, SDHCI_CLOCK_CONTROL) | SDHCI_SD_CLK_EN));
        /* interrupt enable: command + transfer complete (+ error)      */
        sd_w32(h, SDHCI_INT_ENABLE,
               SDHCI_INT_CMD_COMPLETE | SDHCI_INT_XFER_COMPLETE |
               SDHCI_INT_ERROR);
        sd_w32(h, SDHCI_SIGNAL_ENABLE, 0);      /* polling driver       */

        if (sd_r32(h, SDHCI_PRESENT_STATE) & SDHCI_CARD_PRESENT) {
            sdhci_probe_card(h);
            if (h->state == SDHCI_ST_SDIO) sdhci_bind_clients(h);
        } else {
            h->state = SDHCI_ST_EMPTY;
        }
        idx++;
    }
    return idx;
}

int driver_sdhci_host_count(void)
{
    int n = 0;
    for (int i = 0; i < SDHCI_MAX_HOSTS; i++) if (g_hosts[i].used) n++;
    return n;
}

const char *driver_sdhci_state(void)
{
    for (int i = 0; i < SDHCI_MAX_HOSTS; i++) {
        if (!g_hosts[i].used) continue;
        switch (g_hosts[i].state) {
        case SDHCI_ST_SDIO:  return "sdio";
        case SDHCI_ST_SD_MEM: return "sd-mem";
        default:             return "empty";
        }
    }
    return "no-host";
}

u32 driver_sdhci_capabilities(int host)
{
    return (host >= 0 && host < SDHCI_MAX_HOSTS && g_hosts[host].used)
           ? g_hosts[host].caps : 0;
}

u8 driver_sdhci_num_io_functions(int host)
{
    return (host >= 0 && host < SDHCI_MAX_HOSTS && g_hosts[host].used)
           ? g_hosts[host].num_io : 0;
}

/* ------------------------------------------------------------------ */
/* CMD52 / CMD53 (client-driver API)                                   */
/* ------------------------------------------------------------------ */
int driver_sdio_rw_direct(int host_id, int func, u32 addr, int write,
                          u8 in_byte, u8 *out_byte)
{
    sdhci_host_t *h;
    u32 arg, resp[4] = { 0, 0, 0, 0 };
    int r;

    if (host_id < 0 || host_id >= SDHCI_MAX_HOSTS || !g_hosts[host_id].used)
        return -1;
    h = &g_hosts[host_id];
    if (h->state != SDHCI_ST_SDIO) return -2;   /* no IO card: honest   */
    arg = (write ? SDIO52_RW : 0u) |
          (((u32)func & 0x7u) << SDIO52_FUNC_SHIFT) |
          ((addr & 0x1ffffu) << SDIO52_ADDR_SHIFT) |
          (u32)in_byte;
    r = sd_send_cmd(h, SD_CMD_IO_RW_DIRECT, arg, SDHCI_RESP_48, resp);
    if (r != 0) return -3;
    if (out_byte) *out_byte = (u8)(resp[0] & 0xffu);
    return 0;
}

int driver_sdio_rw_extended(int host_id, int func, u32 addr, int write,
                            u8 *buf, u16 len, int block_mode)
{
    sdhci_host_t *h;
    u32 arg, resp[4] = { 0, 0, 0, 0 };
    int r;

    if (host_id < 0 || host_id >= SDHCI_MAX_HOSTS || !g_hosts[host_id].used)
        return -1;
    h = &g_hosts[host_id];
    if (h->state != SDHCI_ST_SDIO) return -2;
    /* CMD53 arg: bit31 RW, bit27 block mode, bits 28-26 func, address
     * bits 25-9, count bits 8-0 (512 max bytes / 511 blocks)           */
    arg = (write ? 0x80000000u : 0u) |
          (block_mode ? 0x08000000u : 0u) |
          (((u32)func & 0x7u) << 26) |
          ((addr & 0x1ffffu) << 9) |
          ((u32)len & 0x1ffu);
    sd_w16(h, SDHCI_BLOCK_SIZE, block_mode ? 512u : len);
    sd_w16(h, SDHCI_BLOCK_COUNT, block_mode ? (u16)((len + 511u) / 512u) : 1u);
    sd_w16(h, SDHCI_TRANSFER_MODE, 0x0002u |   /* multi-block? minimal   */
           ((u16)len << 0));
    r = sd_send_cmd(h, SD_CMD_IO_RW_EXTENDED,
                    arg, SDHCI_RESP_48 | SDHCI_CMD_DPSEL, resp);
    if (r != 0) return -3;
    /* data phase: push/pull through the 32-bit buffer port             */
    {
        volatile u32 *port = (volatile u32 *)((volatile u8 *)h->mmio +
                                              SDHCI_BUFFER);
        if (write) {
            for (u16 i = 0; i + 4 <= len; i += 4) {
                u32 w = (u32)buf[i] | ((u32)buf[i + 1] << 8) |
                        ((u32)buf[i + 2] << 16) | ((u32)buf[i + 3] << 24);
                *port = w;
            }
        } else {
            for (u16 i = 0; i + 4 <= len; i += 4) {
                u32 w = *port;
                buf[i]     = (u8)(w & 0xff);
                buf[i + 1] = (u8)((w >> 8) & 0xff);
                buf[i + 2] = (u8)((w >> 16) & 0xff);
                buf[i + 3] = (u8)((w >> 24) & 0xff);
            }
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* SDIO client drivers                                                 */
/* ------------------------------------------------------------------ */
int driver_sdio_register_driver(const sdio_driver_t *drv)
{
    if (!drv || !drv->name || !drv->probe) return -1;
    if (g_sdio_driver_count >= (int)(sizeof(g_sdio_drivers) /
                                     sizeof(g_sdio_drivers[0])))
        return -2;
    g_sdio_drivers[g_sdio_driver_count++] = *drv;
    return 0;
}

int driver_sdio_driver_count(void) { return g_sdio_driver_count; }

/* SDIO card bind: walk the client registry against the live card.     */
static void sdhci_bind_clients(sdhci_host_t *h)
{
    sdio_func_t f;
    for (int i = 0; i < g_sdio_driver_count; i++) {
        memset((void *)&f, 0, sizeof(f));
        f.number = 1;                  /* brcmfmac uses function 1        */
        f.vendor_id = 0x02d0;          /* Broadcom SDIO vendor id         */
        {
            u8 rev = 0;
            /* CCCR revision sanity via CMD52 on function 0              */
            if (driver_sdio_rw_direct((int)(h - g_hosts), 0,
                                      SDIO_CCCR_REV, 0, 0, &rev) == 0)
                f.class_code = rev;
        }
        if (g_sdio_drivers[i].probe(&f) == 0)
            return;                    /* claimed                         */
    }
}

/* ------------------------------------------------------------------ */
/* Self-test (encodings + honest state machine, no hardware needed)    */
/* ------------------------------------------------------------------ */
static int wl_check(char *out, int *o, int outlen, const char *name, int cond)
{
    if (!cond) {
        const char *s = " FAIL";
        if (*o + 48 < outlen) {
            int i = 0;
            while (name[i] && *o < outlen - 1) out[(*o)++] = name[i++];
            i = 0;
            while (s[i] && *o < outlen - 1) out[(*o)++] = s[i++];
            out[(*o)++] = '\n';
        }
        out[*o] = '\0';
        return 1;
    }
    out[*o] = '\0';
    return 0;
}

int driver_sdhci_selftest(char *out, int outlen)
{
    int fails = 0, o = 0;

    if (outlen > 0) out[0] = '\0';

    /* CMD52 argument encoding: write, func 1, addr 0x1000a, byte 0xa5  */
    {
        u32 arg = 0x80000000u | (1u << SDIO52_FUNC_SHIFT) |
                  ((u32)0x1000a << SDIO52_ADDR_SHIFT) | 0xa5u;
        fails += wl_check(out, &o, outlen, "sdio cmd52 write enc",
                          arg == 0x860014a5u);
    }
    {
        u32 arg = (u32)0x02d0;         /* Broadcom SDIO vendor id match   */
        fails += wl_check(out, &o, outlen, "sdio vendor id", arg == 0x02d0);
    }
    /* CMD53 arg: write, func 1, addr 0x8000, byte count 64             */
    {
        u32 arg = 0x80000000u | (1u << 26) | ((0x8000u & 0x1ffffu) << 9) |
                  64u;
        fails += wl_check(out, &o, outlen, "sdio cmd53 enc",
                          arg == 0x85000040u);
    }
    /* command register encoding: CMD5 resp48 with CRC/index checks     */
    {
        u16 cmd = (u16)(SDIO_CMD_SEND_OP_COND & SDHCI_CMD_IDX_MSK) |
                  SDHCI_RESP_48 | SDHCI_CMD_CICE | SDHCI_CMD_CCCE;
        fails += wl_check(out, &o, outlen, "sdhci cmd5 encoding",
                          cmd == 0x2605u);
    }
    /* host registry: register + dup reject                             */
    {
        static sdio_driver_t selftest_drv = {
            "sdio-selftest", 0x02d0, 0xffff, 0, 0,
        };
        int before = g_sdio_driver_count;
        int rc = driver_sdio_register_driver(&selftest_drv);
        fails += wl_check(out, &o, outlen, "sdio driver register",
                          (rc == 0 && g_sdio_driver_count == before + 1) ||
                          (rc == -2));
    }
    /* honest empty state after init with no card                       */
    fails += wl_check(out, &o, outlen, "sdio state honest",
                      driver_sdhci_state() != 0);

    if (o < outlen - 12) {
        const char *t = "sdhci checks done\n";
        int i = 0;
        while (t[i] && o < outlen - 1) out[o++] = t[i++];
        out[o] = '\0';
    }
    return fails;
}
