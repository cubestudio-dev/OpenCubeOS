/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10b
 * File: drivers/nic/driver_nic.c
 * Purpose: NIC driver framework - registry + probe scheduling.
 *
 * The registry is a static array (like the WP-10a blk layer).  Drivers
 * register through driver_nic_register(); driver_nic_probe_all() walks the supported
 * families in priority order and stops at the first device that
 * initialises.  net.c routes frames through driver_nic_active().
 */
#include "nic_svc.h"
#include "drv_internal.h"
#include "core_registry.h"
#include "screen_console.h"
#include "core_sync.h"
#include "lib_string.h"
#include "mem_heap.h"

#include <stdint.h>

static driver_nic_device_t g_nic_devs[NIC_MAX_DEVICES];
static int g_nic_count = 0;

void nic_svc_init(void) {
    memset(g_nic_devs, 0, sizeof(g_nic_devs));
    g_nic_count = 0;
}

int nic_dev_add(driver_nic_device_t *dev, const driver_nic_ops_t *ops) {
    if (!dev || !ops || !ops->send || !ops->recv) return -1;
    if (!dev->name[0]) return -1;
    if (dev->type < NIC_TYPE_E1000E || dev->type > NIC_TYPE_OTHER) return -1;

    for (int i = 0; i < NIC_MAX_DEVICES; i++) {
        if (g_nic_devs[i].present) continue;

        driver_nic_device_t *d = &g_nic_devs[i];
        u8 save_index = dev->index;      /* preserve caller scratch */
        (void)save_index;
        memset(d, 0, sizeof(*d));
        strncpy(d->name, dev->name, NIC_NAME_LEN - 1);
        d->name[NIC_NAME_LEN - 1] = 0;
        d->type = dev->type;
        for (int k = 0; k < 6; k++) d->mac[k] = dev->mac[k];
        d->present = 1;
        d->bus = dev->bus;
        d->dev = dev->dev;
        d->func = dev->func;
        d->vendor_id = dev->vendor_id;
        d->device_id = dev->device_id;
        d->ops = ops;
        d->priv = dev->priv;
        d->index = i;
        dev->index = i;                  /* hand the index back to caller */
        if (i >= g_nic_count) g_nic_count = i + 1;
        return i;
    }
    return -1;
}

/* BUG-0334 (A8-16): driver send/recv paths are single-instance globals
 * with cursor state (tx_tail/rx_tail/capr/...) and NO internal locking,
 * while both the shell (nic_rw_test) and the timer-IRQ net_poll call
 * straight into driver_nic_send/recv. Every dispatch now runs under one
 * irqsave spinlock (BUG-0285 semantics), so a task-context sender can no
 * longer race the IRQ poller on the same descriptor ring. The critical
 * section covers the whole driver op: drivers poll their own completion
 * registers inside (bounded waits) and never re-enter this lock, and a
 * spinning IRQ waiter only defers ticks until the holder finishes. */
static spinlock_t g_nic_data_lock;

int driver_nic_send(driver_nic_device_t *dev, const void *buf, int len) {
    if (!dev || !dev->present || !dev->ops || !dev->ops->send) return -1;
    if (!buf || len <= 0) return -1;
    spin_lock(&g_nic_data_lock);
    int rc = dev->ops->send(dev, buf, len);
    spin_unlock(&g_nic_data_lock);
    return rc;
}

int driver_nic_recv(driver_nic_device_t *dev, void *buf, int maxlen) {
    if (!dev || !dev->present || !dev->ops || !dev->ops->recv) return -1;
    if (!buf || maxlen <= 0) return -1;
    spin_lock(&g_nic_data_lock);
    int rc = dev->ops->recv(dev, buf, maxlen);
    spin_unlock(&g_nic_data_lock);
    return rc;
}

int driver_nic_link_status(driver_nic_device_t *dev) {
    if (!dev || !dev->present || !dev->ops) return -1;
    if (!dev->ops->link_status) return -1;
    return dev->ops->link_status(dev);
}

int driver_nic_get_mac(driver_nic_device_t *dev, u8 mac[6]) {
    if (!dev || !dev->present || !mac) return -1;
    if (dev->ops && dev->ops->get_mac) return dev->ops->get_mac(dev, mac);
    for (int k = 0; k < 6; k++) mac[k] = dev->mac[k];
    return 0;
}

