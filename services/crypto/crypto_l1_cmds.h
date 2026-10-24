/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 2.0.0 - services/crypto command surface (exported
 * bodies, migrated by declaration in group O).  The command bodies
 * live in core/main.c; this header exports them to the L1 dispatch
 * table (services/crypto/crypto_l1.c). */
#ifndef OC_CRYPTO_L1_CMDS_H
#define OC_CRYPTO_L1_CMDS_H

#ifdef __cplusplus
extern "C" {
#endif

/* core/main.c (group O: static -> exported) */
int shell_cmd_cryptotest(const char *args);
int shell_cmd_dhtest(const char *args);

#ifdef __cplusplus
}
#endif

#endif /* OC_CRYPTO_L1_CMDS_H */
