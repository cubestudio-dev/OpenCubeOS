/* SPDX-License-Identifier: Apache-2.0 */
/* Open Cube OS - WP-01
 * File: kernel/log.h
 * Purpose: Boot stage logger - writes a line per stage of the form:
 *
 *     [HH:MM:SS.mmm] stage_name ......... OK
 *
 * Time source: a free-running 32-bit counter at 0xB8000 (PIT-pumped
 * dummy for WP-01). For WP-01 we don't have a real timer running, so
 * the "time" is a monotonic 1/100s counter incremented per log call -
 * it's enough to demonstrate the format and ordering. WP-02 will wire
 * a real PIT/HPET.
 */
#ifndef OC_LOG_H
#define OC_LOG_H

#include "types.h"

void oc_log_init(void);

/* Emit a log line for the given stage name. status is "OK" or "FAIL". */
void oc_log_stage(const char* stage, const char* status);

/* Convenience wrappers. */
#define OC_LOG_OK(stage)    oc_log_stage((stage), "OK")
#define OC_LOG_FAIL(stage) oc_log_stage((stage), "FAIL")

/* Plain info line (no [time] prefix, just indentation). */
void oc_log_info(const char* s);

#endif /* OC_LOG_H */
