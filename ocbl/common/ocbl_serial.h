/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP05-ocbl */
#ifndef OCBL_SERIAL_H
#define OCBL_SERIAL_H

#include "ocbl_types.h"

void ocbl_serial_init(void);
void ocbl_serial_putc(char c);
void ocbl_serial_puts(const char* s);
void ocbl_serial_hex(u64 v, int digits);   /* digits<=0: minimal width  */
void ocbl_serial_dec(u64 v);

#endif /* OCBL_SERIAL_H */
