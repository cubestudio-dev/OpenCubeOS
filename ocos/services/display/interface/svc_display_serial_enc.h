/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP06g3-ocse_enc_fix
 * File: services/display/serial_enc.h
 * Purpose: COM1 serial tee with configurable output encoding
 * (the `serial_encoding utf8|gbk` runtime setting).
 *
 * 3.0.0-WP06g3 task 4.2 (postmortem disaster 2): the kernel's serial
 * stream is legal UTF-8, but the Chinese serial-terminal ecosystem
 * (serial assistants, Chinese PuTTY/SecureCRT presets) defaults to
 * GBK - the very same bytes rendered as mojibake on the user's real
 * machine (byte-level repro: the UTF-8 of "控制台..." decoded as GBK
 * reproduces the exact user screenshot).  The serial line encoding is
 * now a runtime setting, and THIS module is the single conversion
 * point: every console byte reaches COM1 through serial_enc_hook, so
 * screen text, L1 sys_write output, IME compose lines and editor
 * redraws all follow the one setting.  No second standard. */
#ifndef OC_SERIAL_ENC_H
#define OC_SERIAL_ENC_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Install-time init: reset the UTF-8 accumulator + default utf8. */
void serial_enc_init(void);

/* The console base hook (kmain installs it via core_kcap_set_base_hook):
 * tees every console byte to COM1, converting to the configured output
 * encoding on the fly.  Always returns 0 (tee - the framebuffer draw
 * must proceed). */
int serial_enc_hook(void *ctx, u8 ch);

/* Output encoding control (the `serial_encoding` command).
 * serial_enc_set accepts "utf8" or "gbk"; returns 0 on success. */
int serial_enc_set(const char *name);
const char *serial_enc_get_name(void);

/* Command body for the L1SVC_DISP_SERIAL_ENC op ("serial_encoding
 * [utf8|gbk]").  Lives here so the setting and its reporter cannot
 * drift apart. */
int disp_cmd_serial_encoding(const char *args);

#ifdef __cplusplus
}
#endif

#endif /* OC_SERIAL_ENC_H */
