/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 2.0.0 - services/nic: driver-internal interface.
 *
 * Kernel-internal ONLY: NIC drivers (the NIC driver files, the wireless
 * bridge and the netfilter/netmap virtual cards) include this header
 * to add a probed card to the NIC service table.  NOT part of the L1
 * surface: the register-class entry was removed in v2.0.0; drivers
 * declare identity with OC_DRIVER_DECL("nic", ...) and call
 * nic_dev_add() from inside their probe.
 */
#ifndef OC_NIC_DRV_INTERNAL_H
#define OC_NIC_DRV_INTERNAL_H

#include "types.h"
#include "drv_bus_pci.h"

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

/* Per-driver init entry points (see file-header documentation). */
int driver_nic_e1000e_init(driver_pci_dev_t *pdev);
void driver_nic_e1000e_resume(void);
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

/* WP-10-chinese_support-fix2: replay the active NIC's register
 * programming after an ACPI S3 resume (platform reset wiped the
 * device's register state; rings/buffers in RAM survived). */
void driver_nic_resume(void);


#ifdef __cplusplus
extern "C" {
#endif

/* Probe descriptor each nic driver declares in its own file:
 *   static const nic_drv_ops_t g_probe = { my_probe };
 *   OC_DRIVER_DECL("nic", "e1000e", &g_probe, "Intel E1000E");
 * my_probe() scans the bus, inits the first match and adds the card
 * via nic_dev_add().  Returns 0 when a card was added. */
typedef struct {
    int (*probe)(void);   /* 0 = a card was found and added */
} nic_drv_ops_t;

/* Add a probed card to the NIC service table (was the L1-surface
 * driver_nic_register(); internal since v2.0.0). */
int nic_dev_add(driver_nic_device_t *dev, const driver_nic_ops_t *ops);

#ifdef __cplusplus
}
#endif

#endif /* OC_NIC_DRV_INTERNAL_H */
