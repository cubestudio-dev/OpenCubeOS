/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP06g2 - the L0-side i18n runtime interface.
 *
 * Kernel-service USER-FACING strings render through l0_tr_() keyed by
 * the resources in etc/i18n/l0/{en,zh}.json (generated into
 * l0_i18n_data.c by tools/gen_l0_i18n.py - nothing is hand-written in
 * C).  The locale selector is the display service's disp_lang_get()
 * (the `lang` command); a zh miss falls back to en, an en miss falls
 * back to the key itself, so a missing entry is visible as the key
 * string and never a crash.
 *
 * This is the L0 twin of the per-app _shared/i18n.h (L1 apps embed
 * their own tables through gen_userprogs.py); L0 keeps ONE shared
 * table because all services link into the same kernel image. */
#ifndef OC_L0_I18N_H
#define OC_L0_I18N_H

#include "types.h"

struct l0_i18n_kv {
    const char *k;
    const char *v;
};

/* Generated tables (tools/gen_l0_i18n.py, key-sorted). */
extern const struct l0_i18n_kv L0_I18N_EN[];
extern const int               L0_I18N_EN_N;
extern const struct l0_i18n_kv L0_I18N_ZH[];
extern const int               L0_I18N_ZH_N;

/* Translate one key under the ACTIVE display locale.
 * zh table first when the locale is zh (miss -> en), en otherwise,
 * the key itself as the last resort. */
const char *l0_tr_(const char *key);

/* The active locale name ("en" / "zh") for status lines. */
const char *l0_i18n_lang_name(void);

#endif /* OC_L0_I18N_H */
