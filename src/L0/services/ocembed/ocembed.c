/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS 3.0.0-WP03
 * File: services/ocembed/ocembed.c
 * Purpose: the OCIRE embed service body (see ocembed.h for the
 *          contract).  The OCIRE-side sources (ocire/) are the
 *          protocol definition this file implements; they are NOT
 *          included or linked - the mailbox / log-ring layouts and
 *          the agent L1SVC numbering are re-declared here verbatim
 *          from the ocire headers (an ABI, not a source dependency).
 *
 * Everything below is dormant on a normal boot: ocembed_early_init
 * finds no oc.* facts, the service declaration still collects (so
 * the category is routable), and every op refuses with "embed
 * inactive" honestly.
 */
#include "types.h"
#include "ocembed.h"
#include "core_registry.h"
#include "core_usermode.h"     /* g_procs, user_process_create/current */
#include "block_svc.h"         /* device table + trusted channel       */
#include "driver_pci.h"        /* i6300esb probe/disarm                */
#include "lib_klog.h"          /* klog_snapshot_text + klog_write      */
#include "lib_string.h"
#include "lib_log.h"
#include "screen_console.h"
#include "mem_heap.h"
#include "l1svc_defs.h"
#include "arch_multiboot2.h"   /* the injected agent module            */

/* ---- protocol constants (verbatim from ocire/ocire_disk.h + _recover.c) */

#define OCIRE_MB_MAGIC        "OCIREMB2"
#define OCIRE_MB_BYTES        128
#define OCIRE_MB_STATE_WAIT_SIGNAL 1
#define OCIRE_MB_STATE_SIGNAL_OK   2

#define OCIRE_LOG_MAGIC       "OCIRELOG1"
#define OCIRE_LOG_HDR_BYTES   16

#pragma pack(push, 1)
typedef struct {
    char magic[8];
    u32  state;
    u32  boot_count;
    u32  fail_count;
    u32  log_start_lba;
    u32  log_sectors;
    u8   agent_sha256[32];
    u8   reserved[68];
} ocembed_mailbox_t;
#pragma pack(pop)

/* ---- boot facts ------------------------------------------------------ */

static int  g_embed_active;          /* oc.mb= present                    */
static u64  g_mb_lba;                /* mailbox first sector (absolute)   */
static u64  g_log_lba;               /* log ring header sector            */
static u32  g_log_secs;              /* ring size in sectors              */
static int  g_wdt_armed;             /* oc.wdt=ib700                      */
static int  g_agent_injected;        /* oc.agent=1                        */
static const u8 *g_agent_img;        /* module bytes (identity-mapped)    */
static u64  g_agent_len;
static int  g_mb_dev = -1;           /* block device holding the mailbox  */
static int  g_signal_written;        /* boot confirm completed            */
static pid_t g_agent_pid = -1;       /* the ocire_guard process           */

/* ---- tiny cmdline scanner --------------------------------------------
 * Finds "key=<digits>" in a space-separated command line.  Returns 1
 * and the value, 0 when the key is absent.  (No strtoul in the
 * freestanding lib; digits are folded by hand.) */
static int fact_u64(const char *cmdline, const char *key, u64 *out) {
    if (!cmdline) return 0;
    int klen = 0;
    while (key[klen]) klen++;
    const char *p = cmdline;
    while (*p) {
        /* candidate: at a token start (or after a space) and matches key */
        if ((p == cmdline || p[-1] == ' ') &&
            strncmp(p, key, (u64)klen) == 0 && p[klen] == '=') {
            const char *v = p + klen + 1;
            if (*v < '0' || *v > '9') return 0;   /* empty/malformed */
            u64 val = 0;
            while (*v >= '0' && *v <= '9') {
                val = val * 10 + (u64)(*v - '0');
                v++;
            }
            *out = val;
            return 1;
        }
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;
    }
    return 0;
}

/* key=<value> presence: matches when the token starts with "key="
 * (any value).  Used for the OCIRE facts that arrive as key=value
 * pairs (oc.wdt=ib700, oc.agent=1) - the bare fact_flag above cannot
 * see them because the character after the key is '='. */
