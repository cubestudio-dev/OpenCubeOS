/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP04 - services/diag: the exception self-test
 * block, moved VERBATIM out of boot/kmain.c (where 3.0.0-WP03 had
 * rescued it from v2.2.0 core/main.c).  Body, handlers and counter are
 * byte-for-byte the v2.2.0 originals; only static -> exported changed
 * (kmain's boot step 11 and the L1 `exc` op share the counter, see
 * diag_kern.h). */
#include "diag_kern.h"
#include "arch_exceptions.h"
#include "arch_idt.h"      /* OC_EXC_* vector numbers */
#include "arch_irq.h"      /* arch_irq_frame_t */

#include <stdint.h>

u64 g_exc_test_pass_count = 0;

static int arch_exc_test_de_handler(arch_irq_frame_t *f) {
    extern u8 arch_exc_test_de_resume;
    f->rip = (u64)(uintptr_t)&arch_exc_test_de_resume;
    f->rax = 0; f->rdx = 0;
    g_exc_test_pass_count++;
    return 1;
}
static int arch_exc_test_ud_handler(arch_irq_frame_t *f) {
    extern u8 arch_exc_test_ud_resume;
    f->rip = (u64)(uintptr_t)&arch_exc_test_ud_resume;
    g_exc_test_pass_count++;
    return 1;
}
static int arch_exc_test_pf_handler(arch_irq_frame_t *f) {
    extern u8 arch_exc_test_pf_resume;
    f->rip = (u64)(uintptr_t)&arch_exc_test_pf_resume;
    g_exc_test_pass_count++;
    return 1;
}

void arch_exc_tests(void) {
    arch_exc_register_handler(OC_EXC_DE, arch_exc_test_de_handler);
    arch_exc_register_handler(OC_EXC_UD, arch_exc_test_ud_handler);
    arch_exc_register_handler(OC_EXC_PF, arch_exc_test_pf_handler);

    __asm__ volatile(
        "xor %%rcx, %%rcx\n"
        "div %%rcx\n"
        "arch_exc_test_de_resume:\n"
        : : : "rax","rcx","rdx"
    );
    __asm__ volatile(
        ".byte 0x0F, 0x0B\n"
        "arch_exc_test_ud_resume:\n"
        : :
    );
    __asm__ volatile(
        "movq $0xFFFFFFFFDEADBEEF, %%rax\n"
        "movq (%%rax), %%rbx\n"
        "arch_exc_test_pf_resume:\n"
        : : : "rax","rbx"
    );

    arch_exc_unregister_handler(OC_EXC_DE, arch_exc_test_de_handler);
    arch_exc_unregister_handler(OC_EXC_UD, arch_exc_test_ud_handler);
    arch_exc_unregister_handler(OC_EXC_PF, arch_exc_test_pf_handler);
}
