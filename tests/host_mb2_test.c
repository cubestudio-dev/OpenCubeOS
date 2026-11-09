/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-10-AUDIT_P2-fix1
 * File: tests/host_mb2_test.c
 *
 * BUG-0142 (A1-9) host regression: arch_multiboot2_parse() against
 * bootloader-supplied (half-trusted) multiboot2 info.
 *
 * Bidirectional assertions, all under ASAN so any out-of-bounds read the
 * pre-fix loop performed is a hard failure:
 *   ACCEPT (legitimate): a well-formed mbi (fb + mmap + cmdline +
 *     bootloader name + basic meminfo + "self" module) parses fully and
 *     every pointer/field lands where the spec says.
 *   REJECT (malicious): total_size over the sanity bound; a tag whose
 *     declared size runs past the mbi end; a truncated fb tag; a
 *     truncated meminfo tag; a truncated module tag - all must stop
 *     cleanly instead of dereferencing out-of-bounds fields.
 *
 * Build & run (host):
 *   cc -I kernel -I kernel/arch/x86_64 -I kernel/lib -fsanitize=address,undefined -g \
 *      -o build/host_mb2_test tests/host_mb2_test.c kernel/arch/x86_64/arch_multiboot2.c
 *   ./build/host_mb2_test
 */
#include "types.h"
#include "arch_multiboot2.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* The kernel file uses its own memset (freestanding); on the host libc
 * memset is fine - just satisfy the symbol. */

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (cond) { printf("  PASS: %s\n", msg); } \
    else { printf("  FAIL: %s\n", msg); failures++; } \
} while (0)

/* ---- builders ------------------------------------------------------- */
typedef struct { u8 *buf; u64 len; } mbi_t;

static void put32(u8 *p, u32 v) { memcpy(p, &v, 4); }
static void put64(u8 *p, u64 v) { memcpy(p, &v, 8); }

static mbi_t build_good_mbi(void) {
    /* layout: header | fb | mmap | cmdline | loader | meminfo | module | end */
    u64 fb_size  = sizeof(arch_multiboot2_fb_tag_t);
    u64 mmap_sz  = sizeof(arch_multiboot2_mmap_tag_t) + 2 * sizeof(arch_multiboot2_mmap_entry_t);
    const char *cmd = "oc root=/dev/sda1";
    u64 cmd_size = 8 + strlen(cmd) + 1;
    const char *ldr = "GRUB 2.12";
    u64 ldr_size = 8 + strlen(ldr) + 1;
    u64 meminfo_size = 16;
    const char *mcmd = "self";
    u64 mod_size = 16 + 5;   /* module header + "self\0" */
    u64 end_size = 8;

    u64 off = 8;
    u64 fb_off = off;      off += (fb_size + 7) & ~7ULL;
    u64 mm_off = off;      off += (mmap_sz + 7) & ~7ULL;
    u64 cmd_off = off;     off += (cmd_size + 7) & ~7ULL;
    u64 ldr_off = off;     off += (ldr_size + 7) & ~7ULL;
    u64 mi_off = off;      off += (meminfo_size + 7) & ~7ULL;
    u64 mod_off = off;     off += (mod_size + 7) & ~7ULL;
    u64 end_off = off;     off += end_size;
    u64 total = off;

    u8 *b = calloc(1, total);
    put32(b + 0, (u32)total);
    put32(b + 4, 0);

    put32(b + fb_off + 0, OC_MB2_TAG_FRAMEBUFFER);
    put32(b + fb_off + 4, (u32)fb_size);
    put64(b + fb_off + 8, 0xFD000000ULL);
    put32(b + fb_off + 16, 4096);   /* pitch */
    put32(b + fb_off + 20, 1024);   /* width */
    put32(b + fb_off + 24, 768);    /* height */

    put32(b + mm_off + 0, OC_MB2_TAG_MMAP);
    put32(b + mm_off + 4, (u32)mmap_sz);
    put32(b + mm_off + 8, 24);      /* entry_size */
    put32(b + mm_off + 12, 0);      /* entry_version */
    put64(b + mm_off + 16, 0x0000000000000000ULL); /* entry 0 addr */
    put64(b + mm_off + 24, 0x000000000009FC00ULL); /* entry 0 len */
    put32(b + mm_off + 32, 1);      /* available */
    put64(b + mm_off + 40, 0x0000000000100000ULL); /* entry 1 addr */
    put64(b + mm_off + 48, 0x000000001FF00000ULL); /* entry 1 len */
    put32(b + mm_off + 56, 1);      /* available */

    put32(b + cmd_off + 0, OC_MB2_TAG_CMDLINE);
    put32(b + cmd_off + 4, (u32)cmd_size);
    memcpy(b + cmd_off + 8, cmd, strlen(cmd) + 1);

    put32(b + ldr_off + 0, OC_MB2_TAG_BOOT_LOADER_NAME);
    put32(b + ldr_off + 4, (u32)ldr_size);
    memcpy(b + ldr_off + 8, ldr, strlen(ldr) + 1);

    put32(b + mi_off + 0, OC_MB2_TAG_BASIC_MEMINFO);
    put32(b + mi_off + 4, (u32)meminfo_size);
    put32(b + mi_off + 8, 640);         /* mem_lower */
    put32(b + mi_off + 12, 523264);     /* mem_upper */

    put32(b + mod_off + 0, OC_MB2_TAG_MODULE);
    put32(b + mod_off + 4, (u32)mod_size);
    put32(b + mod_off + 8, 0x00300000); /* mod_start */
    put32(b + mod_off + 12, 0x003A0000);/* mod_end */
    memcpy(b + mod_off + 16, mcmd, 5);

    put32(b + end_off + 0, OC_MB2_TAG_END);
    put32(b + end_off + 4, 8);

    mbi_t m = { b, total };
    return m;
}