static int fact_kv(const char *cmdline, const char *key) {
    if (!cmdline) return 0;
    int klen = 0;
    while (key[klen]) klen++;
    const char *p = cmdline;
    while (*p) {
        if ((p == cmdline || p[-1] == ' ') &&
            strncmp(p, key, (u64)klen) == 0 && p[klen] == '=')
            return 1;
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;
    }
    return 0;
}

/* key=<value> with a specific single-char value ("oc.agent=1"). */
static int fact_kv_is(const char *cmdline, const char *key, char value) {
    if (!cmdline) return 0;
    int klen = 0;
    while (key[klen]) klen++;
    const char *p = cmdline;
    while (*p) {
        if ((p == cmdline || p[-1] == ' ') &&
            strncmp(p, key, (u64)klen) == 0 && p[klen] == '=' &&
            p[klen + 1] == value &&
            (p[klen + 2] == 0 || p[klen + 2] == ' '))
            return 1;
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;
    }
    return 0;
}

void ocembed_early_init(const char *cmdline) {
    u64 v;
    if (fact_u64(cmdline, "oc.mb", &v)) {
        g_mb_lba = v;
        g_embed_active = 1;
    }
    if (fact_u64(cmdline, "oc.log", &v))        g_log_lba = v;
    if (fact_u64(cmdline, "oc.logsecs", &v))    g_log_secs = (u32)v;
    /* oc.wdt=ib700 / oc.agent=1 are key=value facts (the supervisor
     * always writes these exact forms; fact_kv sees any value) */
    if (fact_kv(cmdline, "oc.wdt"))             g_wdt_armed = 1;
    if (fact_kv_is(cmdline, "oc.agent", '1'))   g_agent_injected = 1;

    if (g_agent_injected &&
        arch_multiboot2_get_ocire_agent(&g_agent_img, &g_agent_len) == 0)
        ; /* captured */
    else
        g_agent_injected = 0;    /* honest: flag without module = none */

    if (g_embed_active) {
        char line[128]; char n[24];
        strcpy(line, "ocire embed: SUPERVISED boot (mb=");
        u64_to_str(g_mb_lba, n); strcat(line, n);
        strcat(line, " log=");
        u64_to_str(g_log_lba, n); strcat(line, n);
        strcat(line, " logsecs=");
        u64_to_str((u64)g_log_secs, n); strcat(line, n);
        strcat(line, g_wdt_armed ? " wdt=armed" : " wdt=none");
        strcat(line, g_agent_injected ? " agent=yes)" : " agent=no)");
        strcat(line, "\n");
        screen_console_puts(line);
        klog_record(KLOG_INFO, "ocire", "supervised boot: mailbox located");
    }
}

int ocembed_active(void) {
    return g_embed_active;
}

const char *ocembed_status(char *out, int cap) {
    if (!out || cap < 2) return out ? out : "";
    if (!g_embed_active) {
        strncpy(out, "ocire embed: inactive (normal boot)", (u64)cap - 1);
        out[cap - 1] = 0;
        return out;
    }
    char n[24];
    strcpy(out, "ocire embed: active");
    if (g_signal_written) strcat(out, ", SIGNAL_OK written");
    else strcat(out, ", signal PENDING");
    strcat(out, ", agent ");
    if (g_agent_pid > 0) {
        strcat(out, "pid=");
        u64_to_str((u64)g_agent_pid, n); strcat(out, n);
    } else if (g_agent_injected) {
        strcat(out, "injected (not spawned)");
    } else {
        strcat(out, "absent");
    }
    if (g_mb_dev >= 0) {
        driver_block_device_t *d = driver_block_get_device(g_mb_dev);
        strcat(out, ", protected=");
        strcat(out, (d && d->name[0]) ? d->name : "?");
    }
    return out;
}

/* ---- i6300esb disarm --------------------------------------------------
 * The recipe mirrors ocire/ocire_wdt.c (the supervisor armed the
 * device; the SAME device must be stopped by the boot signal):
 *   PCI 8086:25AB, BAR0 (memory or I/O), reload register at +0x0C:
 *   unlock (write 0x80 then 0x86), reload (0x100), then clear the
 *   enable bit in PCI config 0x68 and read it back. */

/* Port I/O with the port ALWAYS in DX (the 2.0.0 P-01 lesson: an
 * "Nd" constraint lets gcc fold a constant port into an 8-bit
 * immediate - 0xCFC+ > 255 then produces `out %al, $0xCFC`, which
 * Debian binutils tolerates but the CI assembler rejects outright.
 * Register-only, like the tui_core.c fix.) */
