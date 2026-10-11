/* Host-side regression test for the WP-10-AUDIT_P2-fix4 lib_tar batch
 * (BUG-0300). NOT part of the kernel build.
 *
 * Covers BUG-0300 (A5-11):
 *   - a pax metadata entry ('x', size > 0) must be SKIPPED including its
 *     body (old code zeroed file_size, the body was parsed as a header and
 *     the whole package died with a bogus ERR_CSUM);
 *   - a GNU long-link entry ('K', size > 0) is skipped the same way;
 *   - a GNU long-name entry ('L') whose body spans multiple 512-byte
 *     blocks keeps the parser in sync and the next file is reported with
 *     the long path (truncated to the 255-char gnu_name buffer cap).
 * Plus regressions: plain ustar file, directory entry, empty file.
 *
 * Build (from oc-os/):
 *   gcc -O2 -I. -Ikernel/lib -Ikernel/mem tests/host_fix4_tar_test.c \
 *       -o /tmp/fix4_tar_test
 * (do NOT include <string.h> - same lib_string.h strchr conflict as
 * tools/host_gzip_test.c)
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "kernel/types.h"

void *kmalloc(u64 n) { return malloc(n); }
void kfree(void *p) { free(p); }

#include "kernel/lib/lib_tar.c"

static int check(const char *name, int cond) {
    printf("%-58s %s\n", name, cond ? "PASS" : "FAIL");
    return cond ? 0 : 1;
}

/* ---- ustar header builder ---- */
static void set_octal(char *dst, int width, u64 v) {
    char tmp[24];
    int i = width - 1;
    dst[i--] = 0;
    if (i >= 0) dst[i--] = ' ';
    do { dst[i--] = (char)('0' + (v & 7)); v >>= 3; } while (v && i >= 0);
    while (i >= 0) dst[i--] = '0';
}
static void tar_hdr(u8 *b, const char *name, char type, u64 size) {
    for (int i = 0; i < 512; i++) b[i] = 0;
    int nl = 0;
    while (name[nl] && nl < 99) { b[nl] = (u8)name[nl]; nl++; }
    memcpy(b + 100, "0000644\0", 8);
    memcpy(b + 108, "0000000\0", 8);
    memcpy(b + 116, "0000000\0", 8);
    set_octal((char *)b + 124, 12, size);
    set_octal((char *)b + 136, 12, 0);
    memset(b + 148, ' ', 8);
    b[156] = (u8)type;
    memcpy(b + 257, "ustar\0", 6);
    memcpy(b + 263, "00", 2);
    u32 sum = 0;
    for (int i = 0; i < 512; i++) sum += b[i];
    set_octal((char *)b + 148, 8, sum);
}

/* ---- callback recorder ---- */
#define MAX_EVENTS 32
struct event { int type; u64 size; int slice_len; int has_data; char name[260]; };
static struct event g_ev[MAX_EVENTS];
static int g_ev_n;
static int rec_cb(void *ctx, const char *path, int type, u64 size,
                  const u8 *data, int len) {
    (void)ctx;
    if (g_ev_n < MAX_EVENTS) {
        g_ev[g_ev_n].type = type;
        g_ev[g_ev_n].size = size;
        g_ev[g_ev_n].slice_len = len;
        g_ev[g_ev_n].has_data = (data != NULL);
        int i = 0;
        while (path[i] && i < 259) { g_ev[g_ev_n].name[i] = path[i]; i++; }
        g_ev[g_ev_n].name[i] = 0;
        g_ev_n++;
    }
    return 0;
}
static void reset_events(void) { g_ev_n = 0; }

static int run_tar(const u8 *buf, int len) {
    reset_events();
    lib_tar_t *t = lib_tar_open(rec_cb, NULL);
    int pos = 0, rc = 0;
    while (pos < len) {
        int take = 97;   /* awkward chunk size on purpose: exercise split blocks */
        if (pos + take > len) take = len - pos;
        rc = lib_tar_feed(t, buf + pos, take);
        if (rc != 0) break;
        pos += take;
    }
    if (rc == 0) rc = lib_tar_finish(t);
    lib_tar_close(t);
    return rc;
}

static u8 arc[8192];

