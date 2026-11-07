/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP02-ocire_recovery
 * File: ocire/ocire_recover.c
 * Purpose: the WP-02 full recovery shell commands.
 *
 * logread    - dump the boot-log ring recorded by the OCOS-side agent
 * syscheck   - ten-point integrity self-check (volumes, mailbox, log
 *              ring, agent digest, golden manifest, golden files,
 *              system files, recovery-kernel copy)
 * repair quick  - fix metadata + restore damaged system files
 * repair full   - reformat the system volume + full golden rewrite
 * return     - normal-boot mailbox + reboot into OpenCubeOS
 *
 * Every command re-scans the disks first (ocire_disk_scan is
 * idempotent), so each one is safe to run directly at the prompt,
 * after a supervisor degrade, or repeatedly during a repair session.
 */

#include "ocire_recover.h"
#include "ocire_console.h"
#include "ocire_input.h"
#include "ocire_util.h"
#include "ocire_io.h"
#include "ocire_acpi.h"
#include "ocire_ata.h"
#include "ocire_disk.h"
#include "ocire_fat.h"
#include "ocire_sha256.h"

/* Boot-log ring header (sector 0 of /LOG/BOOT.LOG, written by
 * make_ocire_disk.sh and appended-to by the OCOS agent through the
 * kernel channel at the absolute LBA from the mailbox). */
#define OCIRE_LOG_MAGIC    "OCIRELOG1"
#define OCIRE_LOG_HDR_BYTES 16

#pragma pack(push, 1)
typedef struct {
    u8  boot;
    u8  start_chs[3];
    u8  type;
    u8  end_chs[3];
    u32 start_lba;
    u32 sectors;
} rec_part_t;

typedef struct {
    u8         code[446];
    rec_part_t parts[4];
    u16        sig;
} rec_mbr_t;
#pragma pack(pop)

/* ---------- shared helpers ---------- */

static const char *mb_state_name(u32 st) {
    switch (st) {
    case OCIRE_MB_STATE_NONE:        return "NONE";
    case OCIRE_MB_STATE_WAIT_SIGNAL: return "WAIT_BOOT_SIGNAL";
    case OCIRE_MB_STATE_SIGNAL_OK:   return "SIGNAL_OK";
    case OCIRE_MB_STATE_ENTER_FULL:  return "ENTER_FULL";
    default:                         return "INVALID";
    }
}

/* Manifest path ("BOOT/GRUB/GRUB.CFG") -> golden volume path
 * ("GOLDEN/GRUB.CFG"): the golden area is FLAT by basename. */
static void golden_path_of(const char *manifest_path, char *out, u32 cap) {
    const char *base = manifest_path;
    for (const char *p = manifest_path; *p; p++) {
        if (*p == '/') base = p + 1;
    }
    int i = 0;
    const char *g = "GOLDEN/";
    for (; *g && (u32)i < cap - 1; g++) out[i++] = *g;
    for (; *base && (u32)i < cap - 1; base++) out[i++] = *base;
    out[i] = 0;
}

/* Parse the boot-log header.  Returns 0 and fills *used and *seq on a
 * valid ring, -1 when the file cannot be read, -2 on bad magic or
 * an out-of-range cursor. */
static int log_ring_facts(u32 *used, u32 *seq, u64 *cap_out) {
    ocire_layout_t *L = (ocire_layout_t *)ocire_layout();
    u8 hdr[512];
    s64 got = ocire_fat_read_range(&L->ocire_vol, &L->log_file, 0,
                                   hdr, 512);
    if (got != 512) return -1;
    u32 cap = L->mb.log_sectors * 512 - 512;
    if (cap_out) *cap_out = cap;
    if (oc_memcmp(hdr, OCIRE_LOG_MAGIC, 9) != 0) return -2;
    u32 w = (u32)(hdr[9] | (hdr[10] << 8) | (hdr[11] << 16) |
                  ((u32)hdr[12] << 24));
    u32 s = (u32)(hdr[13] | (hdr[14] << 8) | (hdr[15] << 16));
    if (w > cap) return -2;
    *used = w;
    *seq = s;
    return 0;
}

