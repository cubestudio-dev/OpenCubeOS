/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10b
 * File: kernel/nic.h
 * Purpose: NIC (network interface card) driver framework.
 *
 * WP-10b introduces a small device-driver layer for Ethernet NICs, in the
 * same shape as the WP-10a block layer (blk.h): a static registry of
 * devices plus per-driver operations.  The protocol stack (net.c) sends
 * and receives Ethernet frames through this layer; the legacy built-in
 * e1000/virtio-net paths in net.c stay as a fallback.
 *
 * Supported driver families (WP-10b):
 *
 *   e1000e   Intel 82574/82540-family PCIe/PCI GbE   (8086:10D3, 100E, 100F, 10EA, 15B8)
 *   igb      Intel 82575/82576 server GbE            (8086:10C9, 1521)
 *   ixgbe    Intel 82598/82599 10GbE                 (8086:10FB, 1563)
 *   rtl8139  Realtek RTL8139 100M                    (10EC:8139)
 *   rtl8168  Realtek RTL8168/8111 GbE                (10EC:8168, 8161)
 *   rtl8125  Realtek RTL8125 2.5G                    (10EC:8125)
 *   rtl810x  Realtek RTL810x 100M                    (10EC:8136, 8137)
 *   bcm57xx  Broadcom BCM57xx/BCM440x GbE            (14E4:16xx, 17xx, 440x)
 *   other    3c59x / nForce / Atheros AR81xx / Marvell (best-effort detect)
 *
 * All operations return 0 (or a positive byte count) on success and a
 * negative value on error, mirroring the blk layer convention.
 *
 * L1 extension surface (this header is the documented interface):
 *
 *   int driver_nic_register(driver_nic_device_t *dev, const driver_nic_ops_t *ops);
 *       Register a NIC with the framework.  dev->name and dev->type must
 *       be filled in; ops->send and ops->recv are mandatory.  Returns the
 *       device index (>= 0) or -1 when the table is full / arguments are
 *       invalid.  On success dev->index is filled in.
 *
 *   int driver_nic_send(driver_nic_device_t *dev, const void *buf, int len);
 *       Hand one Ethernet frame (<= 1514 bytes payload path) to the
 *       driver.  Returns the number of bytes queued (>= 0) or -1.
 *
 *   int driver_nic_recv(driver_nic_device_t *dev, void *buf, int maxlen);
 *       Poll for one received frame.  Returns frame length (> 0), 0 when
 *       no frame is pending, or -1 on error.
 *
 *   int driver_nic_link_status(driver_nic_device_t *dev);
 *       1 = link up, 0 = link down, -1 = driver cannot tell.
 *
 *   int driver_nic_get_mac(driver_nic_device_t *dev, u8 mac[6]);
 *       Copy the 6-byte station address.  0 on success, -1 on error.
 *
 *   int xxx_init(driver_pci_dev_t *pdev);   -- per-driver init entry points
 *       pdev == NULL: scan the PCI bus for the family's vendor/device IDs
 *       and initialise the first match.  pdev != NULL: initialise exactly
 *       that PCI function.  Returns 0 on success, -1 when no device is
 *       present or init fails.  Drivers that initialise successfully
 *       register themselves with driver_nic_register().
 */
#ifndef OC_NIC_H
#define OC_NIC_H

#include "types.h"
#include "driver_pci.h"

/* ---- Registry limits ---- */
#define NIC_MAX_DEVICES  8
#define NIC_NAME_LEN     16

/* ---- Driver families ---- */
typedef enum {
    NIC_TYPE_E1000E  = 1,
    NIC_TYPE_IGB     = 2,
    NIC_TYPE_IXGBE   = 3,
    NIC_TYPE_RTL8139 = 4,
    NIC_TYPE_RTL8168 = 5,
    NIC_TYPE_RTL8125 = 6,
    NIC_TYPE_RTL810X = 7,
    NIC_TYPE_BCM57XX = 8,
    NIC_TYPE_OTHER   = 9,
} driver_nic_type_t;

