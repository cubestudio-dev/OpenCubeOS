/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */

/* reloc_test.c - WP-08b Batch 5 test program (multiple relocation types).
 *
 * A PIE executable (ET_DYN) with:
 *   - PT_INTERP = /lib/ld.so
 *   - DT_NEEDED = libfoo.so
 *
 * Exercises four different relocation types in one binary:
 *
 *   extern int foo_add(int, int);            -- JUMP_SLOT (.rela.plt)
 *       The call `foo_add(10,20)` goes through the PLT; ld.so patches
 *       the GOT slot (R_X86_64_JUMP_SLOT) at startup.
 *
 *   extern int foo_global;                    -- COPY (.rela.dyn)
 *       ld.so copies foo_global (value 42) from libfoo.so into a
 *       local .bss slot in this executable (R_X86_64_COPY). The
 *       direct `mov foo_global(%rip), %eax` we emit via inline asm
 *       forces the linker to emit the COPY relocation rather than
 *       the GOTPCREL form (which would be GLOB_DAT).
 *
 *   static char *msg = "reloc_test";          -- RELATIVE (.rela.dyn)
 *       `msg` is a file-scope pointer to a string literal. Both live
 *       in this binary, so the relocation is R_X86_64_RELATIVE --
 *       the runtime loader adds the load bias to the link-time value.
 *
 *   static int (*fp)(int, int) = foo_add;     -- GLOB_DAT (.rela.dyn)
 *       `fp` is a file-scope pointer to an extern function from
 *       libfoo.so. ld.so patches the slot with foo_add's runtime
 *       address (R_X86_64_GLOB_DAT) at startup.
 *
 * _start does:
 *     r1 = foo_add(10, 20);       ->  30
 *     r2 = foo_global;            ->  42  (via COPY-relocated slot)
 *     r3 = fp(100, 200);          -> 300  (calls the GLOB_DAT-patched fp)
 *     r4 = msg[0];                -> 'r'  (reads RELATIVE-relocated ptr)
 *
 * and prints "reloc_test: foo_add=30 foo_global=42 fp=300 msg=r\n".
 *
 * Build:
 *   gcc -nostdlib -pie -fPIE -Wl,--no-as-needed \
 *       -Wl,--dynamic-linker=/lib/ld.so -L. -lfoo \
 *       -o reloc_test.elf reloc_test.c
 */

extern int foo_add(int a, int b);
extern int foo_global;

/* RELATIVE: pointer to a string literal in this binary. */
static char *msg = "reloc_test";
/* R_X86_64_64 (a.k.a. "GLOB_DAT-equivalent"): pointer to an extern
 * function from libfoo.so.
 *
 * The user's spec mentions "GLOB_DAT" for this construct, but in
 * practice binutils emits R_X86_64_64 (type 1) for a static data
 * initializer that points to an extern symbol -- the strict
 * R_X86_64_GLOB_DAT (type 6) is only produced for GOT slots that
 * are accessed via @GOTPCREL in code, not for static initializers.
 * The two reloc types are functionally equivalent: both write the
 * symbol's runtime address into a slot at startup. Standard glibc
 * ld.so handles them with the same code path, and so must the
 * WP-08b ld.so (i.e. ld.so must handle BOTH R_X86_64_64 AND
 * R_X86_64_GLOB_DAT, since real binaries contain both kinds).
 *
 * (We tried adding an explicit `mov foo_add@GOTPCREL(%rip), ...`
 * inline-asm fetch to force a strict GLOB_DAT reloc, but that
 * causes the linker to optimize the direct `foo_add(10,20)` call
 * from .plt (JUMP_SLOT) to .plt.got -- so we'd lose JUMP_SLOT.
 * Keeping JUMP_SLOT is more important, so we accept R_X86_64_64
 * for fp's slot instead of strict GLOB_DAT. The ld.so impl in
 * Batch 5 must simply handle R_X86_64_64 with the same logic as
 * GLOB_DAT.) */
static int (*fp)(int, int) = foo_add;

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
    /* JUMP_SLOT: PLT call to extern foo_add. */
    int r1 = foo_add(10, 20);

    /* COPY: direct RIP-relative load of extern foo_global. Using
     * inline asm (rather than the C-level `r2 = foo_global`) forces
     * the compiler to emit a direct `mov foo_global(%rip), %eax`
     * instead of a GOTPCREL access; the linker then creates a
     * R_X86_64_COPY relocation in .rela.dyn. */
    int r2;
    __asm__ volatile ("movl foo_global(%%rip), %0" : "=r"(r2));

    /* R_X86_64_64 ("GLOB_DAT-equivalent"): indirect call through
     * the fp slot patched by ld.so (the static initializer's reloc). */
    int r3 = fp(100, 200);

    /* RELATIVE: load through the msg slot (pointer to local string
     * literal, value fixed up by R_X86_64_RELATIVE at load time). */
    char r4 = msg[0];

    /* Build "reloc_test: foo_add=<r1> foo_global=<r2> fp=<r3> msg=<r4>\n". */
    char buf[128];
    int len = 0;
    int i;

    const char *p;
    int plen;
    char numbuf[16];
    int numlen;

    p = "reloc_test: foo_add=";
    plen = strlen_(p);
    for (i = 0; i < plen; i++) buf[len++] = p[i];
    numlen = int_to_dec(r1, numbuf);
    for (i = 0; i < numlen; i++) buf[len++] = numbuf[i];

    p = " foo_global=";
    plen = strlen_(p);
    for (i = 0; i < plen; i++) buf[len++] = p[i];
    numlen = int_to_dec(r2, numbuf);
    for (i = 0; i < numlen; i++) buf[len++] = numbuf[i];

    p = " fp=";
    plen = strlen_(p);
    for (i = 0; i < plen; i++) buf[len++] = p[i];
    numlen = int_to_dec(r3, numbuf);
    for (i = 0; i < numlen; i++) buf[len++] = numbuf[i];

    p = " msg=";
    plen = strlen_(p);
    for (i = 0; i < plen; i++) buf[len++] = p[i];
    buf[len++] = r4;

    buf[len++] = '\n';

    sys_write(1, buf, len);
    sys_exit2(0);
}
