/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP-runtime-hardening-and-experience-rebase
 * File: ocos/kernel/core_i18n_res.h
 * Purpose: the L0 side of the MSG_ID message-resource contract
 *          (ocos/interface/l1_i18n_res.h): the lang_en / lang_zh tables
 *          live in core_i18n_res.c and get_text(MSG_ID) serves the
 *          ACTIVE-locale string (zh when the display locale is zh,
 *          en otherwise, "?" only for an out-of-range id).
 *
 *          This is the SYSTEM message layer the spec's 4.4 mandates:
 *          kernel panic/boot failures, usermode ELF errors, the
 *          update flow's human lines, and the shared L1 UX strings
 *          render through HERE - never through inline literals.
 *
 *          Machine-contract lines (wizard anchors, E2E greps) stay
 *          English literals by design and are documented in
 *          tools/i18n_contract_anchor.txt.
 */
#ifndef OC_CORE_I18N_RES_H
#define OC_CORE_I18N_RES_H

#include "types.h"
#include "../interface/l1_i18n_res.h"

/* The tables (core_i18n_res.c): indexed by msg_id_t, exactly
 * MSG_ID_COUNT entries each - a mismatch fails the build via the
 * static assert in the .c file. */
extern const char *const lang_en[MSG_ID_COUNT];
extern const char *const lang_zh[MSG_ID_COUNT];

/* The active-locale string for `id`:
 *   zh locale + non-empty zh entry -> lang_zh[id]
 *   otherwise                      -> lang_en[id]
 *   out-of-range id                -> "?" (never NULL, never a crash)
 * EXCEPTION/EARLY-BOOT NOTE: this calls disp_lang_get() (the display
 * service's locale state) - before the service registry exists it
 * reads its static default (en), so early FATAL paths safely print
 * English. */
const char *get_text(msg_id_t id);

#endif /* OC_CORE_I18N_RES_H */