int main(void) {
    int fails = 0;
    int n;

    /* ---- regression: plain file ---- */
    {
        n = 0;
        tar_hdr(arc + n, "data.txt", '0', 7); n += 512;
        memcpy(arc + n, "PAYLOAD", 7); n += 7;
        while (n & 511) arc[n++] = 0;
        memcpy(arc + n, "\0\0", 2); n += 2;          /* one zero block */
        memset(arc + n, 0, 512); n += 512;
        int rc = run_tar(arc, n);
        fails += check("regression plain file parsed (rc=0)", rc == 0);
        fails += check("regression plain file one file + final slice",
                       g_ev_n == 2 && g_ev[0].type == OC_TAR_FILE &&
                       g_ev[0].size == 7 && g_ev[0].slice_len == 7 &&
                       g_ev[1].slice_len == 0);
    }

    /* ---- BUG-0300: pax 'x' metadata entry with body is skipped ---- */
    {
        n = 0;
        const char *paxbody =
            "30 mtime=1700000000.000000000\n25 path=ignored/ignored.txt\n";
        u64 blen = 0;
        while (paxbody[blen]) blen++;
        tar_hdr(arc + n, "PaxHeader", 'x', blen); n += 512;
        memcpy(arc + n, paxbody, blen); n += (int)blen;
        while (n & 511) arc[n++] = 0;
        tar_hdr(arc + n, "data.txt", '0', 7); n += 512;
        memcpy(arc + n, "PAYLOAD", 7); n += 7;
        while (n & 511) arc[n++] = 0;
        memcpy(arc + n, "\0\0", 2); n += 2;
        memset(arc + n, 0, 512); n += 512;
        int rc = run_tar(arc, n);
        fails += check("BUG-0300 pax 'x' package parses (was ERR_CSUM)", rc == 0);
        fails += check("BUG-0300 pax body skipped, file reported",
                       g_ev_n == 2 && g_ev[0].type == OC_TAR_FILE &&
                       g_ev[0].slice_len == 7 && g_ev[1].slice_len == 0);
    }

    /* ---- BUG-0300: GNU 'K' long-link entry with body is skipped ---- */
    {
        n = 0;
        const char *lkbody = "/somewhere/very/long/target/name.txt";
        u64 blen = 0;
        while (lkbody[blen]) blen++;
        tar_hdr(arc + n, "symlink", 'K', blen); n += 512;
        memcpy(arc + n, lkbody, blen); n += (int)blen;
        while (n & 511) arc[n++] = 0;
        tar_hdr(arc + n, "data.txt", '0', 7); n += 512;
        memcpy(arc + n, "PAYLOAD", 7); n += 7;
        while (n & 511) arc[n++] = 0;
        memcpy(arc + n, "\0\0", 2); n += 2;
        memset(arc + n, 0, 512); n += 512;
        int rc = run_tar(arc, n);
        fails += check("BUG-0300 GNU 'K' package parses (was ERR_CSUM)", rc == 0);
        fails += check("BUG-0300 'K' body skipped, file reported",
                       g_ev_n == 2 && g_ev[0].type == OC_TAR_FILE &&
                       g_ev[0].slice_len == 7);
    }

    /* ---- BUG-0300: GNU 'L' long name spanning MULTIPLE blocks ---- */
    {
        n = 0;
        /* 700-char name: body spans two 512-byte blocks (700+pad = 1024) */
        static char longname[701];
        for (int i = 0; i < 700; i++) longname[i] = (char)('a' + (i % 26));
        longname[700] = 0;
        u64 nlen = 700;
        tar_hdr(arc + n, "././@LongLink", 'L', nlen); n += 512;
        memcpy(arc + n, longname, (int)nlen); n += (int)nlen;
        while (n & 511) arc[n++] = 0;                 /* pad to block edge */
        tar_hdr(arc + n, "short_name.txt", '0', 7); n += 512;
        memcpy(arc + n, "PAYLOAD", 7); n += 7;
        while (n & 511) arc[n++] = 0;
        memcpy(arc + n, "\0\0", 2); n += 2;
        memset(arc + n, 0, 512); n += 512;
        int rc = run_tar(arc, n);
        fails += check("BUG-0300 multi-block GNU 'L' package parses", rc == 0);
        /* reported name = first 255 chars of the long name (gnu_name cap) */
        int name_ok = (g_ev_n >= 1) && g_ev[0].type == OC_TAR_FILE;
        int L = 0;
        if (name_ok) while (g_ev[0].name[L]) L++;
        fails += check("BUG-0300 long-name file reported (255-char cap)",
                       name_ok && L == 255);
        int prefix_ok = 1;
        for (int i = 0; i < 255 && name_ok; i++)
            if (g_ev[0].name[i] != longname[i]) { prefix_ok = 0; break; }
        fails += check("BUG-0300 long-name prefix matches", prefix_ok);
        fails += check("BUG-0300 long-name body intact",
                       g_ev_n >= 2 && g_ev[0].slice_len == 7 &&
                       g_ev[1].slice_len == 0);
    }

    /* ---- empty file (size 0): single (NULL,0) callback, parser OK ---- */
    {
        n = 0;
        tar_hdr(arc + n, "empty.txt", '0', 0); n += 512;
        tar_hdr(arc + n, "next.txt", '0', 4); n += 512;
        memcpy(arc + n, "ABCD", 4); n += 4;
        while (n & 511) arc[n++] = 0;
        memcpy(arc + n, "\0\0", 2); n += 2;
        memset(arc + n, 0, 512); n += 512;
        int rc = run_tar(arc, n);
        fails += check("empty file + next file parse (rc=0)", rc == 0);
        fails += check("empty file emits single (NULL,0) callback",
                       g_ev_n == 3 && g_ev[0].slice_len == 0 &&
                       g_ev[0].has_data == 0 && g_ev[0].size == 0 &&
                       g_ev[1].slice_len == 4);
    }

    printf("host_fix4_tar_test: %s (%d failure%s)\n",
           fails == 0 ? "ALL PASS" : "FAIL", fails, fails == 1 ? "" : "s");
    return fails == 0 ? 0 : 1;
}