/* case: tag size field runs past the end of the mbi */
static mbi_t build_oversized_tag_mbi(void) {
    u8 *b = calloc(1, 64);
    put32(b + 0, 64);            /* total_size */
    put32(b + 4, 0);
    put32(b + 8, OC_MB2_TAG_FRAMEBUFFER);
    put32(b + 12, 0x7FFFFFFF);   /* declared size way past total_size */
    memset(b + 16, 0, 48);       /* payload present but the tag is bogus */
    put32(b + 56, OC_MB2_TAG_END);
    put32(b + 60, 8);
    mbi_t m = { b, 64 };
    return m;
}

/* case: fb tag present but truncated below its structural minimum */
static mbi_t build_truncated_fb_mbi(void) {
    u8 *b = calloc(1, 64);
    u64 total = 64;
    put32(b + 0, (u32)total);
    put32(b + 4, 0);
    put32(b + 8, OC_MB2_TAG_FRAMEBUFFER);
    put32(b + 12, 12);           /* < sizeof(fb_tag) */
    put64(b + 16, 0xDEADBEEFULL);/* junk that must NOT be dereferenced */
    put32(b + 24, OC_MB2_TAG_END);
    put32(b + 28, 8);
    mbi_t m = { b, total };
    return m;
}

/* case: meminfo tag truncated to 12 bytes (p32[2]/p32[3] must not be read) */
static mbi_t build_truncated_meminfo_mbi(void) {
    u8 *b = calloc(1, 48);
    put32(b + 0, 48);
    put32(b + 4, 0);
    put32(b + 8, OC_MB2_TAG_BASIC_MEMINFO);
    put32(b + 12, 12);           /* < 16: mem_upper would be OOB */
    put32(b + 16, 640);
    put32(b + 20, OC_MB2_TAG_END);
    put32(b + 24, 8);
    mbi_t m = { b, 48 };
    return m;
}

/* case: module tag with cmdline 4 bytes but no NUL within its bounds */
static mbi_t build_truncated_module_mbi(void) {
    u8 *b = calloc(1, 48);
    put32(b + 0, 48);
    put32(b + 4, 0);
    put32(b + 8, OC_MB2_TAG_MODULE);
    put32(b + 12, 19);           /* 16 header + 3 bytes: < 16+5 */
    put32(b + 16, 0x00300000);
    put32(b + 20, 0x003A0000);
    b[24] = 's'; b[25] = 'e'; b[26] = 'l';  /* no room for the 5th byte */
    put32(b + 32, OC_MB2_TAG_END);
    put32(b + 36, 8);
    mbi_t m = { b, 48 };
    return m;
}

/* case: absurd total_size */
static mbi_t build_huge_total_mbi(void) {
    u8 *b = calloc(1, 16);
    put32(b + 0, 0x00200000);    /* 2 MiB: over the 1 MiB sanity bound */
    put32(b + 4, 0);
    put32(b + 8, OC_MB2_TAG_END);
    put32(b + 12, 8);
    mbi_t m = { b, 16 };
    return m;
}

/* case: valid fb tag + cmdline string WITHOUT a NUL inside the tag
 * (consumers strcmp the string -> would run past the tag) */