static inline void esb_outw_io(u16 port, u16 val) {
    __asm__ volatile("outw %0, %%dx" : : "a"(val), "d"(port));
}

static inline void esb_outb_io(u16 port, u8 val) {
    __asm__ volatile("outb %0, %%dx" : : "a"(val), "d"(port));
}

static inline u8 esb_inb_io(u16 port) {
    u8 v;
    __asm__ volatile("inb %%dx, %0" : "=a"(v) : "d"(port));
    return v;
}

static inline void esb_outl_io(u16 port, u32 val) {
    __asm__ volatile("outl %0, %%dx" : : "a"(val), "d"(port));
}

/* 3.0.0-WP03 G4: TRUE 8-bit PCI config access.  The QEMU i6300esb
 * model handles the LOCK/ENABLE register (0x68) ONLY on len==1
 * writes (i6300esb_config_write: `addr == ESB_LOCK_REG && len == 1`);
 * the generic driver_pci_write_config issues a 32-bit DWORD write,
 * which falls through to pci_default_write_config and never reaches
 * the model's enable logic - the watchdog timer keeps running (found
 * live: the disarmed watchdog still fired its stage-1 expiry at 60 s
 * in the supervised_wdt case).  Mirrors ocire's own pci_write8. */
static void esb_pci_write8(u8 bus, u8 dev, u8 fn, u8 offset, u8 val) {
    u32 addr = ((u32)bus << 16) | ((u32)dev << 11) | ((u32)fn << 8)
               | ((u32)offset & 0xFCu) | 0x80000000u;
    esb_outl_io(0xCF8, addr);
    esb_outb_io((u16)(0xCFC + (offset & 3)), val);
}

static u8 esb_pci_read8(u8 bus, u8 dev, u8 fn, u8 offset) {
    u32 addr = ((u32)bus << 16) | ((u32)dev << 11) | ((u32)fn << 8)
               | ((u32)offset & 0xFCu) | 0x80000000u;
    esb_outl_io(0xCF8, addr);
    return esb_inb_io((u16)(0xCFC + (offset & 3)));
}

static int esb_disarm(void) {
    for (u8 bus = 0; bus < 2; bus++) {
        for (u8 dev = 0; dev < 32; dev++) {
            for (u8 fn = 0; fn < 8; fn++) {
                u32 id = driver_pci_read_config(bus, dev, fn, 0);
                if (id == 0xFFFFFFFFu) continue;
                if ((id & 0xFFFFu) != 0x8086u || ((id >> 16) & 0xFFFFu) != 0x25ABu)
                    continue;
                u32 bar = driver_pci_read_config(bus, dev, fn, 0x10);
                if (bar == 0) return -1;         /* not mapped */
                u64 base;
                int is_io;
                if (bar & 1) {
                    is_io = 1;
                    base = (u64)(bar & ~0x3u);
                } else {
                    is_io = 0;
                    base = (u64)(bar & ~0xFu);   /* MMIO: 4 GiB identity map */
                }
                /* unlock: two writes to the reload register */
                if (is_io) {
                    esb_outw_io((u16)(base + 0x0C), 0x80);
                    esb_outw_io((u16)(base + 0x0C), 0x86);
                    esb_outw_io((u16)(base + 0x0C), 0x100);   /* reload */
                } else {
                    *(volatile u16 *)(uintptr_t)(base + 0x0C) = 0x80;
                    *(volatile u16 *)(uintptr_t)(base + 0x0C) = 0x86;
                    *(volatile u16 *)(uintptr_t)(base + 0x0C) = 0x100;
                }
                /* clear the WDT enable bit (LOCK reg 0x68, bit 1) with
                 * a TRUE 8-bit write + read back (see esb_pci_write8) */
                esb_pci_write8(bus, dev, fn, 0x68, 0x00);
                (void)esb_pci_read8(bus, dev, fn, 0x68);
                return 0;
            }
        }
    }
    return -1;                                   /* device not present */
}

/* ---- mailbox boot signal --------------------------------------------- */

