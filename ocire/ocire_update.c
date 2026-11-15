/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP07-ocire-loop-perfect
 * File: ocire/ocire_update.c
 * Purpose: the WP-07 secure OTA commands for the OCIRE shell.
 *
 *   update   - the ONLY path that modifies OCIRE itself:
 *              1. find the ESP (GPT EFI System Partition / MBR
 *                 0xEF/0x0C FAT32) and mount it read-only
 *              2. read \EFI\OpenCube\ocire-update.bin + .sig
 *                 (long file names, matched case-insensitively)
 *              3. Ed25519-verify the payload against the built-in
 *                 public key - a forged/unsigned/corrupt package is
 *                 REFUSED before anything is written
 *              4. sanity-check the payload as an ELF (magic +
 *                 class + a multiboot2 header in the first 32 KiB)
 *              5. flash it to the INACTIVE slot as /BOOT/OCIRE.ELF
 *              6. read the file back and SHA-256-compare it
 *              7. point the A/B flags at the new slot
 *                 (try_slot = target, attempts = 0) - the next boot
 *                 tries it and falls back automatically on failure
 *              NOTHING here ever scans the network or runs on a
 *              timer: the update happens strictly because the user
 *              typed `update` at the prompt.
 *
 *   ls       - list a directory on the ESP (or the active slot
 *              volume with `ls slot:`) - the FAT32 mount surface.
 *
 *   slots    - dump the recovery-disk A/B map + flags.
 */
#include "ocire_update.h"
#include "ocire_console.h"
#include "ocire_util.h"
#include "ocire_disk.h"
#include "ocire_fat.h"
#include "ocire_ata.h"
#include "ocire_sha256.h"
#include "ocire_ed25519.h"

/* the update payload cap: the OCIRE kernel is a few hundred KiB;
 * 4 MiB leaves room for a considerably fatter future kernel */
#define OCIRE_UPDATE_CAP  (4 * 1024 * 1024)

/* where the user drops the package on the ESP (LFN, case-insens) */
#define OCIRE_UPDATE_PATH "EFI/OPENCUBE/ocire-update.bin"
#define OCIRE_UPDATE_SIG  "EFI/OPENCUBE/ocire-update.sig"

/* ---- helpers ------------------------------------------------------- */

static const char* slot_name(int s) {
    return s == 1 ? "A" : s == 2 ? "B" : "?";
}

/* ---- `slots` ------------------------------------------------------- */

void ocire_cmd_slots(void) {
    ocire_layout_t *L = (ocire_layout_t *)ocire_layout();

    ocire_puts("Recovery disk: ");
    if (L->ocire_idx < 0) {
        ocire_puts("not found\n");
        return;
    }
    ocire_putdec("ata", (u64)L->ocire_idx);
    if (!L->dual_slot) {
        ocire_puts(" - single-slot media (legacy 0xEE FAT12 or one "
                   "FAT32 slot), no A/B\n");
        return;
    }
    ocire_puts(", dual independent partitions:\n");
    for (int s = 1; s <= 2; s++) {
        ocire_puts("  slot ");
        ocire_puts(slot_name(s));
        ocire_putdec(": LBA ", L->slot_lba[s]);
        ocire_putdec(", ", L->slot_sectors[s] / 2048);
        ocire_puts(" MiB");
        if (L->my_slot == s) ocire_puts("  <- running from here");
        ocire_puts("\n");
    }
    if (L->ab_valid) {
        ocire_puts("  flags: active=");
        ocire_puts(slot_name(L->ab.active_slot));
        ocire_putdec(", try=", (u64)(L->ab.try_slot ? L->ab.try_slot : 0));
        ocire_putdec(", attempts=", (u64)L->ab.attempts);
        ocire_putdec(", boots=", (u64)L->ab.boot_count);
        ocire_puts(L->ab.flags & OCIRE_AB_F_DIRTY ? ", DIRTY" : "");
        ocire_puts("\n");
    } else {
        ocire_puts("  flags: sector invalid or absent (defaults in use)\n");
    }
}

/* ---- `ls` ---------------------------------------------------------- */