/* Per-driver operations.  send/recv are mandatory; link_status/get_mac
 * may be NULL (the framework then reports -1 / returns the MAC captured
 * at registration time). */
typedef struct driver_nic_device driver_nic_device_t;

typedef struct driver_nic_ops {
    int (*send)(driver_nic_device_t *dev, const void *buf, int len);
    int (*recv)(driver_nic_device_t *dev, void *buf, int maxlen);
    int (*link_status)(driver_nic_device_t *dev);
    int (*get_mac)(driver_nic_device_t *dev, u8 mac[6]);
} driver_nic_ops_t;

/* One registered NIC.  name/type/mac/pci ids describe the hardware;
 * ops/priv bind the driver implementation. */
struct driver_nic_device {
    char     name[NIC_NAME_LEN];
    driver_nic_type_t type;
    u8       mac[6];
    u8       present;
    u8       bus, dev, func;
    u16      vendor_id, device_id;
    const driver_nic_ops_t *ops;
    void    *priv;
    int      index;
};

/* ---- Framework API (L1 extension surface) ---- */
void driver_nic_init(void);

/* Register a NIC (see file-header documentation). */
int  driver_nic_register(driver_nic_device_t *dev, const driver_nic_ops_t *ops);

/* Send / receive one Ethernet frame through a driver. */
int  driver_nic_send(driver_nic_device_t *dev, const void *buf, int len);
int  driver_nic_recv(driver_nic_device_t *dev, void *buf, int maxlen);

/* Link state: 1 up, 0 down, -1 unknown. */
int  driver_nic_link_status(driver_nic_device_t *dev);

/* Station address: 0 on success, -1 on error. */
int  driver_nic_get_mac(driver_nic_device_t *dev, u8 mac[6]);

/* ---- Registry helpers ---- */
int  driver_nic_num_devices(void);
driver_nic_device_t *driver_nic_get_device(int idx);
int  driver_nic_find_by_type(driver_nic_type_t type);       /* index / -1 */
int  driver_nic_find_first_present(void);            /* index / -1 */
void driver_nic_list_devices(void);

/* Probe every WP-10b driver family in priority order (e1000e, igb,
 * rtl8168/8125/810x, rtl8139, bcm57xx, ixgbe, other).  Returns 0 when at
 * least one NIC initialised and registered, -1 when none did.  Called
 * once from net_init() before the legacy fallbacks. */
int  driver_nic_probe_all(void);

/* The device net.c routes frames to (first registered NIC), or NULL. */
driver_nic_device_t *driver_nic_active(void);

/* Per-driver init entry points (see file-header documentation). */
int driver_nic_e1000e_init(driver_pci_dev_t *pdev);
int driver_nic_igb_init(driver_pci_dev_t *pdev);
int driver_nic_ixgbe_init(driver_pci_dev_t *pdev);
int driver_nic_rtl8139_init(driver_pci_dev_t *pdev);
int rtl8168_init(driver_pci_dev_t *pdev);
int rtl8125_init(driver_pci_dev_t *pdev);
int rtl810x_init(driver_pci_dev_t *pdev);
int bcm57xx_init(driver_pci_dev_t *pdev);

/* Best-effort probe for the remaining legacy families: 3Com 3c59x,
 * NVIDIA nForce, Atheros AR81xx, Marvell Yukon.  Returns 0 when one of
 * them initialised, -1 otherwise. */
int other_nics_init(driver_pci_dev_t *pdev);
void other_nics_print_state(void);

/* WP-10b shell surface: tests + status commands (kernel/driver_nic_test_cmds.c). */
void shell_cmds_nic_test_register(void);

/* Status printers for the shell commands (one per family). */
void driver_nic_e1000e_print_state(void);
void driver_nic_igb_print_state(void);
void driver_nic_ixgbe_print_state(void);
void driver_nic_rtl8139_print_state(void);
void rtl8168_print_state(void);
void rtl8125_print_state(void);
void rtl810x_print_state(void);
void bcm57xx_print_state(void);

#endif /* OC_NIC_H */