static int mb_find_device(void) {
    /* The disk whose sector at oc.mb actually CONTAINS the mailbox.
    * Covering-range alone is ambiguous when a bigger disk happens to
     * span the same LBA (the 128 MiB dual-boot disk covers every LBA
     * of the 16 MiB recovery disk); the OCIREMB2 magic identifies the
     * real one.  The 0xEE GPT shadow means no partition children
     * exist on the recovery disk (WP-01 isolation), so a whole-disk
     * device is what we find. */
    u8 *sec = (u8 *)kmalloc(512);
    if (!sec) return -1;
    int found = -1;
    for (int i = 0; i < BLK_MAX_DEVICES && found < 0; i++) {
        driver_block_device_t *d = driver_block_get_device(i);
        if (!d) continue;
        if (g_mb_lba >= d->sectors) continue;
        if (driver_block_read_sectors_trusted(i, g_mb_lba, 1, sec) != 0)
            continue;
        if (strncmp((const char *)sec, OCIRE_MB_MAGIC, 8) == 0)
            found = i;
    }
    kfree(sec);
    return found;
}

static int mb_write_signal(void) {
    if (g_mb_dev < 0) return -1;
    u8 *sec = (u8 *)kmalloc(512);
    if (!sec) return -1;
    if (driver_block_read_sectors_trusted(g_mb_dev, g_mb_lba, 1, sec) != 0) {
        kfree(sec);
        return -1;
    }
    ocembed_mailbox_t *mb = (ocembed_mailbox_t *)sec;
    if (strncmp(mb->magic, OCIRE_MB_MAGIC, 8) != 0) {
        kfree(sec);
        klog_write(KLOG_ERROR, "ocire",
                   "mailbox magic mismatch at lba=%u - no signal written",
                   (u32)g_mb_lba);
        return -1;
    }
    if (mb->state != OCIRE_MB_STATE_WAIT_SIGNAL) {
        /* Honest: only the armed transition is ours to take. */
        char n[24];
        char line[96];
        strcpy(line, "ocire embed: mailbox state != WAIT_SIGNAL (");
        u64_to_str((u64)mb->state, n); strcat(line, n);
        strcat(line, ") - signal not rewritten\n");
        screen_console_puts(line);
        kfree(sec);
        return 0;
    }
    mb->state = OCIRE_MB_STATE_SIGNAL_OK;
    mb->boot_count++;
    int rc = driver_block_write_sectors_trusted(g_mb_dev, g_mb_lba, 1, sec);
    kfree(sec);
    return rc;
}

/* ---- the log ring channel (guard-only LOG_APPEND) ---------------------
 * Ring layout (verbatim from ocire/ocire_recover.c): sector 0 is a
 * 16-byte header (magic "OCIRELOG1", u32 LE cursor = bytes used in
 * the payload, u24 LE sequence = wrap counter); sectors 1..N-1 are
 * the payload.  cap = log_sectors*512 - 512.  On overflow the cursor
 * wraps to 0 and the sequence bumps (the reader dumps [0, cursor)). */