/* Read the whole log payload [0, used) and print it. */
static void log_ring_dump(u32 used) {
    ocire_layout_t *L = (ocire_layout_t *)ocire_layout();
    static u8 sec[512];
    ocire_puts("---- boot log begin ----\n");
    u64 off = 512;                       /* skip the header sector */
    while (off < 512 + used) {
        u64 take = used + 512 - off;
        if (take > 512) take = 512;
        s64 got = ocire_fat_read_range(&L->ocire_vol, &L->log_file, off,
                                       sec, take);
        if (got <= 0) break;
        for (s64 i = 0; i < got; i++) {
            u8 c = sec[i];
            ocire_putc((c == 0 || c == '\r') ? ' ' : (char)c);
        }
        off += (u64)got;
    }
    ocire_puts("\n---- boot log end ----\n");
}

/* Hash AGENT/AGENT.ELF into digest.  Returns 0. */
static int agent_digest(u8 digest[32]) {
    ocire_layout_t *L = (ocire_layout_t *)ocire_layout();
    ocire_fat_file_t f;
    if (ocire_fat_lookup(&L->ocire_vol, "AGENT/AGENT.ELF", &f) != 0)
        return -1;
    return ocire_fat_hash_file(&L->ocire_vol, &f, digest, (u64 *)0);
}

/* Compare two 64-hex strings. */
static int hex64_eq(const char *a, const char *b) {
    for (int i = 0; i < 64; i++) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'F') x = (char)(x - 'A' + 'a');
        if (y >= 'A' && y <= 'F') y = (char)(y - 'A' + 'a');
        if (x != y) return 0;
    }
    return 1;
}

/* Reboot now (same sequence as the shell's reboot). */
static void reboot_now(void) {
    for (int i = 0; i < 100000; i++) {
        if (!(oc_inb(0x64) & 2)) break;
    }
    oc_outb(0x64, 0xFE);
    ocire_acpi_reset();
    oc_outb(0xCF9, 0x02);
    oc_outb(0xCF9, 0x06);
    for (volatile int i = 0; i < 2000000; i++) { }
    ocire_puts("Reset failed on this machine; halting.\n");
    oc_halt();
}

/* ---------- logread ---------- */

void ocire_cmd_logread(void) {
    if (ocire_disk_scan() != 0) {
        ocire_puts("logread: recovery volume not found (run syscheck).\n");
        return;
    }
    u32 used = 0, seq = 0;
    u64 cap = 0;
    int rc = log_ring_facts(&used, &seq, &cap);
    if (rc == -1) {
        ocire_puts("logread: cannot read /LOG/BOOT.LOG.\n");
        return;
    }
    if (rc == -2) {
        ocire_puts("logread: BAD ring header (magic/cursor) - "
                   "use 'repair quick' to reset it.\n");
        return;
    }
    ocire_putdec("Boot log: ", cap);
    ocire_puts(" bytes capacity, ");
    ocire_putdec("", used);
    ocire_puts(" bytes used, boot sequence ");
    ocire_putdec("", seq);
    ocire_puts("\n");
    if (used == 0) {
        ocire_puts("  (no entries recorded)\n");
        return;
    }
    log_ring_dump(used);
}

/* ---------- syscheck ---------- */

/* One system-file check against the manifest.  Returns:
 *   0 OK, 1 digest DIFF, 2 MISSING, -1 I/O error. */
static int check_system_file(const char *path, const char *sha_hex,
                             u8 digest_out[32]) {
    ocire_layout_t *L = (ocire_layout_t *)ocire_layout();
    ocire_fat_file_t f;
    if (ocire_fat_lookup(&L->ocos_vol, path, &f) != 0) return 2;
    if (ocire_fat_hash_file(&L->ocos_vol, &f, digest_out, (u64 *)0) != 0)
        return -1;
    char hex[65];
    ocire_sha256_hex(digest_out, hex);
    return hex64_eq(hex, sha_hex) ? 0 : 1;
}

