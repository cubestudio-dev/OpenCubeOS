/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 2.0.0 - L1 service call surface numbering.
 *
 * SYS_L1SVC(110) routes a ring-3 call to the owning L0 service:
 *     syscall(SYS_L1SVC, cat, op, arg, out, cap)
 * `cat` selects the services/<category>/ folder (the category string
 * and the folder name are one-to-one by rule); `op` is the operation
 * inside that service; `arg` is a NUL-terminated user string (or 0);
 * `out/cap` receive a NUL-terminated text result.  Every op returns
 * plain ASCII text: the ring-3 side formats nothing, the kernel
 * service owns the data and its rendering.
 *
 * The kernel side includes this header; the ring-3 side (l1apps/ush.c)
 * carries a verbatim copy of the constant block below with a comment
 * pointing back here.  Renumber only by appending.
 */
#ifndef OC_L1SVC_DEFS_H
#define OC_L1SVC_DEFS_H

/* ---- Service categories (== services/ folder names) ---------------- */
#define L1SVC_CAT_TIME      1   /* services/time    */
#define L1SVC_CAT_BLOCK     2   /* services/block   */
#define L1SVC_CAT_NIC       3   /* services/nic     */
#define L1SVC_CAT_SND       4   /* services/snd     */
#define L1SVC_CAT_POWER     5   /* services/power   */
#define L1SVC_CAT_OTA       6   /* services/ota     */
#define L1SVC_CAT_FS        7   /* services/fs      */
#define L1SVC_CAT_NET       8   /* services/net     */
#define L1SVC_CAT_HEALTH    9   /* services/health  */
#define L1SVC_CAT_KLOG     10   /* services/klog    */
#define L1SVC_CAT_WATCHDOG 11   /* services/watchdog*/
#define L1SVC_CAT_CRYPTO   12   /* services/crypto  */
#define L1SVC_CAT_L1       13   /* services/l1      */

/* Category number -> folder name (kernel side; used by SYS_L1SVC). */
#define L1SVC_CAT_NAME(n) \
    ((n) == L1SVC_CAT_TIME     ? "time"     : \
     (n) == L1SVC_CAT_BLOCK    ? "block"    : \
     (n) == L1SVC_CAT_NIC      ? "nic"      : \
     (n) == L1SVC_CAT_SND      ? "snd"      : \
     (n) == L1SVC_CAT_POWER    ? "power"    : \
     (n) == L1SVC_CAT_OTA      ? "ota"      : \
     (n) == L1SVC_CAT_FS       ? "fs"       : \
     (n) == L1SVC_CAT_NET      ? "net"      : \
     (n) == L1SVC_CAT_HEALTH   ? "health"   : \
     (n) == L1SVC_CAT_KLOG     ? "klog"     : \
     (n) == L1SVC_CAT_WATCHDOG ? "watchdog" : \
     (n) == L1SVC_CAT_CRYPTO   ? "crypto"   : \
     (n) == L1SVC_CAT_L1       ? "l1"       : 0)

/* ---- time operations (services/time) -------------------------------
 * Mirrors the v1 oc> commands rtc / hpet / pmtimer / tsc. */
#define L1SVC_TIME_RTC      1   /* rtc [uie N]        : CMOS RTC status  */
#define L1SVC_TIME_HPET     2   /* hpet               : HPET status      */
#define L1SVC_TIME_PMTIMER  3   /* pmtimer            : PM timer status  */
#define L1SVC_TIME_TSC      4   /* tsc                : TSC status       */

#endif /* OC_L1SVC_DEFS_H */