static int log_ring_append(const char *text) {
    if (g_mb_dev < 0 || g_log_secs < 2) return -1;
    int len = (int)strlen(text);
    if (len <= 0) return 0;

    u8 *hdr = (u8 *)kmalloc(512);
    u8 *sec = (u8 *)kmalloc(512);
    if (!hdr || !sec) {
        if (hdr) kfree(hdr);
        if (sec) kfree(sec);
        return -1;
    }
    if (driver_block_read_sectors_trusted(g_mb_dev, g_log_lba, 1, hdr) != 0) {
        kfree(hdr); kfree(sec);
        return -1;
    }
    if (strncmp((char *)hdr, OCIRE_LOG_MAGIC, 9) != 0) {
        kfree(hdr); kfree(sec);
        klog_write(KLOG_ERROR, "ocire",
                   "log ring magic mismatch at lba=%u - append refused",
                   (u32)g_log_lba);
        return -1;
    }
    u32 cap = g_log_secs * 512 - 512;
    u32 w = (u32)(hdr[9] | (hdr[10] << 8) | (hdr[11] << 16) |
                  ((u32)hdr[12] << 24));
    u32 seq = (u32)(hdr[13] | (hdr[14] << 8) | (hdr[15] << 16));
    if (w > cap) w = cap;                    /* hostile header: clamp */

    int pos = 0;
    while (pos < len) {
        if (w >= cap) {                      /* wrap */
            w = 0;
            seq = (seq + 1) & 0xFFFFFFu;
        }
        u32 off_in_payload = w;              /* byte offset in [0, cap) */
        u64 secno = 1 + (off_in_payload / 512);
        u32  secoff = off_in_payload % 512;
        u32  room = 512 - secoff;
        u32  take = (u32)(len - pos);
        if (take > room) take = room;
        if (take > cap - w) take = cap - w;
        if (secoff == 0 && take == 512) {
            memset(sec, 0, 512);
        } else {
            if (driver_block_read_sectors_trusted(g_mb_dev, g_log_lba + secno,
                                                  1, sec) != 0) {
                kfree(hdr); kfree(sec);
                return -1;
            }
        }
        memcpy(sec + secoff, text + pos, take);
        if (driver_block_write_sectors_trusted(g_mb_dev, g_log_lba + secno,
                                               1, sec) != 0) {
            kfree(hdr); kfree(sec);
            return -1;
        }
        w += take;
        pos += (int)take;
    }

    /* update the header last (crash-safe ordering: a torn append loses
     * at most the not-yet-committed bytes, never the ring structure) */
    hdr[9]  = (u8)(w & 0xFF);
    hdr[10] = (u8)((w >> 8) & 0xFF);
    hdr[11] = (u8)((w >> 16) & 0xFF);
    hdr[12] = (u8)((w >> 24) & 0xFF);
    hdr[13] = (u8)(seq & 0xFF);
    hdr[14] = (u8)((seq >> 8) & 0xFF);
    hdr[15] = (u8)((seq >> 16) & 0xFF);
    int rc = driver_block_write_sectors_trusted(g_mb_dev, g_log_lba, 1, hdr);
    kfree(hdr); kfree(sec);
    return rc;
}

/* ---- guard check ------------------------------------------------------ */

static int caller_is_guard(void) {
    user_proc_t *p = user_process_current();
    return (p && p->ocire_guard) ? 1 : 0;
}

/* ---- the L1SVC channel ------------------------------------------------ */

int ocembed_l1_dispatch(u32 op, const char *arg, char *out, int cap) {
    if (!out || cap < 2) return -1;
    out[0] = 0;

    if (!g_embed_active) {
        strncpy(out, "ocire: embed inactive (normal boot)", (u64)cap - 1);
        out[cap - 1] = 0;
        return -1;
    }
    if (!caller_is_guard()) {
        /* Refuse BEFORE any disk access: only the injected agent may
         * use this channel.  There is deliberately no other caller
         * path - this is not a general "access OCIRE" door. */
        klog_write(KLOG_WARN, "ocire",
                   "L1SVC ocire op=%u refused: caller is not ocire_guard", op);
        strncpy(out, "ocire: refused (guard-only channel)", (u64)cap - 1);
        out[cap - 1] = 0;
        return -1;
    }

    switch (op) {
    case L1SVC_OCIRE_LOG_APPEND: {
        if (!arg || !arg[0]) {
            strncpy(out, "ocire: empty append", (u64)cap - 1);
            out[cap - 1] = 0;
            return -1;
        }
        if (log_ring_append(arg) != 0) {
            strncpy(out, "ocire: append FAILED (ring io)", (u64)cap - 1);
            out[cap - 1] = 0;
            return -1;
        }
        char n[24];
        strncpy(out, "ocire: appended ", (u64)cap - 1);
        u64_to_str((u64)strlen(arg), n);
        strcat(out, n);
        strcat(out, " bytes");
        return 0;
    }
    case L1SVC_OCIRE_KLOG_SNAPSHOT: {
        int n = klog_snapshot_text(out, cap);
        if (n == 0) {
            strncpy(out, "(kernel log ring empty)", (u64)cap - 1);
            out[cap - 1] = 0;
        }
        return 0;
    }
    default:
        strncpy(out, "ocire: unknown op", (u64)cap - 1);
        out[cap - 1] = 0;
        return -1;
    }
}

/* ---- boot confirm ------------------------------------------------------ */