void ocire_cmd_syscheck(void) {
    int ok = 0, bad = 0;
    ocire_puts("OCIRE syscheck\n");

    /* 1. disks */
    int nd = ocire_ata_scan();
    ocire_putdec("  1. Disks: ", (u64)nd);
    ocire_puts(" ATA disk(s) ... ");
    if (nd > 0) { ocire_puts("OK\n"); ok++; }
    else { ocire_puts("FAIL\n"); bad++; }

    /* 2/3. volumes */
    int src = ocire_disk_scan();
    ocire_layout_t *L = (ocire_layout_t *)ocire_layout();
    ocire_puts("  2. Recovery volume (0xEE + FAT12) ... ");
    if (src != -1 && src != -3) {
        ocire_putdec("OK (disk ", (u64)L->ocire_idx);
        ocire_putdec(", LBA ", L->ocire_part_lba);
        ocire_puts(")\n");
        ok++;
    } else {
        ocire_puts("FAIL\n");
        bad++;
    }
    ocire_puts("  3. System volume (FAT32) ... ");
    if (src != -2) {
        ocire_putdec("OK (disk ", (u64)L->ocos_idx);
        ocire_putdec(", LBA ", L->ocos_part_lba);
        ocire_puts(")\n");
        ok++;
    } else {
        ocire_puts("FAIL\n");
        bad++;
    }
    if (src != 0) {
        ocire_puts("  4-10. skipped (volumes incomplete)\n");
        ocire_puts("Result: ");
        ocire_putdec("", (u64)ok);
        ocire_puts(" checks OK, ");
        ocire_putdec("", (u64)bad + 7);
        ocire_puts(" problems found\n");
        return;
    }

    /* 4. mailbox */
    ocire_puts("  4. Mailbox ... ");
    {
        int rc = ocire_mailbox_read();
        const ocire_mailbox_t *mb = &L->mb;
        if (rc == 0 && mb->state <= OCIRE_MB_STATE_ENTER_FULL) {
            ocire_puts("OK (state ");
            ocire_puts(mb_state_name(mb->state));
            ocire_putdec(", boots ", (u64)mb->boot_count);
            ocire_putdec(", timeouts ", (u64)mb->fail_count);
            ocire_puts(")\n");
            ok++;
        } else {
            ocire_puts("BAD (magic/state invalid)\n");
            bad++;
        }
    }

    /* 5. log ring */
    ocire_puts("  5. Boot log ring ... ");
    {
        u32 used = 0, seq = 0;
        u64 cap = 0;
        int rc = log_ring_facts(&used, &seq, &cap);
        if (rc == 0) {
            ocire_putdec("OK (", cap);
            ocire_putdec(" bytes, ", used);
            ocire_puts(" used)\n");
            ok++;
        } else {
            ocire_puts("BAD (magic/cursor invalid)\n");
            bad++;
        }
    }

    /* 6. log agent digest vs mailbox */
    ocire_puts("  6. Log agent ... ");
    {
        u8 dg[32];
        if (agent_digest(dg) == 0 &&
            ocire_sha256_eq(dg, L->mb.agent_sha256) == 0) {
            ocire_puts("OK (SHA-256 matches the mailbox)\n");
            ok++;
        } else {
            ocire_puts("MISMATCH (agent image vs mailbox digest)\n");
            bad++;
        }
    }

    /* 7. golden manifest */
    ocire_puts("  7. Golden manifest ... ");
    ocire_golden_t g;
    if (ocire_golden_load(&g) == 0 && g.n > 0) {
        ocire_putdec("OK (", (u64)g.n);
        ocire_puts(" entries)\n");
        ok++;
    } else {
        ocire_puts("FAIL (cannot parse)\n");
        ocire_puts("  8-10. skipped (no manifest)\n");
        ocire_puts("Result: ");
        ocire_putdec("", (u64)ok);
        ocire_puts(" checks OK, ");
        ocire_putdec("", (u64)bad + 3);
        ocire_puts(" problems found\n");
        return;
    }

    /* 8. golden files themselves (is the repair source intact?) */
    ocire_puts("  8. Golden files ... ");
    {
        int gok = 1;
        for (int i = 0; i < g.n; i++) {
            char gp[48];
            golden_path_of(g.e[i].path, gp, sizeof(gp));
            ocire_fat_file_t f;
            u8 dg[32];
            if (ocire_fat_lookup(&L->ocire_vol, gp, &f) != 0 ||
                ocire_fat_hash_file(&L->ocire_vol, &f, dg, (u64 *)0) != 0) {
                gok = 0;
                ocire_puts("\n       ");
                ocire_puts(gp);
                ocire_puts(" ... MISSING/UNREADABLE");
                continue;
            }
            char hex[65];
            ocire_sha256_hex(dg, hex);
            if (!hex64_eq(hex, g.e[i].sha_hex)) {
                gok = 0;
                ocire_puts("\n       ");
                ocire_puts(gp);
                ocire_puts(" ... DIGEST MISMATCH");
            }
        }
        if (gok) { ocire_puts("OK (all match the manifest)\n"); ok++; }
        else { ocire_puts("\n       (golden area damaged)\n"); bad++; }
    }

    /* 9. system files on the OCOS volume */
    ocire_puts("  9. System files:\n");
    {
        int sys_bad = 0;
        for (int i = 0; i < g.n; i++) {
            u8 dg[32];
            int rc = check_system_file(g.e[i].path, g.e[i].sha_hex, dg);
            ocire_puts("       ");
            ocire_puts(g.e[i].path);
            ocire_puts(" ... ");
            if (rc == 0) {
                ocire_putdec("OK (", g.e[i].size);
                ocire_puts(" bytes)\n");
            } else if (rc == 2) {
                ocire_puts("MISSING\n");
                sys_bad++;
            } else if (rc == 1) {
                ocire_puts("DIGEST MISMATCH\n");
                sys_bad++;
            } else {
                ocire_puts("I/O ERROR\n");
                sys_bad++;
            }
        }
        if (sys_bad == 0) ok++; else bad++;
    }

    /* 10. recovery kernel copy (hd1 /BOOT/OCIRE.ELF vs manifest) */
    ocire_puts(" 10. Recovery kernel copy (hd1) ... ");
    {
        const char *msha = (const char *)0;
        for (int i = 0; i < g.n; i++) {
            if (oc_strcmp(g.e[i].path, "BOOT/OCIRE.ELF") == 0)
                msha = g.e[i].sha_hex;
        }
        ocire_fat_file_t f;
        u8 dg[32];
        char hex[65];
        if (msha &&
            ocire_fat_lookup(&L->ocire_vol, "BOOT/OCIRE.ELF", &f) == 0 &&
            ocire_fat_hash_file(&L->ocire_vol, &f, dg, (u64 *)0) == 0) {
            ocire_sha256_hex(dg, hex);
            if (hex64_eq(hex, msha)) { ocire_puts("OK\n"); ok++; }
            else { ocire_puts("DIGEST MISMATCH\n"); bad++; }
        } else {
            ocire_puts("FAIL (missing)\n");
            bad++;
        }
    }

    ocire_puts("Result: ");
    ocire_putdec("", (u64)ok);
    ocire_puts(" checks OK, ");
    ocire_putdec("", (u64)bad);
    ocire_puts(" problems found\n");
    if (bad == 0) ocire_puts("System healthy.\n");
}

