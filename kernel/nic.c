/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10b
 * File: kernel/nic.c
 * Purpose: NIC driver framework - registry + probe scheduling.
 *
 * The registry is a static array (like the WP-10a blk layer).  Drivers
 * register through nic_register(); nic_probe_all() walks the supported
 * families in priority order and stops at the first device that
 * initialises.  net.c routes frames through nic_active().
 */
#include "nic.h"
#include "console.h"
#include "string.h"
#include "heap.h"

#include <stdint.h>

static nic_device_t g_nic_devs[NIC_MAX_DEVICES];
static int g_nic_count = 0;

void nic_init(void) {
    oc_memset(g_nic_devs, 0, sizeof(g_nic_devs));
    g_nic_count = 0;
}

int nic_register(nic_device_t *dev, const nic_ops_t *ops) {
    if (!dev || !ops || !ops->send || !ops->recv) return -1;
    if (!dev->name[0]) return -1;
    if (dev->type < NIC_TYPE_E1000E || dev->type > NIC_TYPE_OTHER) return -1;

    for (int i = 0; i < NIC_MAX_DEVICES; i++) {
        if (g_nic_devs[i].present) continue;

        nic_device_t *d = &g_nic_devs[i];
        u8 save_index = dev->index;      /* preserve caller scratch */
        (void)save_index;
        oc_memset(d, 0, sizeof(*d));
        oc_strncpy(d->name, dev->name, NIC_NAME_LEN - 1);
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

int nic_send(nic_device_t *dev, const void *buf, int len) {
    if (!dev || !dev->present || !dev->ops || !dev->ops->send) return -1;
    if (!buf || len <= 0) return -1;
    return dev->ops->send(dev, buf, len);
}

int nic_recv(nic_device_t *dev, void *buf, int maxlen) {
    if (!dev || !dev->present || !dev->ops || !dev->ops->recv) return -1;
    if (!buf || maxlen <= 0) return -1;
    return dev->ops->recv(dev, buf, maxlen);
}

int nic_link_status(nic_device_t *dev) {
    if (!dev || !dev->present || !dev->ops) return -1;
    if (!dev->ops->link_status) return -1;
    return dev->ops->link_status(dev);
}

int nic_get_mac(nic_device_t *dev, u8 mac[6]) {
    if (!dev || !dev->present || !mac) return -1;
    if (dev->ops && dev->ops->get_mac) return dev->ops->get_mac(dev, mac);
    for (int k = 0; k < 6; k++) mac[k] = dev->mac[k];
    return 0;
}

int nic_num_devices(void) {
    int n = 0;
    for (int i = 0; i < NIC_MAX_DEVICES; i++)
        if (g_nic_devs[i].present) n++;
    return n;
}

nic_device_t *nic_get_device(int idx) {
    if (idx < 0 || idx >= NIC_MAX_DEVICES) return NULL;
    return g_nic_devs[idx].present ? &g_nic_devs[idx] : NULL;
}

int nic_find_by_type(nic_type_t type) {
    for (int i = 0; i < NIC_MAX_DEVICES; i++)
        if (g_nic_devs[i].present && g_nic_devs[i].type == type) return i;
    return -1;
}

int nic_find_first_present(void) {
    for (int i = 0; i < NIC_MAX_DEVICES; i++)
        if (g_nic_devs[i].present) return i;
    return -1;
}

void nic_list_devices(void) {
    char line[96];
    char n[24];
    int any = 0;
    for (int i = 0; i < NIC_MAX_DEVICES; i++) {
        nic_device_t *d = &g_nic_devs[i];
        if (!d->present) continue;
        any = 1;
        oc_strcpy(line, "nic");
        oc_u64_to_str((u64)i, n); oc_strcat(line, n);
        oc_strcat(line, "  ");
        oc_strcat(line, d->name);
        for (int pad = (int)oc_strlen(d->name); pad < 10; pad++) oc_strcat(line, " ");
        oc_strcat(line, " mac=");
        for (int k = 0; k < 6; k++) {
            oc_u64_to_hex(d->mac[k], n, 2);
            oc_strcat(line, n);
            if (k < 5) oc_strcat(line, ":");
        }
        oc_strcat(line, " pci=");
        oc_u64_to_hex(d->bus, n, 2); oc_strcat(line, n);
        oc_strcat(line, ":");
        oc_u64_to_hex(d->dev, n, 2); oc_strcat(line, n);
        oc_strcat(line, ".");
        oc_u64_to_hex(d->func, n, 1); oc_strcat(line, n);
        oc_strcat(line, " vid:did=");
        oc_u64_to_hex(d->vendor_id, n, 4); oc_strcat(line, n);
        oc_strcat(line, ":");
        oc_u64_to_hex(d->device_id, n, 4); oc_strcat(line, n);
        oc_strcat(line, "\n");
        oc_console_puts(line);
    }
    if (!any) oc_console_puts("no NIC devices registered\n");
}

nic_device_t *nic_active(void) {
    int idx = nic_find_first_present();
    if (idx < 0) return NULL;
    return &g_nic_devs[idx];
}

/* Probe every family in priority order.  The first success wins (the
 * kernel currently routes through a single NIC).  Probe order puts the
 * QEMU-verifiable PCIe families first and best-effort legacy last. */
int nic_probe_all(void) {
    static const struct {
        const char *name;
        int (*init)(pci_dev_t *pdev);
    } probes[] = {
        { "e1000e",  e1000e_init  },
        { "igb",     igb_init     },
        { "rtl8168", rtl8168_init },
        { "rtl8125", rtl8125_init },
        { "rtl810x", rtl810x_init },
        { "rtl8139", rtl8139_init },
        { "bcm57xx", bcm57xx_init },
        { "ixgbe",   ixgbe_init   },
        { "other",   other_nics_init },
    };

    for (unsigned i = 0; i < sizeof(probes) / sizeof(probes[0]); i++) {
        if (probes[i].init(NULL) == 0) {
            return 0;
        }
    }
    return -1;
}
