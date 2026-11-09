/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */

/* dlsym_test.c - WP-08b Batch 5 test program (dlopen/dlsym via API table).
 *
 * A PIE executable (ET_DYN) with:
 *   - PT_INTERP = /lib/ld.so
 *   - No DT_NEEDED   (does NOT link against libfoo.so directly)
 *
 * Instead, this program uses the ld.so API table located at fixed
 * address 0x08000000, which contains 3 function pointers:
 *   dlopen, dlsym, dlclose.
 *
 * _start calls:
 *   LDSO_API->dlopen("libfoo.so", 0)         -> handle
 *   LDSO_API->dlsym(handle, "foo_add")       -> foo_add_ptr
 *   foo_add_ptr(7, 8)                         -> 15
 *   LDSO_API->dlclose(handle)
 *
 * Then prints "dlsym_test: foo_add(7,8)=15\n" via SYS_WRITE,
 * and exits via SYS_EXIT2.
 *
 * Build:
 *   gcc -nostdlib -pie -fPIE -Wl,--dynamic-linker=/lib/ld.so \
 *       -o dlsym_test.elf dlsym_test.c
 *
 * NOTE: There is no -lfoo on the link line -- this binary has no
 * DT_NEEDED for libfoo.so. The library is loaded at runtime by
 * calling dlopen through the API table.
 */

typedef void *(*dlopen_fn)(const char *, int);
typedef void *(*dlsym_fn)(void *, const char *);
typedef int   (*dlclose_fn)(void *);

struct ldso_api {
    dlopen_fn  dlopen;
    dlsym_fn   dlsym;
    dlclose_fn dlclose;
};

#define LDSO_API ((volatile struct ldso_api *)0x08000000)

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
    /* Resolve foo_add at runtime via the ld.so API table at 0x08000000. */
    void *handle = LDSO_API->dlopen("libfoo.so", 0);
    int (*fp)(int, int) = (int (*)(int, int))LDSO_API->dlsym(handle, "foo_add");
    int result = fp(7, 8);
    LDSO_API->dlclose(handle);

    /* Build "dlsym_test: foo_add(7,8)=<result>\n" in a stack buffer. */
    char buf[64];
    int len = 0;
    const char *prefix = "dlsym_test: foo_add(7,8)=";
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
