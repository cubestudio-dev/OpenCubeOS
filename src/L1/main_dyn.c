/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */

/* main_dyn.c - WP-08b Batch 4d test program (end-to-end dynamic linking).
 *
 * A PIE executable (ET_DYN) with:
 *   - PT_INTERP = /lib/ld.so   (so the kernel invokes ld.so)
 *   - DT_NEEDED = libfoo.so    (so ld.so knows to load libfoo.so)
 *
 * _start calls foo_add(2, 3) from libfoo.so. The call goes through
 * the PLT, which reads the GOT entry that ld.so patched in Batch 4c
 * to point to foo_add's real address in libfoo.so (0x300010f9).
 * foo_add executes in libfoo.so, returns 5. _start then formats the
 * result as "main: foo_add(2,3)=5\n" and writes it via SYS_WRITE,
 * then exits via SYS_EXIT2.
 *
 * This proves dynamic linking works end-to-end:
 *   kernel maps libfoo.so →
 *   ld.so parses .dynamic + .dynsym (finds foo_add) →
 *   ld.so patches GOT with foo_add address →
 *   ld.so jumps to main entry →
 *   main calls foo_add via PLT → GOT → libfoo.so →
 *   foo_add returns 5 →
 *   main prints "main: foo_add(2,3)=5"
 */

extern int foo_add(int a, int b);

/* Syscall numbers (per kernel/syscall.h) */
#define SYS_WRITE  1
#define SYS_EXIT2  16

static inline long sys_write(int fd, const void *buf, long len) {
    long ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"((long)SYS_WRITE), "D"((long)fd), "S"(buf), "d"(len)
        : "memory", "rcx", "r11"
    );
    return ret;
}

static inline void sys_exit2(int code) {
    __asm__ volatile (
        "int $0x80"
        :
        : "a"((long)SYS_EXIT2), "D"((long)code)
        : "memory", "rcx", "r11"
    );
    for (;;) { }
}

static int strlen_(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

/* Convert a non-negative int to a decimal string in buf.
 * Returns the length (not counting the NUL terminator). */
static int int_to_dec(int v, char *buf) {
    char tmp[16];
    int i = 0, j;
    if (v == 0) { buf[0] = '0'; buf[1] = '\0'; return 1; }
    while (v > 0) {
        tmp[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    for (j = 0; j < i; j++) buf[j] = tmp[i - 1 - j];
    buf[i] = '\0';
    return i;
}

void _start(void) {
    /* Call foo_add(2, 3) from libfoo.so via PLT → GOT (patched by
     * ld.so in Batch 4c) → foo_add's real address in libfoo.so. */
    int result = foo_add(2, 3);

    /* Build "main: foo_add(2,3)=<result>\n" in a stack buffer. */
    char buf[64];
    int len = 0;
    const char *prefix = "main: foo_add(2,3)=";
    int plen = strlen_(prefix);
    int i;
    for (i = 0; i < plen; i++) buf[len++] = prefix[i];

    char numbuf[16];
    int numlen = int_to_dec(result, numbuf);
    for (i = 0; i < numlen; i++) buf[len++] = numbuf[i];

    buf[len++] = '\n';

    sys_write(1, buf, len);
    sys_exit2(0);
}
