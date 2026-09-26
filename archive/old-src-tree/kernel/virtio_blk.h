/* SPDX-License-Identifier: Apache-2.0 */
/* Open Cube OS - WP-07
 * File: kernel/virtio_blk.h
 * Purpose: virtio-blk PCI driver - public init entry point.
 *
 * Registers a single virtio-blk device ("vda") with the blk layer if a
 * supported PCI device is found.
 */
#ifndef OC_VIRTIO_BLK_H
#define OC_VIRTIO_BLK_H

void virtio_blk_init(void);

#endif /* OC_VIRTIO_BLK_H */
