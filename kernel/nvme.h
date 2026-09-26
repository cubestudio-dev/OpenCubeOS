/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Cube Studio <cubestudio@qq.com> */
/* Open Cube OS - WP-07
 * File: kernel/nvme.h
 * Purpose: NVMe PCI driver - public init entry point.
 *
 * Probes for an NVMe controller (PCI class 0x010802).  If found, brings
 * the controller online, creates admin + I/O queue pairs, performs
 * Identify Namespace to read capacity/sector size, and registers a
 * single block device ("nvme0") with the blk layer.
 */
#ifndef OC_NVME_H
#define OC_NVME_H

void nvme_init(void);

#endif /* OC_NVME_H */