/* ---------- repair helpers ---------- */

/* Check + rebuild a disk MBR.  which: 0 = the OCOS boot disk
 * (bootable FAT32 p1), 1 = the OCIRE disk (0xEE shadow p1).
 * Returns 0 when the MBR was already correct, 1 when it was
 * rebuilt, negative on I/O error. */
static int fix_mbr(int which) {
    ocire_layout_t *L = (ocire_layout_t *)ocire_layout();
    int idx = which == 0 ? L->ocos_idx : L->ocire_idx;
    u8 sec[512];
    if (ocire_ata_read(idx, 0, 1, sec) != 0) return -1;
    rec_mbr_t *m = (rec_mbr_t *)sec;
    u32 want_lba = (u32)(which == 0 ? L->ocos_part_lba : L->ocire_part_lba);
    u8  want_type = (u8)(which == 0 ? 0x0C : 0xEE);
    u8  want_boot = (u8)(which == 0 ? 0x80 : 0x00);
    u32 want_secs = (u32)(which == 0 ? L->ocos_part_sectors : 0);

    int good = (m->sig == 0xAA55) &&
               (m->parts[0].boot == want_boot) &&
               (m->parts[0].type == want_type) &&
               (m->parts[0].start_lba == want_lba) &&
               (which == 1 || m->parts[0].sectors == want_secs) &&
               (which == 0 ||
                (m->parts[1].type == 0 && m->parts[2].type == 0 &&
                 m->parts[3].type == 0));
    if (good) return 0;

    oc_memset(sec, 0, 512);
    rec_mbr_t *w = (rec_mbr_t *)sec;
    w->parts[0].boot = want_boot;
    w->parts[0].start_chs[0] = 0x20;
    w->parts[0].start_chs[1] = 0x21;
    w->parts[0].start_chs[2] = 0x00;
    w->parts[0].type = want_type;
    w->parts[0].end_chs[0] = 0xFE;
    w->parts[0].end_chs[1] = 0xFF;
    w->parts[0].end_chs[2] = 0xFF;
    w->parts[0].start_lba = want_lba;
    w->parts[0].sectors = want_secs;
    w->sig = 0xAA55;
    if (ocire_ata_write(idx, 0, 1, sec) != 0) return -2;
    return 1;
}