static mbi_t build_unterminated_cmdline_mbi(void) {
    u8 *b = calloc(1, 96);
    u64 fb_size = sizeof(arch_multiboot2_fb_tag_t);
    u64 cmd_size = 12;           /* 8 header + 4 bytes, none of them NUL */
    u64 off = 8;
    u64 fb_off = off; off += (fb_size + 7) & ~7ULL;
    u64 cmd_off = off; off += (cmd_size + 7) & ~7ULL;
    u64 end_off = off; off += 8;
    put32(b + 0, (u32)off);
    put32(b + 4, 0);
    put32(b + fb_off + 0, OC_MB2_TAG_FRAMEBUFFER);
    put32(b + fb_off + 4, (u32)fb_size);
    put64(b + fb_off + 8, 0xFD000000ULL);
    put32(b + fb_off + 16, 4096);
    put32(b + fb_off + 20, 1024);
    put32(b + fb_off + 24, 768);
    put32(b + cmd_off + 0, OC_MB2_TAG_CMDLINE);
    put32(b + cmd_off + 4, (u32)cmd_size);
    memcpy(b + cmd_off + 8, "JUNK", 4);   /* no NUL within the tag */
    put32(b + end_off + 0, OC_MB2_TAG_END);
    put32(b + end_off + 4, 8);
    mbi_t m = { b, off };
    return m;
}

int main(void) {
    printf("host_mb2_test: BUG-0142 (A1-9) arch_multiboot2_parse hardening\n");

    /* ---- legitimate input must fully pass ---- */
    mbi_t good = build_good_mbi();
    arch_multiboot2_info_t info;
    int rc = arch_multiboot2_parse(&info, (uintptr_t)good.buf);
    CHECK(rc == 0, "good mbi parses (rc==0)");
    CHECK(info.fb != NULL && info.fb->framebuffer_width == 1024,
          "good mbi: fb tag fields intact");
    CHECK(info.mmap != NULL && info.mmap->entry_size == 24,
          "good mbi: mmap tag fields intact");
    CHECK(info.mem_lower_kb == 640 && info.mem_upper_kb == 523264,
          "good mbi: meminfo fields intact");
    CHECK(info.cmdline && strcmp(info.cmdline, "oc root=/dev/sda1") == 0,
          "good mbi: cmdline intact");
    CHECK(info.loader_name && strcmp(info.loader_name, "GRUB 2.12") == 0,
          "good mbi: loader name intact");
    {
        const u8 *kd; u64 ks;
        CHECK(arch_multiboot2_get_kernel_self(&kd, &ks) == 0 && ks == 0xA0000,
              "good mbi: 'self' module found with the right size");
    }
    free(good.buf);

    /* ---- malicious inputs must be rejected without OOB reads ---- */
    mbi_t t;

    t = build_oversized_tag_mbi();
    rc = arch_multiboot2_parse(&info, (uintptr_t)t.buf);
    CHECK(rc == -1, "oversized tag: parse rejects (rc==-1)");
    free(t.buf);

    t = build_truncated_fb_mbi();
    rc = arch_multiboot2_parse(&info, (uintptr_t)t.buf);
    CHECK(rc == -1 && info.fb == NULL,
          "truncated fb tag: not accepted as framebuffer info");
    free(t.buf);

    t = build_truncated_meminfo_mbi();
    rc = arch_multiboot2_parse(&info, (uintptr_t)t.buf);
    CHECK(info.mem_upper_kb == 0,
          "truncated meminfo: mem_upper NOT read out of bounds");
    free(t.buf);

    t = build_truncated_module_mbi();
    rc = arch_multiboot2_parse(&info, (uintptr_t)t.buf);
    CHECK(arch_multiboot2_get_kernel_self(NULL, NULL) == -1,
          "truncated module tag: cmdline bytes NOT read out of bounds");
    (void)rc;
    free(t.buf);

    t = build_huge_total_mbi();
    rc = arch_multiboot2_parse(&info, (uintptr_t)t.buf);
    CHECK(rc == -1, "absurd total_size: rejected up front");
    free(t.buf);

    /* unterminated cmdline string must not be published (strcmp would
     * read past the tag); the rest of the mbi must still parse */
    t = build_unterminated_cmdline_mbi();
    rc = arch_multiboot2_parse(&info, (uintptr_t)t.buf);
    CHECK(rc == 0 && info.fb != NULL && info.cmdline == NULL,
          "unterminated cmdline: rejected, fb still parsed");
    free(t.buf);

    if (failures == 0) { printf("  PASS (all cases)\n"); return 0; }
    printf("  FAIL (%d cases)\n", failures);
    return 1;
}