void ocembed_boot_confirm(void) {
    if (!g_embed_active) return;

    /* 1. locate the recovery disk (the block table is fully probed by
     * now - this runs after the whole L0 init). */
    g_mb_dev = mb_find_device();
    if (g_mb_dev < 0) {
        screen_console_puts("ocire embed: NO block device covers the mailbox "
                            "- signal/watchdog/agent skipped, "
                            "protection NOT armed\n");
        klog_write(KLOG_ERROR, "ocire",
                   "no block device covers the mailbox lba=%u", (u32)g_mb_lba);
        return;
    }

    /* 2. whole-disk hard protection FIRST: from this point every
     * write through a covering device index is refused (the signal
     * and the log channel write through the trusted kernel path). */
    driver_block_set_protected(g_mb_dev);
    {
        driver_block_device_t *d = driver_block_get_device(g_mb_dev);
        char line[96];
        strcpy(line, "ocire embed: whole-disk write protection ARMED (");
        strcat(line, (d && d->name[0]) ? d->name : "?");
        strcat(line, ")\n");
        screen_console_puts(line);
    }

    /* 3. mailbox: WAIT_BOOT_SIGNAL -> SIGNAL_OK (+boot_count).  Only
     * the armed transition is taken; any other state is reported and
     * left alone. */
    if (mb_write_signal() == 0) {
        g_signal_written = 1;
        screen_console_puts("ocire embed: boot signal written "
                            "(SIGNAL_OK, boot_count+1)\n");
        klog_record(KLOG_INFO, "ocire", "SIGNAL_OK written to the mailbox");
    } else {
        screen_console_puts("ocire embed: boot signal FAILED - see log\n");
        klog_write(KLOG_ERROR, "ocire", "SIGNAL_OK write failed");
    }

    /* 4. disarm the i6300esb watchdog the supervisor armed.  This
     * MUST happen inside the 60 s boot window; we are ~2 s in.  When
     * the device is absent (mailbox-only signalling) that is honest
     * and harmless. */
    if (g_wdt_armed) {
        if (esb_disarm() == 0) {
            screen_console_puts("ocire embed: i6300esb watchdog DISARMED\n");
            klog_record(KLOG_INFO, "ocire", "i6300esb disarmed");
        } else {
            screen_console_puts("ocire embed: i6300esb not found on the bus "
                                "(mailbox-only signalling)\n");
            klog_write(KLOG_WARN, "ocire", "i6300esb disarm: device absent");
        }
    }

    /* 5. spawn the injected agent as the protected process.  Its ELF
     * lives at the mb2 module address (PMM-reserved since boot; the
     * identity map makes it dereferenceable here). */
    if (g_agent_injected && g_agent_img && g_agent_len) {
        pid_t pid = user_process_create(g_agent_img, g_agent_len,
                                        "ocire_guard");
        if (pid > 0) {
            g_agent_pid = pid;
            /* the protection flag (layer 1): sys_kill refuses every
             * signal for this pid; the L1SVC channel only serves it */
            for (int i = 0; i < MAX_USER_PROCS; i++) {
                if (g_procs[i].alive && g_procs[i].pid == pid) {
                    g_procs[i].ocire_guard = 1;
                    break;
                }
            }
            char line[96]; char n[24];
            strcpy(line, "ocire embed: log agent spawned (ocire_guard, pid=");
            u64_to_str((u64)pid, n); strcat(line, n);
            strcat(line, ", ");
            u64_to_str(g_agent_len, n); strcat(line, n);
            strcat(line, " bytes, protected)\n");
            screen_console_puts(line);
            klog_write(KLOG_INFO, "ocire",
                       "log agent spawned as ocire_guard pid=%d", (int)pid);
        } else {
            screen_console_puts("ocire embed: agent spawn FAILED - the "
                                "recovery log channel stays unused\n");
            klog_write(KLOG_ERROR, "ocire", "log agent spawn failed");
        }
    } else {
        screen_console_puts("ocire embed: no log agent injected "
                            "(the recovery log channel stays unused)\n");
    }
}

/* ---- service identity -------------------------------------------------- */
/* The category string MUST equal the folder name (services/ocembed) -
 * that is also the L1SVC_CAT_NAME(18) routing target. */

OC_SERVICE_DECL_L1("ocire", "ocire embed service (boot signal + agent channel + whole-disk protection + ring-3 surface)",
                   NULL, NULL, ocembed_l1_dispatch);