/* Reset the boot-log ring: zero the whole file + write a fresh
 * header.  Returns 0 or negative. */
static int reset_log_ring(void) {
    ocire_layout_t *L = (ocire_layout_t *)ocire_layout();
    u8 zeros[512];
    oc_memset(zeros, 0, 512);
    u64 cap = L->mb.log_sectors * 512;
    for (u64 off = 0; off < cap; off += 512) {
        if (ocire_fat_file_patch(&L->ocire_vol, &L->log_file, off,
                                 zeros, 512) != 0) return -1;
    }
    u8 hdr[OCIRE_LOG_HDR_BYTES];
    oc_memset(hdr, 0, sizeof(hdr));
    oc_memcpy(hdr, OCIRE_LOG_MAGIC, 9);
    return ocire_fat_file_patch(&L->ocire_vol, &L->log_file, 0,
                                hdr, OCIRE_LOG_HDR_BYTES);
}

/* Rebuild the mailbox from on-disk facts (state NONE, counters
 * zero, fresh agent digest, log LBA from the log file). */
static int rebuild_mailbox(void) {
    ocire_layout_t *L = (ocire_layout_t *)ocire_layout();
    oc_memset(&L->mb, 0, sizeof(L->mb));
    oc_memcpy(L->mb.magic, OCIRE_MAILBOX_MAGIC, 8);
    L->mb.state = OCIRE_MB_STATE_NONE;
    /* /LOG/BOOT.LOG may be missing (a -3 scan): leave the log facts
     * zeroed rather than computing an LBA from an unfilled file.
     * The agent digest is independent of the log and is always
     * filled when the agent image is readable. */
    if (L->log_file.size == 0) {
        L->mb.log_start_lba = 0;
        L->mb.log_sectors = 0;
    } else {
        L->mb.log_start_lba =
            (u32)ocire_fat_file_first_lba(&L->ocire_vol, &L->log_file);
        L->mb.log_sectors = (u32)(L->log_file.size / 512);
    }
    if (agent_digest(L->mb.agent_sha256) != 0) {
        oc_memset(L->mb.agent_sha256, 0, 32);
    }
    return ocire_mailbox_write();
}

/* Restore one system file from the golden area (streaming) and
 * verify the result.  Returns 0 on verified success. */
