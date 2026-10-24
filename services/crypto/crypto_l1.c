/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 2.0.0 - services/crypto L1 surface (SYS_L1SVC cat
 * "crypto"): the self-test family (NIST vectors + DH modexp goldens).
 * ssh/sshd are interactive-session commands and stay in L0.  Bodies
 * live in core/main.c (exported as shell_cmd_cryptotest/dhtest by
 * group O), referenced by the op table. */
#include "core_registry.h"
#include "l1svc_defs.h"
#include "crypto_l1_cmds.h"
#include "shell.h"

typedef int (*crypto_cmd_fn_t)(const char *);

static int crypto_l1_dispatch(u32 op, const char *arg, char *out, int cap) {
    if (!out || cap <= 0) return -1;
    crypto_cmd_fn_t fn = 0;
    switch (op) {
        case L1SVC_CRYPTO_CRYPTOTEST: fn = shell_cmd_cryptotest; break;
        case L1SVC_CRYPTO_DHTEST:     fn = shell_cmd_dhtest;     break;
        default: return -1;
    }
    return shell_run_fn_captured(fn, arg, out, cap) == 0 ? 0 : -1;
}

/* v2.0.0 group O: crypto service identity (AEAD/SHA/RSA/DH/X.509 +
 * the self-test surface). */
OC_SERVICE_DECL_L1("crypto", "crypto service (aead/sha/rsa/dh/x509 + ring-3 surface)",
                   (oc_service_init_fn_t)0, (oc_service_late_fn_t)0,
                   crypto_l1_dispatch);
