/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-03
 * File: l1/l1_wp3.h
 * Purpose: WP-03 extension API summary. Re-exports the WP-03 extension
 *          functions so L1 has one place to look.
 *
 * WP-03 adds these extension points (on top of WP-01's 4 and WP-02's 6):
 *
 *  11. PMM alloc/free/stats/emergency callback  - pmm.h
 *  12. VMM create/destroy/map/unmap/protect     - vmm.h
 *  13. Kernel heap kmalloc/kfree/krealloc/stats - heap.h
 *  14. REMOVED in 3.0.0-WP03: shell command registration died with
 *      the oc> user plane (recovery/shell.h is gone).
 *
 * All WP-01 and WP-02 interfaces remain unchanged.
 */
#ifndef OC_EXT_WP3_H
#define OC_EXT_WP3_H

#include "mem_pmm.h"
#include "mem_vmm.h"
#include "mem_heap.h"

#endif /* OC_EXT_WP3_H */