static int restore_system_file(const char *manifest_path,
                               const char *sha_hex, u64 *bytes_out) {
    ocire_layout_t *L = (ocire_layout_t *)ocire_layout();
    char gp[48];
    golden_path_of(manifest_path, gp, sizeof(gp));
    ocire_fat_file_t src;
    if (ocire_fat_lookup(&L->ocire_vol, gp, &src) != 0) return -1;
    static u8 chunk[8192];
    if (ocire_fat_copy_stream(&L->ocire_vol, &src,
                              (ocire_fat_vol_t *)&L->ocos_vol,
                              manifest_path, chunk, sizeof(chunk),
                              bytes_out) != 0) return -2;
    /* verify */
    ocire_fat_file_t f;
    u8 dg[32];
    char hex[65];
    if (ocire_fat_lookup(&L->ocos_vol, manifest_path, &f) != 0) return -3;
    if (ocire_fat_hash_file(&L->ocos_vol, &f, dg, (u64 *)0) != 0) return -3;
    ocire_sha256_hex(dg, hex);
    return hex64_eq(hex, sha_hex) ? 0 : -4;
}

/* ---------- repair quick ---------- */

void ocire_cmd_repair_quick(void) {
    ocire_puts("OCIRE repair (quick mode)\n");
    if (ocire_disk_scan() != 0) {
        ocire_puts("  Volumes incomplete - repair needs both the "
                   "recovery and the system volume. Aborting.\n");
        return;
    }
    ocire_layout_t *L = (ocire_layout_t *)ocire_layout();
    ocire_golden_t g;
    if (ocire_golden_load(&g) != 0) {
        ocire_puts("  Golden manifest unreadable - quick repair "
                   "cannot verify files. Aborting.\n");
        return;
    }

    ocire_puts("  1. Boot-disk MBR ... ");
    {
        int rc = fix_mbr(0);
        if (rc == 0) ocire_puts("OK\n");
        else if (rc > 0) ocire_puts("REBUILT\n");
        else ocire_puts("I/O ERROR\n");
    }
    ocire_puts("  2. Recovery-disk MBR (0xEE) ... ");
    {
        int rc = fix_mbr(1);
        if (rc == 0) ocire_puts("OK\n");
        else if (rc > 0) ocire_puts("REBUILT\n");
        else ocire_puts("I/O ERROR\n");
    }
    ocire_puts("  3. Mailbox ... ");
    {
        int rc = ocire_mailbox_read();
        if (rc == 0 && L->mb.state <= OCIRE_MB_STATE_ENTER_FULL) {
            ocire_puts("OK (state ");
            ocire_puts(mb_state_name(L->mb.state));
            ocire_puts(")\n");
        } else {
            int rb = rebuild_mailbox();
            if (rb == 0) ocire_puts("REBUILT (state NONE)\n");
            else ocire_puts("REBUILD FAILED\n");
        }
    }
    ocire_puts("  4. Boot log ring header ... ");
    {
        u32 used = 0, seq = 0;
        u64 cap = 0;
        if (log_ring_facts(&used, &seq, &cap) == 0) {
            ocire_puts("OK\n");
        } else {
            u8 hdr[OCIRE_LOG_HDR_BYTES];
            oc_memset(hdr, 0, sizeof(hdr));
            oc_memcpy(hdr, OCIRE_LOG_MAGIC, 9);
            if (ocire_fat_file_patch(&L->ocire_vol, &L->log_file, 0,
                                     hdr, OCIRE_LOG_HDR_BYTES) == 0)
                ocire_puts("RESET\n");
            else
                ocire_puts("RESET FAILED\n");
        }
    }
    ocire_puts("  5. System files:\n");
    {
        int restored = 0, unchanged = 0, failed = 0;
        for (int i = 0; i < g.n; i++) {
            ocire_puts("       ");
            ocire_puts(g.e[i].path);
            ocire_puts(" ... ");
            u8 dg[32];
            int rc = check_system_file(g.e[i].path, g.e[i].sha_hex, dg);
            if (rc == 0) {
                ocire_puts("OK\n");
                unchanged++;
                continue;
            }
            u64 nbytes = 0;
            if (restore_system_file(g.e[i].path, g.e[i].sha_hex,
                                    &nbytes) == 0) {
                ocire_putdec("RESTORED (", nbytes);
                ocire_puts(" bytes, verified)\n");
                restored++;
            } else {
                ocire_puts("FAILED\n");
                failed++;
            }
        }
        ocire_puts("Result: quick repair complete - ");
        ocire_putdec("", (u64)restored);
        ocire_puts(" restored, ");
        ocire_putdec("", (u64)unchanged);
        ocire_puts(" unchanged, ");
        ocire_putdec("", (u64)failed);
        ocire_puts(" failed\n");
        if (failed == 0 && restored == 0)
            ocire_puts("Nothing needed repair.\n");
    }
}

