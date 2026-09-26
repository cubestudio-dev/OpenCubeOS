/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Cube Studio <cubestudio@qq.com> */

/* dyn_hello.c - WP-08b Batch 5 test program (dynamic hello, no .so).
 *
 * A PIE executable (ET_DYN) with:
 *   - PT_INTERP = /lib/ld.so   (so the kernel invokes ld.so)
 *   - No DT_NEEDED             (no shared libraries needed)
 *
 * _start prints "hello from dynamic program\n" via SYS_WRITE,
 * then exits via SYS_EXIT2.
 *
 * This proves that a PIE executable with PT_INTERP can be loaded
 * by the kernel and run through ld.so even without any DT_NEEDED
 * dependencies -- the minimal dynamic linking case.
 *
 * Build:
 *   gcc -nostdlib -pie -fPIE -Wl,--dynamic-linker=/lib/ld.so \
 *       -o dyn_hello.elf dyn_hello.c
 */

#define SYS_WRITE  1
#define SYS_EXIT2 16

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
    /* Simple dynamic hello: write a fixed message and exit.
     * int_to_dec is not actually used here (no number to format),
     * but we keep the helper consistent with the other test programs
     * in case someone wants to extend this. */
    (void)int_to_dec;

    const char *msg = "hello from dynamic program\n";
    sys_write(1, msg, strlen_(msg));
    sys_exit2(0);
}
