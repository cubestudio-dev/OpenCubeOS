/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP05-ocbl
 * File: ocbl/common/ocbl_wdt.h
 * Purpose: Intel 6300ESB hardware watchdog (the same device the
 *          OCIRE recovery kernel drives; register protocol per the
 *          6300ESB datasheet and the canonical Linux i6300esb.c).
 *
 * WP-05 contract: OCBL arms the watchdog BEFORE jumping into a
 * payload and keeps kicking it while the menu waits.  A payload that
 * dies before installing its own heartbeat loses the machine to the
 * watchdog reset - which re-enters OCBL (the recovery ladder).
 */
#ifndef OCBL_WDT_H
#define OCBL_WDT_H

#include "ocbl_types.h"

/* PCI sweep for 8086:25AB (6300ESB).  Returns 0 when found. */
int ocbl_wdt_probe(void);

/* Arm both stages with `seconds` per stage (1..2046). */
int ocbl_wdt_arm(int seconds);

/* Reload both stages (the menu loop calls this as its heartbeat). */
int ocbl_wdt_kick(void);

/* Reload + clear the enable bit (before a chainload handoff that the
 * target OS is expected to take over). */
int ocbl_wdt_disarm(void);

int ocbl_wdt_present(void);

#endif /* OCBL_WDT_H */
