/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS 3.0.0-WP03 G5
 * File: services/klog/klog_file.h
 * Purpose: the on-disk kernel-log channel API (klog_file.c) - the
 *          /var/log/kernel.log sink that installs itself as the ring's
 *          emit hook after the first filesystem mounts.
 */
#ifndef OC_KLOG_FILE_H
#define OC_KLOG_FILE_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Called from kmain after the first filesystem is up: installs the
 * emit hook and dumps the boot log to /var/log/kernel.log. */
void klog_on_mount(void);

/* File-channel reporting + rotation tuning. */
u64  klog_file_bytes(void);    /* bytes in the active kernel.log       */
int  klog_file_open(void);     /* 1 when the channel is live           */
u64  klog_set_rotate_bytes(u64 bytes);
u32  klog_set_rotate_keep(u32 keep);
u64  klog_get_rotate_bytes(void);
u32  klog_get_rotate_keep(void);

/* explicit flush / rotate (task context only) */
int  klog_flush(void);
int  klog_flush_panic(void);
int  klog_rotate(void);

#ifdef __cplusplus
}
#endif

#endif /* OC_KLOG_FILE_H */