void ocire_cmd_ls(const char *arg) {
    ocire_layout_t *L = (ocire_layout_t *)ocire_layout();

    /* which volume: default the ESP; "slot:" prefix = the active
     * recovery slot volume; "slotX:" = slot A/B by letter */
    ocire_fat_vol_t *v = 0;
    const char *path = "";
    static ocire_fat_vol_t slotvol;
    if (arg[0]) {
        if (oc_strncmp(arg, "slot", 4) == 0 &&
            (arg[4] == ':' || (arg[4] && arg[5] == ':'))) {
            int s = L->my_slot;
            if (arg[4] == 'A' || arg[4] == 'a') s = 1;
            else if (arg[4] == 'B' || arg[4] == 'b') s = 2;
            else if (arg[4] == ':') s = L->my_slot;
            else {
                ocire_puts("usage: ls [slotA:|slotB:|slot:][path]\n");
                return;
            }
            const char *rest = (arg[4] == ':') ? arg + 5 : arg + 6;
            if (ocire_slot_mount(L, s, &slotvol) != 0) {
                ocire_puts("Cannot mount slot ");
                ocire_putdec("", (u64)s);
                ocire_puts(" (absent or not a FAT32 OCIRE slot).\n");
                return;
            }
            v = &slotvol;
            path = rest;
        }
    }
    if (!v) {
        if (L->esp_idx < 0) {
            ocire_puts("No ESP found (no GPT EFI System Partition or "
                       "MBR 0xEF/0x0C FAT32 volume with an /EFI tree).\n");
            return;
        }
        v = &L->esp_vol;
        path = arg;                     /* plain path on the ESP */
        ocire_putdec("ESP: ata", (u64)L->esp_idx);
        ocire_putdec(" LBA ", L->esp_part_lba);
        ocire_puts(" (FAT32)\n");
    }

    static ocire_fat_dirent_t ents[48];
    int n = ocire_fat_list_dir(v, path, ents, 48);
    if (n < 0) {
        ocire_puts("Cannot list '");
        ocire_puts(path);
        ocire_puts("' (not found or not a directory).\n");
        return;
    }
    for (int i = 0; i < n; i++) {
        ocire_puts((ents[i].attr & 0x10) ? "  <DIR>  " : "         ");
        ocire_putdec("", ents[i].size);
        ocire_puts("  ");
        ocire_puts(ents[i].name);
        ocire_puts("\n");
    }
    ocire_putdec("", (u64)n);
    ocire_puts(" entr(y/ies)\n");
}

/* ---- `update` ------------------------------------------------------ */

/* minimal ELF sanity: 64-bit ELF with a multiboot2 header in the
 * first 32 KiB (the OCBL/GRUB load contract) */
static int payload_is_kernel(const u8 *img, u64 len) {
    if (len < 64 || len > OCIRE_UPDATE_CAP) return 0;
    if (img[0] != 0x7F || img[1] != 'E' || img[2] != 'L' || img[3] != 'F')
        return 0;
    if (img[4] != 2 || img[5] != 1) return 0;   /* 64-bit, LE */
    u64 scan = len < 32768 ? len : 32768;
    for (u64 off = 0; off + 4 <= scan; off += 4)
        if (img[off] == 0xD6 && img[off + 1] == 0x50 &&
            img[off + 2] == 0x52 && img[off + 3] == 0xE8)
            return 1;                            /* 0xE85250D6 LE */
    return 0;
}