/* ---------- repair full ---------- */

void ocire_cmd_repair_full(void) {
    ocire_puts("OCIRE repair (full mode)\n");
    ocire_puts("WARNING: this REFORMATS the OpenCubeOS system volume;\n");
    ocire_puts("all data on it will be lost.  Type YES to continue: ");
    static char ans[16];
    int n = 0;
    for (;;) {
        char c;
        while (!ocire_getchar(&c)) { }
        if (c == '\n') { ocire_putc('\n'); break; }
        if (c == '\b' || c == 0x7F) {
            if (n > 0) { n--; ocire_putc('\b'); ocire_putc(' '); ocire_putc('\b'); }
            continue;
        }
        if (c == '\r') continue;
        if (n < 15) { ans[n++] = c; ocire_putc(c); }
    }
    ans[n] = 0;
    if (oc_strcmp(ans, "YES") != 0) {
        ocire_puts("Cancelled (nothing was written).\n");
        return;
    }

    int src = ocire_disk_scan();
    if (src == -1) {
        ocire_puts("  No recovery volume - the golden image is the "
                   "repair source. Aborting.\n");
        return;
    }
    ocire_layout_t *L = (ocire_layout_t *)ocire_layout();
    if (src != 0) {
        /* The system volume does not MOUNT (destroyed BPB / empty
         * partition) - which is exactly what repair full exists to
         * rebuild.  Locate the partition by its MBR entry instead of
         * by mounting it: scan every non-recovery disk for the first
         * plausible data partition. */
        int found = 0;
        int nd = ocire_ata_scan();
        for (int i = 0; i < nd && !found; i++) {
            if (i == L->ocire_idx) continue;
            const ocire_ata_dev_t *d = ocire_ata_dev(i);
            if (!d->present) continue;
            u8 sec[512];
            if (ocire_ata_read(i, 0, 1, sec) != 0) continue;
            rec_mbr_t *m = (rec_mbr_t *)sec;
            if (m->sig != 0xAA55) continue;
            for (int p = 0; p < 4 && !found; p++) {
                if (m->parts[p].type == 0 || m->parts[p].type == 0xEE)
                    continue;
                u64 lba = m->parts[p].start_lba;
                u64 secs = m->parts[p].sectors;
                if (lba < 2048 || secs < 2048) continue;
                L->ocos_idx = i;
                L->ocos_part_lba = lba;
                L->ocos_part_sectors = secs;
                found = 1;
            }
        }
        if (!found) {
            ocire_puts("  System volume unmountable AND no partition "
                       "entry to rebuild from (MBR empty too). "
                       "Aborting.\n");
            return;
        }
        ocire_puts("  System volume unmountable - partition located "
                   "by MBR entry (disk ");
        ocire_putdec("", (u64)L->ocos_idx);
        ocire_putdec(", LBA ", L->ocos_part_lba);
        ocire_puts("); rebuilding it.\n");
    }
    ocire_golden_t g;
    if (ocire_golden_load(&g) != 0) {
        ocire_puts("  Golden manifest unreadable - the golden image "
                   "is the repair source. Aborting.\n");
        return;
    }

    ocire_puts("  1. Boot-disk MBR ... ");
    {
        int rc = fix_mbr(0);
        if (rc == 0) ocire_puts("OK\n");
        else if (rc > 0) ocire_puts("REBUILT\n");
        else ocire_puts("I/O ERROR\n");
    }
    ocire_puts("  2. Recovery-disk MBR (0xEE) ... ");
    {
        int rc = fix_mbr(1);
        if (rc == 0) ocire_puts("OK\n");
        else if (rc > 0) ocire_puts("REBUILT\n");
        else ocire_puts("I/O ERROR\n");
    }
    ocire_puts("  3. System volume format (FAT32) ... ");
    if (ocire_fat_format(&L->ocos_vol, L->ocos_idx, L->ocos_part_lba,
                         L->ocos_part_sectors, "OCOS_SYS") == 0) {
        ocire_puts("OK\n");
    } else {
        ocire_puts("FAILED - aborting (volume left as-is).\n");
        return;
    }
    ocire_puts("  4. Directories ... ");
    {
        static const char *dirs[] = {
            "BOOT", "BOOT/GRUB", "BOOT/GRUB/FONTS", "EFI", "EFI/BOOT"
        };
        int fail = 0;
        for (u32 i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
            if (ocire_fat_mkdir(&L->ocos_vol, dirs[i]) != 0) fail = 1;
        }
        ocire_puts(fail ? "FAILED\n" : "OK\n");
    }
    ocire_puts("  5. System files (from the golden image):\n");
    {
        int okc = 0, failed = 0;
        u64 total = 0;
        for (int i = 0; i < g.n; i++) {
            ocire_puts("       ");
            ocire_puts(g.e[i].path);
            ocire_puts(" ... ");
            u64 nbytes = 0;
            if (restore_system_file(g.e[i].path, g.e[i].sha_hex,
                                    &nbytes) == 0) {
                ocire_putdec("RESTORED (", nbytes);
                ocire_puts(" bytes, verified)\n");
                okc++;
                total += nbytes;
            } else {
                ocire_puts("FAILED\n");
                failed++;
            }
        }
        if (failed) {
            ocire_putdec("Result: full repair INCOMPLETE - ", (u64)okc);
            ocire_putdec(" restored, ", (u64)failed);
            ocire_puts(" failed (re-run or use another golden set)\n");
            return;
        }
        ocire_puts("  6. Boot log ring ... ");
        if (L->log_file.size == 0) {
            ocire_puts("SKIPPED (no /LOG/BOOT.LOG on the recovery "
                       "volume)\n");
        } else if (reset_log_ring() == 0) ocire_puts("RESET\n");
        else ocire_puts("RESET FAILED\n");
        ocire_puts("  7. Mailbox ... ");
        if (rebuild_mailbox() == 0) ocire_puts("RESET (state NONE)\n");
        else ocire_puts("RESET FAILED\n");
        ocire_puts("Result: full repair complete - system volume "
                   "rebuilt (");
        ocire_putdec("", (u64)okc);
        ocire_puts(" files, ");
        ocire_putdec("", total);
        ocire_puts(" bytes)\n");
        ocire_puts("Boot the system with 'return' or 'reboot'.\n");
    }
}

/* ---------- return ---------- */

void ocire_cmd_return(void) {
    if (ocire_disk_scan() != 0) {
        ocire_puts("return: volumes incomplete - cannot mark a "
                   "normal boot. Use 'reboot'.\n");
        return;
    }
    ocire_layout_t *L = (ocire_layout_t *)ocire_layout();
    L->mb.state = OCIRE_MB_STATE_NONE;
    if (ocire_mailbox_write() == 0) {
        ocire_puts("Mailbox: normal boot (state NONE).\n");
    } else {
        ocire_puts("Mailbox write FAILED - rebooting anyway.\n");
    }
    ocire_puts("Rebooting into OpenCubeOS...\n");
    reboot_now();                        /* never returns */
}
