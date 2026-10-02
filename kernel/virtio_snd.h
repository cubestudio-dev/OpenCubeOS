/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10c
 * File: kernel/virtio_snd.h
 * Purpose: virtio-sound (virtio-snd-pci) driver interface (virtio_snd.c).
 *
 * L1 extension surface:
 *
 *   int virtio_snd_init(pci_dev_t *pdev);
 *       Initialise the first virtio-sound PCI device (1AF4:1059 modern
 *       transitional).  On success a SND_TYPE_VIRTIO device is
 *       registered with the snd framework; -1 when no device is
 *       present or init fails.  Note: as of QEMU 10.x there is no
 *       virtio-snd-pci device model, so under QEMU this probe reports
 *       "not present" (the WP-10c test reports SKIPPED for that
 *       environment).
 *
 *   void virtio_snd_print_state(void);
 *       One-line status for the `virtiosnd` shell command.
 */
#ifndef OC_VIRTIO_SND_H
#define OC_VIRTIO_SND_H

#include "types.h"
#include "pci.h"

int  virtio_snd_init(pci_dev_t *pdev);
void virtio_snd_print_state(void);

#endif /* OC_VIRTIO_SND_H */