int ocire_cmd_update(void) {
    ocire_layout_t *L = (ocire_layout_t *)ocire_layout();

    ocire_puts("OCIRE update: strictly manual, no network, no timers.\n");

    if (L->ocire_idx < 0) {
        ocire_puts("update: no recovery disk found - nothing to update.\n");
        return -1;
    }
    if (!L->dual_slot) {
        ocire_puts("update: this recovery medium is single-slot "
                   "(legacy FAT12); the dual-slot OTA needs the WP-07 "
                   "A/B disk.\n");
        return -1;
    }
    if (L->esp_idx < 0) {
        ocire_puts("update: no ESP found. Place the signed package at\n"
                   "  \\EFI\\OpenCube\\ocire-update.bin (+ .sig)\n"
                   "on the EFI System Partition and retry.\n");
        return -1;
    }

    /* 1. locate the package + the signature on the ESP */
    ocire_fat_file_t pf, sf;
    if (ocire_fat_lookup(&L->esp_vol, OCIRE_UPDATE_PATH, &pf) != 0) {
        ocire_puts("update: \\EFI\\OpenCube\\ocire-update.bin not found "
                   "on the ESP.\n");
        return -1;
    }
    if (ocire_fat_lookup(&L->esp_vol, OCIRE_UPDATE_SIG, &sf) != 0) {
        ocire_puts("update: \\EFI\\OpenCube\\ocire-update.sig not found "
                   "on the ESP.\n");
        return -1;
    }
    ocire_putdec("update: package ", pf.size);
    ocire_puts(" bytes, signature ");
    ocire_putdec("", sf.size);
    ocire_puts(" bytes\n");
    if (pf.size == 0 || pf.size > OCIRE_UPDATE_CAP) {
        ocire_puts("update: package size out of range (0 < size <= 4 MiB)."
                   "  REFUSED.\n");
        return -1;
    }
    if (sf.size != 64) {
        ocire_puts("update: signature must be exactly 64 bytes.  REFUSED.\n");
        return -1;
    }

    /* 2. read both */
    static u8 img[OCIRE_UPDATE_CAP];
    static u8 sig[64];
    s64 got = ocire_fat_read_file(&L->esp_vol, &pf, img, sizeof(img));
    if (got != (s64)pf.size) {
        ocire_puts("update: short read on the package.  REFUSED.\n");
        return -1;
    }
    if (ocire_fat_read_file(&L->esp_vol, &sf, sig, 64) != 64) {
        ocire_puts("update: short read on the signature.  REFUSED.\n");
        return -1;
    }

    /* 3. Ed25519 verification against the built-in public key */
    ocire_puts("update: verifying the Ed25519 signature...\n");
    int rc = ocire_ed25519_verify(img, (u64)got, sig);
    if (rc != 0) {
        ocire_puts("update: SIGNATURE VERIFICATION FAILED (rc ");
        ocire_putdec("", (u64)(-rc));
        ocire_puts(").  The package is not authentic.\n"
                   "update: FLASH REFUSED - nothing was written.\n");
        return -1;
    }
    ocire_puts("update: signature OK (Ed25519, built-in public key).\n");

    /* 4. payload sanity */
    if (!payload_is_kernel(img, (u64)got)) {
        ocire_puts("update: payload is not a 64-bit multiboot2 ELF "
                   "kernel.  REFUSED.\n");
        return -1;
    }

    /* 5. flash the INACTIVE slot */
    int target = ocire_ab_other_slot(L);
    if (target == 0) {
        ocire_puts("update: cannot determine the inactive slot.\n");
        return -1;
    }
    ocire_fat_vol_t tv;
    if (ocire_slot_mount(L, target, &tv) != 0) {
        ocire_puts("update: cannot mount the target slot volume.\n");
        return -1;
    }
    ocire_puts("update: flashing slot ");
    ocire_puts(slot_name(target));
    ocire_putdec(" (", L->slot_lba[target]);
    ocire_puts(") ...\n");
    if (ocire_fat_create_file(&tv, "BOOT/OCIRE.ELF", img, (u64)got) != 0) {
        ocire_puts("update: flash FAILED (FAT write error).  The old "
                   "slot content is untouched.\n");
        return -1;
    }

    /* 6. read-back + SHA-256 compare */
    {
        ocire_fat_file_t nf;
        if (ocire_fat_lookup(&tv, "BOOT/OCIRE.ELF", &nf) != 0) {
            ocire_puts("update: read-back FAILED (file missing).  "
                       "REFUSING to arm the slot.\n");
            return -1;
        }
        if (nf.size != (u64)got) {
            ocire_puts("update: read-back FAILED (size mismatch).  "
                       "REFUSING to arm the slot.\n");
            return -1;
        }
        u8 a[32], b[32];
        ocire_sha256(img, (u64)got, a);
        if (ocire_fat_hash_file(&tv, &nf, b, 0) != 0) {
            ocire_puts("update: read-back FAILED (hash error).  "
                       "REFUSING to arm the slot.\n");
            return -1;
        }
        if (oc_memcmp(a, b, 32) != 0) {
            ocire_puts("update: read-back FAILED (SHA-256 mismatch).  "
                       "REFUSING to arm the slot.\n");
            return -1;
        }
        char hex[65];
        ocire_sha256_hex(a, hex);
        ocire_puts("update: read-back verified, SHA-256 ");
        ocire_puts(hex);
        ocire_puts("\n");
    }

    /* 7. arm the target slot */
    if (ocire_ab_read(L) != 0 || !L->ab_valid) {
        oc_memset(&L->ab, 0, sizeof(L->ab));
        L->ab.active_slot = (u8)L->my_slot;
        L->ab_valid = 1;
    }
    L->ab.try_slot = (u8)target;
    L->ab.attempts = 0;
    L->ab.flags &= (u8)~OCIRE_AB_F_DIRTY;
    if (ocire_ab_write(L) != 0) {
        ocire_puts("update: FATAL - cannot write the A/B flags.  The "
                   "slot was flashed but will NOT be selected "
                   "automatically; boot stays on the current slot.\n");
        return -1;
    }

    ocire_puts("update: SUCCESS.  Slot ");
    ocire_puts(slot_name(target));
    ocire_puts(" now holds the new OCIRE; the next boot tries it "
               "first and falls back to slot ");
    ocire_puts(slot_name(L->my_slot));
    ocire_puts(" automatically if it fails.\n");
    ocire_puts("update: reboot to activate, or keep working here.\n");
    return 0;
}
