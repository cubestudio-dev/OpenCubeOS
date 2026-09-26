/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Cube Studio <cubestudio@qq.com> */

/* pie_test.c - WP-08b Batch 5 test program (PIE load address detection).
 *
 * A PIE executable (ET_DYN) with:
 *   - PT_INTERP = /lib/ld.so
 *   - No DT_NEEDED
 *
 * _start uses `lea _start(%rip), %0` to read its own runtime address,
 * then masks the result with 0xF0000000 to find the load base.
 * Under the WP-08b kernel the main executable is mapped at 0x20000000,
 * so the masked value is expected to be 0x20000000.
 *
 * Prints "pie_test: loaded at 0x20000000\n" via SYS_WRITE,
 * then exits via SYS_EXIT2.
 *
 * Build:
 *   gcc -nostdlib -pie -fPIE -Wl,--dynamic-linker=/lib/ld.so \
 *       -o pie_test.elf pie_test.c
 *
 * Implementation note: `lea _start(%rip)` is RIP-relative, so the
 * 32-bit displacement from the lea instruction to _start's start is
 * resolved at link time (the binary's internal layout is fixed). The
 * instruction itself is position-independent -- no runtime dynamic
 * relocation is needed for it. The displacement is tiny (just bytes
 * to a couple of KB), so masking with 0xF0000000 cleanly isolates
 * the load base.
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

/* Convert a non-negative 64-bit value to a hex string in buf.
 * Returns the length (no NUL terminator counted). */
static int u64_to_hex(unsigned long v, char *buf) {
    const char *hex = "0123456789abcdef";
    char tmp[32];
    int i = 0, j;
    if (v == 0) { buf[0] = '0'; buf[1] = '\0'; return 1; }
    while (v > 0) {
        tmp[i++] = hex[v & 0xF];
        v >>= 4;
    }
    for (j = 0; j < i; j++) buf[j] = tmp[i - 1 - j];
    buf[i] = '\0';
    return i;
}

void _start(void) {
    /* Read the runtime address of _start via RIP-relative lea.
     * The `%%rip` is needed because % is the inline-asm escape char. */
    unsigned long self_addr;
    __asm__ volatile ("lea _start(%%rip), %0" : "=r"(self_addr));

    /* Mask down to the 256MB-aligned load base. WP-08b maps the
     * main ELF at 0x20000000, so this should yield 0x20000000. */
    unsigned long load_base = self_addr & 0xF0000000UL;

    /* Build "pie_test: loaded at 0x<hex>\n" in a stack buffer. */
    char buf[64];
    int len = 0;
    const char *prefix = "pie_test: loaded at 0x";
    int plen = strlen_(prefix);
    int i;
    for (i = 0; i < plen; i++) buf[len++] = prefix[i];

    char hexbuf[32];
    int hexlen = u64_to_hex(load_base, hexbuf);
    for (i = 0; i < hexlen; i++) buf[len++] = hexbuf[i];

    buf[len++] = '\n';

    sys_write(1, buf, len);
    sys_exit2(0);
}
