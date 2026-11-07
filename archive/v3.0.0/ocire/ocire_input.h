/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP-01-ocire-core
 * File: ocire/ocire_input.h
 * Purpose: OCIRE input - a single getchar() that polls BOTH the serial
 *          RX queue (headless QEMU / service console) and the PS/2
 *          keyboard (real machine with a monitor attached).
 */

#ifndef OCIRE_INPUT_H
#define OCIRE_INPUT_H

#include "ocire_types.h"

void ocire_input_init(void);
/* Returns 1 and stores the char when input is available, else 0. */
int  ocire_getchar(char *out);

#endif /* OCIRE_INPUT_H */
