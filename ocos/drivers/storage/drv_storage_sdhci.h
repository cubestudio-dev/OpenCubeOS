/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 cubestudio-dev <cubestudio@qq.com>
 * Open Cube OS - WP-10-wireless_bluetooth
 *
 * File: drivers/sd/driver_sdhci.h
 *
 * SD Host Controller (SDHCI, SD Host Controller Standard Specification)
 * + SDIO bus.  Public interface: host init + SDIO client drivers
 * (brcmfmac SDIO registers here), plus CMD52/CMD53 register-level bus
 * access for the client drivers.
 */
#ifndef DRIVER_SDHCI_H
#define DRIVER_SDHCI_H

#include <types.h>

/* SDIO card view (client drivers use this)                            */
typedef struct sdio_func {
    u8  number;                       /* 0 = CCCR/func0, 1..7 functions  */
    u16 vendor_id;                    /* from CCCR CIS probe             */
    u16 device_id;
    u8  class_code;
    u8  present;
} sdio_func_t;

typedef struct sdio_driver {
    const char *name;
    u16         vendor_id;            /* 0xffff = match any              */
    u16         device_id;            /* 0xffff = match any              */
    int       (*probe)(sdio_func_t *func);
    void      (*remove)(sdio_func_t *func);
} sdio_driver_t;

/* Bring-up.  Called once from kernel/main.c after the PCI scan.       */
int  driver_sdhci_init(void);

/* SDIO client-driver registry (WP-03 G3: drivers self-declare via
 * OC_DRIVER_DECL("sdio", ...) and the core collects them at init;
 * the count accessor stays public for the shell/status surfaces). */
int  driver_sdio_driver_count(void);

/* Card state for the shell (honest states only).                      */
const char *driver_sdhci_state(void);      /* "no-host" / "empty" / "sdio" */
int  driver_sdhci_host_count(void);
u32  driver_sdhci_capabilities(int host);
u8   driver_sdhci_num_io_functions(int host);

/* CMD52 (IO_RW_DIRECT) - single byte register access on the SDIO bus. */
int  driver_sdio_rw_direct(int host, int func, u32 addr, int write,
                           u8 in_byte, u8 *out_byte);
/* CMD53 (IO_RW_EXTENDED) - block transfer on the SDIO bus.            */
int  driver_sdio_rw_extended(int host, int func, u32 addr, int write,
                             u8 *buf, u16 len, int block_mode);

/* Self-test (register-encoding + state-machine checks; used by the
 * `wifi sdio` shell command and the host suite).                      */
int  driver_sdhci_selftest(char *out, int outlen);

#endif /* DRIVER_SDHCI_H */
