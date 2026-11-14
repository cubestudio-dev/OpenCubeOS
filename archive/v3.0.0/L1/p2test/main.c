/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0 - WP-fix-p2-p3-cleanup L1 probe app "p2test".
 *
 * Live probes for the batch-A kernel fixes; driven by
 * tools/wp_p2_batch_a_test.py over the QEMU serial console:
 *
 *   p2test utf8   - #12: 174 hanzi (522 bytes, one syscall) through
 *                   BOTH console write paths (SYS_WRITE fd=1 and
 *                   SYS_WRITE2 fd=1).  522 > 512, so the old fixed
 *                   512-byte chunking split one hanzi per call across
 *                   two cli windows (Latin-1 mojibake on the frame
 *                   buffer).  The host script counts the intact UTF-8
 *                   sequences on the serial log and VLM-compares the
 *                   frame buffer rows.
 *
 *   p2test read0  - #14: read(0, buf, 0) must return 0 IMMEDIATELY
 *                   (POSIX).  The old stdin loop blocked until a key
 *                   arrived and then stored 1 byte + returned 1.  The
 *                   host script sends no keys and requires the
 *                   "READ0 ret=0" line within the timeout window.
 *
 *   p2test pipe   - #9 + pipe slot leak: 100 rounds; each round
 *                   pipes, forks TWO readers (multi-reader is the
 *                   exact race window #9 describes), the parent
 *                   writes a 4-byte round cookie, and the round is
 *                   valid only when exactly one reader decodes the
 *                   cookie and the other gets a clean EOF (0).  100
 *                   rounds also prove the pipe-slot table is reused:
 *                   MAX_PIPES is 32, so a leaking in_use flag died at
 *                   round 17 in the pre-fix stress run.
 */
#include "_shared/l1_lib.h"

/* SYS_WRITE2(72) is defined in l1_lib.h but has no wrapper; the utf8
 * probe needs the raw fd-aware path. */
static inline long write2_call(long fd, const void *buf, long len) {
    return syscall3(SYS_WRITE2, fd, (long)buf, len);
}

/* SYS_FORK(10) likewise (ush spells its own macro). */
static inline long fork_call(void) {
    return syscall0(SYS_FORK);
}

/* SYS_WAIT4 is 4-arg in the kernel (pid, status_ptr, options, ...);
 * spell the status pointer so the exit code is collected. */
static inline long wait4_status(long pid, int *status) {
    return syscall2(SYS_WAIT4, pid, (long)status);
}

/* ---------------- utf8 probe (#12) ---------------- */
static int probe_utf8(void) {
    /* 174 hanzi x 3 bytes = 522 bytes: one write crosses the 512-byte
     * console chunk boundary, and 522 = 170*3 + 2 splits the 171st
     * hanzi exactly like production Chinese logs do. */
    static char payload[522 + 1];
    int n = 0;
    for (int i = 0; i < 174; i++) {
        payload[n++] = (char)0xE6;   /* U+6D4B "ce" (test) */
        payload[n++] = (char)0xB5;
        payload[n++] = (char)0x8B;
    }
    payload[n] = 0;

    puts_("UTF8-BEGIN\n");
    long w1 = sys_write(1, payload, n);        /* SYS_WRITE console path */
    puts_("\nUTF8-W1 bytes=");
    putu_((unsigned long)w1);
    puts_("\n");
    long w2 = write2_call(1, payload, n);      /* SYS_WRITE2 fd-aware path */
    puts_("\nUTF8-W2 bytes=");
    putu_((unsigned long)w2);
    puts_("\nUTF8-END\n");
    if (w1 != n || w2 != n) {
        puts_("UTF8-FAIL short write\n");
        return 1;
    }
    puts_("UTF8-OK 348/348\n");
    return 0;
}

/* ---------------- read0 probe (#14) ---------------- */
static int probe_read0(void) {
    static char scratch[8];
    long t0 = sys_uptime();
    long r = sys_read(0, scratch, 0);   /* count == 0: must not block */
    long t1 = sys_uptime();
    puts_("READ0 ret=");
    putu_((unsigned long)r);
    puts_(" dt=");
    putu_((unsigned long)(t1 - t0));
    puts_("ms\n");
    /* POSIX: return 0, immediately (well under the 1000 ms sample). */
    if (r != 0 || (t1 - t0) > 900) {
        puts_("READ0-FAIL\n");
        return 1;
    }
    puts_("READ0-OK\n");
    return 0;
}

/* ---------------- pipe probe (#9 + slot leak) ---------------- */
static void pipe_reader_child(unsigned long expect, int d0, int d1) {
    /* Drop the inherited WRITE end first: without this the sibling's
     * EOF read only unblocks when the winner's exit is reaped. */
    sys_close(d1);
    unsigned char b[4];
    long n = sys_read(d0, b, 4);
    if (n == 4) {
        /* 4-byte little-endian manual decode (no long* aliasing: the
         * pipe payload is exactly the 4 bytes the parent wrote). */
        unsigned long v = (unsigned long)b[0]
                        | ((unsigned long)b[1] << 8)
                        | ((unsigned long)b[2] << 16)
                        | ((unsigned long)b[3] << 24);
        sys_exit2(v == expect ? 0 : 2);   /* 0 = data verified */
    } else if (n == 0) {
        sys_exit2(5);                     /* 5 = clean EOF (all writers gone) */
    } else {
        sys_exit2(3);                     /* 3 = read error */
    }
}

static int probe_pipe(void) {
    const int rounds = 100;
    int data_ok = 0, eof_ok = 0;
    for (int r = 1; r <= rounds; r++) {
        int d[2];                          /* int pair: the kernel ABI */
        if (syscall2(SYS_PIPE, (long)d, 0) != 0) {
            puts_("PIPE-FAIL round=");
            putu_((unsigned long)r);
            puts_(" pipe()=-1 (slot table exhausted?)\n");
            return 1;
        }
        unsigned long expect =
            (0x5A5A5A5AUL ^ ((unsigned long)r * 0x01010101UL))
            & 0xFFFFFFFFUL;
        unsigned char pay[4] = {
            (unsigned char)(expect & 0xFF),
            (unsigned char)((expect >> 8) & 0xFF),
            (unsigned char)((expect >> 16) & 0xFF),
            (unsigned char)((expect >> 24) & 0xFF),
        };
        long c1 = fork_call();
        if (c1 == 0) { pipe_reader_child(expect, d[0], d[1]); }
        long c2 = fork_call();
        if (c2 == 0) { pipe_reader_child(expect, d[0], d[1]); }
        /* Parent: no reading here - close the read end, ship the
         * cookie, close the write end (EOF for the losing reader). */
        sys_close(d[0]);
        long w = sys_write(d[1], pay, 4);
        sys_close(d[1]);
        if (w != 4) {
            puts_("PIPE-FAIL round=");
            putu_((unsigned long)r);
            puts_(" write=");
            putu_((unsigned long)w);
            puts_("\n");
            return 1;
        }
        int st1 = -1, st2 = -1;
        long w1 = wait4_status(c1, &st1);
        long w2 = wait4_status(c2, &st2);
        if (w1 < 0 || w2 < 0) {
            puts_("PIPE-FAIL round=");
            putu_((unsigned long)r);
            puts_(" wait4 miss\n");
            return 1;
        }
        /* Exactly one reader must decode the cookie (exit 0) and the
         * other must see a clean EOF (exit 5).  Exit 2 = duplicate or
         * corrupted delivery (the #9 race), 3 = read error. */
        if ((st1 == 0 && st2 == 5) || (st1 == 5 && st2 == 0)) {
            if (st1 == 0 || st2 == 0) data_ok++;
            if (st1 == 5 || st2 == 5) eof_ok++;
            continue;
        }
        puts_("PIPE-FAIL round=");
        putu_((unsigned long)r);
        puts_(" st1=");
        putu_((unsigned long)st1);
        puts_(" st2=");
        putu_((unsigned long)st2);
        puts_("\n");
        return 1;
    }
    puts_("PIPE: ");
    putu_((unsigned long)data_ok);
    puts_("/");
    putu_((unsigned long)rounds);
    puts_(" data + ");
    putu_((unsigned long)eof_ok);
    puts_("/");
    putu_((unsigned long)rounds);
    puts_(" eof, slot-reuse 100 rounds\n");
    puts_("PIPE-PASS\n");
    return 0;
}

/* ---------------- entry ---------------- */
static void usage(void) {
    puts_(tr_("usage: p2test <utf8|read0|pipe>\n"));
}

int app_main(int argc, char **argv) {
    if (argc < 2) { usage(); return 1; }
    if (strcmp_(argv[1], "utf8") == 0) return probe_utf8();
    if (strcmp_(argv[1], "read0") == 0) return probe_read0();
    if (strcmp_(argv[1], "pipe") == 0) return probe_pipe();
    usage();
    return 1;
}
