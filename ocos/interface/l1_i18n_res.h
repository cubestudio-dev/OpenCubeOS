/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP-runtime-hardening-and-experience-rebase
 * File: ocos/interface/l1_i18n_res.h
 * Purpose: the MSG_ID message-resource contract - the single shared
 *          enumeration for the system message tables
 *          (ocos/kernel/core_i18n_res.c: lang_en[] / lang_zh[], served
 *          by get_text(MSG_ID)).
 *
 *          Shared between L0 (the kernel table implementation) and L1
 *          (the L1SVC get_text op) - both worlds include THIS header
 *          so the numbering can never drift.
 *
 * Extension protocol (append-only, spec 4.4):
 *   - new messages APPEND before MSG_ID_COUNT; never renumber
 *   - the id name mirrors its English text's role, prefixed by the
 *     consuming surface (MSG_KERN_ / MSG_USER_ / MSG_BOOT_ /
 *     MSG_UPD_ / MSG_FS_ / MSG_NET_ / MSG_BLK_ / MSG_TUI_ / ...)
 *   - every id MUST have both lang_en and lang_zh entries (the zh
 *     column is a build gate - a missing zh entry fails the build)
 *   - MACHINE-CONTRACT lines (wizard anchors, E2E greps, protocol
 *     markers) NEVER become message ids - they stay English literals
 *     forever; the resource table carries only human-facing text
 */
#ifndef OC_I18N_RES_H
#define OC_I18N_RES_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    /* ---- kernel boot (core_boot_main / l1launch) ---- */
    MSG_BOOT_MB2_MAGIC_BAD = 0,     /* bad multiboot2 magic          */
    MSG_BOOT_MB2_PARSE,             /* multiboot2 parse failed       */
    MSG_BOOT_FB_INIT,               /* fb_init failed                */
    MSG_BOOT_L1_DEFAULT_FAILED,     /* default L1 app failed         */
    MSG_BOOT_L1_RECOVERY,           /* entering recovery shell       */

    /* ---- kernel core (sched/panic) ---- */
    MSG_KERN_PANIC_IDLE_STACK,      /* idle task stack allocation    */
    MSG_KERN_HALTED,                /* system halted                 */
    MSG_KERN_PANIC_NO_HANDLER,      /* panic: no crash handler       */
    MSG_KERN_PANIC_NESTED_PF,       /* nested #PF in handler         */
    MSG_KERN_PROC_KILLED,           /* process killed by exception   */
    MSG_KERN_EXC_FAULT_ADDR,        /* (faulting address)            */

    /* ---- usermode ELF / process errors (core_usermode.c) ---- */
    MSG_USER_NO_ADDR_SPACE,
    MSG_USER_NOT_ELF,
    MSG_USER_PROC_TABLE_FULL,
    MSG_USER_BAD_PHENTSIZE,
    MSG_USER_PTL_VADDR_OVERFLOW,
    MSG_USER_PTL_IN_KERNEL_SPACE,
    MSG_USER_ELF_HDR_CHECKS,
    MSG_USER_MAP_PT_LOAD_FAILED,

    /* ---- update / OTA (selfhost; human lines only - the wizard
     *      anchors stay English literals by contract) ---- */
    MSG_UPD_LIVE_HINT,      /* running live/ISO - updates unsupported */
    MSG_UPD_NO_AB_HINT,     /* no A/B disk - install first            */
    MSG_UPD_LIVE_WIZARD,    /* wizard: live env explanation           */
    MSG_UPD_CHECK_ONLY_HINT,/* wizard: check-only suggestion          */

    /* ---- block / disk ---- */
    MSG_BLK_NO_DISK,                /* no disk found                 */
    MSG_BLK_READ_FAILED,            /* read failed                   */
    MSG_BLK_WRITE_FAILED,           /* write failed                  */

    /* ---- filesystem ---- */
    MSG_FS_NOT_FOUND,               /* file not found                */
    MSG_FS_IS_DIR,                  /* is a directory                */
    MSG_FS_NO_SPACE,                /* no space left                 */
    MSG_FS_READ_ONLY,               /* read-only filesystem          */

    /* ---- network ---- */
    MSG_NET_IF_DOWN,                /* interface is down             */
    MSG_NET_NO_ROUTE,               /* no route                      */
    MSG_NET_DNS_HINT,               /* DNS resolution problem hint   */

    /* ---- shell / common L1 UX (ush + the shared table for apps
     *      without their own i18n json) ---- */
    MSG_SH_CMD_NOT_FOUND,           /* command not found             */
    MSG_SH_BAD_ARGS,                /* bad arguments                 */
    MSG_SH_PERMISSION,              /* permission denied             */
    MSG_SH_DONE,                    /* done                          */
    MSG_SH_CANCELLED,               /* cancelled                     */

    /* ---- config / serial ---- */
    MSG_CFG_BAD_KEY,                /* unknown config key            */
    MSG_CFG_SAVED,                  /* configuration saved           */
    MSG_SER_ENC_SET,                /* serial encoding set to ...    */
    MSG_SER_ENC_NOW,                /* serial encoding is ...        */
    MSG_SER_ENC_PROBE_HEAD,         /* serial-probe banner           */
    MSG_SER_ENC_PROBE_UTF8,         /* probe: UTF-8 sample head      */
    MSG_SER_ENC_PROBE_GBK,          /* probe: GBK sample head        */
    MSG_SER_ENC_PROBE_ASK,          /* probe: which was readable?    */
    MSG_SER_ENC_PROBE_SAVED,        /* probe: config written         */

    /* ---- TUI hidden-entry interception (the CONFIG_TUI=hidden
     *      direct-call block message) ---- */
    MSG_TUI_ENTRY_BLOCKED,          /* entry hidden by build config  */

    /* ---- misc / diagnostics ---- */
    MSG_DIAG_NOT_SUPPORTED,         /* not supported here            */
    MSG_DIAG_NO_DEVICE,             /* device not found              */

    /* ---- update / OTA labels (appended P2A-L0-I18N; keep EVERY id
     *      append-only - never insert above, the L1SVC get_text op
     *      hands raw numbers across the ring boundary) ---- */
    MSG_UPD_CHANGES_LABEL,          /* checkupdate "Changes: " label */

    /* MSG_ID_COUNT entries total (build-gated: lang_en/lang_zh must
     * carry exactly this many rows - the static asserts in
     * core_i18n_res.c fail the build on any drift). */
    MSG_ID_COUNT
} msg_id_t;

/* L1-side helper (ocos/interface/l1_i18n_api.h wraps this through L1SVC):
 * fills `out` with the active-locale text for `id` (NUL-terminated,
 * truncated to cap-1).  Returns the copied length, -1 on a bad id. */
int i18n_res_get_text(int id, char* out, int cap);

#ifdef __cplusplus
}
#endif

#endif /* OC_I18N_RES_H */