int driver_nic_num_devices(void) {
    int n = 0;
    for (int i = 0; i < NIC_MAX_DEVICES; i++)
        if (g_nic_devs[i].present) n++;
    return n;
}

driver_nic_device_t *driver_nic_get_device(int idx) {
    if (idx < 0 || idx >= NIC_MAX_DEVICES) return NULL;
    return g_nic_devs[idx].present ? &g_nic_devs[idx] : NULL;
}

int driver_nic_find_by_type(driver_nic_type_t type) {
    for (int i = 0; i < NIC_MAX_DEVICES; i++)
        if (g_nic_devs[i].present && g_nic_devs[i].type == type) return i;
    return -1;
}

int driver_nic_find_first_present(void) {
    for (int i = 0; i < NIC_MAX_DEVICES; i++)
        if (g_nic_devs[i].present) return i;
    return -1;
}

void driver_nic_list_devices(void) {
    char line[96];
    char n[24];
    int any = 0;
    for (int i = 0; i < NIC_MAX_DEVICES; i++) {
        driver_nic_device_t *d = &g_nic_devs[i];
        if (!d->present) continue;
        any = 1;
        strcpy(line, "nic");
        u64_to_str((u64)i, n); strcat(line, n);
        strcat(line, "  ");
        strcat(line, d->name);
        for (int pad = (int)strlen(d->name); pad < 10; pad++) strcat(line, " ");
        strcat(line, " mac=");
        for (int k = 0; k < 6; k++) {
            u64_to_hex(d->mac[k], n, 2);
            strcat(line, n);
            if (k < 5) strcat(line, ":");
        }
        strcat(line, " pci=");
        u64_to_hex(d->bus, n, 2); strcat(line, n);
        strcat(line, ":");
        u64_to_hex(d->dev, n, 2); strcat(line, n);
        strcat(line, ".");
        u64_to_hex(d->func, n, 1); strcat(line, n);
        strcat(line, " vid:did=");
        u64_to_hex(d->vendor_id, n, 4); strcat(line, n);
        strcat(line, ":");
        u64_to_hex(d->device_id, n, 4); strcat(line, n);
        strcat(line, "\n");
        screen_console_puts(line);
    }
    if (!any) screen_console_puts("no NIC devices registered\n");
}

driver_nic_device_t *driver_nic_active(void) {
    int idx = driver_nic_find_first_present();
    if (idx < 0) return NULL;
    return &g_nic_devs[idx];
}

/* Probe every family in priority order.  The first success wins (the
 * kernel currently routes through a single NIC).  Probe order puts the
 * QEMU-verifiable PCIe families first and best-effort legacy last. */
int nic_svc_probe_all(void) {
    /* v2.0.0: probe list COLLECTED from the OC_DRIVER_DECL("nic", ...)
     * declarations (was a hardcoded function-pointer table).  Order
     * below is the historical mainstream-first priority. */
    static const char *const order[] = {
        "e1000e", "igb", "rtl8168", "rtl8125", "rtl810x",
        "rtl8139", "bcm57xx", "ixgbe", "other",
    };
    for (unsigned k = 0; k < sizeof(order) / sizeof(order[0]); k++) {
        for (int i = 0; i < core_registry_drvs_count(); i++) {
            const oc_driver_decl_t *c = core_registry_drv_at(i);
            if (!c || strcmp(c->family, "nic") != 0) continue;
            if (strcmp(c->name, order[k]) != 0) continue;
            const nic_drv_ops_t *ops = (const nic_drv_ops_t *)c->ops;
            if (ops && ops->probe && ops->probe() == 0) return 0;
        }
    }
    return -1;
}

/* WP-10-chinese_support-fix2: S3 resume hook. The kernel routes through
 * a single NIC (driver_nic_probe_all picks the first family that
 * probes); only the e1000e family has a register replay today. Other
 * families get the generic PCI command restore from
 * driver_pci_restore_commands() and need a same-shape replay hook if
 * they are ever resumed on real hardware (recorded in KNOWN_ISSUES). */
void driver_nic_resume(void) {
    driver_nic_e1000e_resume();
}

/* ---- v2.0.0 file-local service identity ---- */
/* v2.0.0: the service identity (with its ring-3 surface) is declared
 * in services/nic/nic_l1.c; the bring-up callbacks stay here. */
