/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP03 - services/crypto op surface.  The bodies
 * were rescued verbatim from core/main.c into crypto_cmds.c (renamed
 * shell_cmd_* -> crypto_cmd_*) when the oc> shell died; this header
 * exports them to the L1 dispatch table (crypto_l1.c). */
#ifndef OC_CRYPTO_L1_CMDS_H
#define OC_CRYPTO_L1_CMDS_H

#ifdef __cplusplus
extern "C" {
#endif

/* crypto_cmds.c (WP-03 rescue, renamed crypto_cmd_*) */
int crypto_cmd_cryptotest(const char *args);
int crypto_cmd_dhtest(const char *args);

#ifdef __cplusplus
}
#endif

#endif /* OC_CRYPTO_L1_CMDS_H */
