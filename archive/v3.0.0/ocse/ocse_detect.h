/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP06a-ocse
 * File: ocse/ocse_detect.h
 * Purpose: the four OCSE hardware checks (GUI panel + `hwcheck`).
 */
#ifndef OCSE_DETECT_H
#define OCSE_DETECT_H

#define OCSE_CHECKS 4

typedef struct {
    int  pass;
    char detail[72];       /* ASCII facts (serial-log friendly) */
} ocse_check_t;

extern ocse_check_t g_ocse_checks[OCSE_CHECKS];

/* Run all four checks, filling g_ocse_checks[]. */
void ocse_detect_run(void);

#endif /* OCSE_DETECT_H */
