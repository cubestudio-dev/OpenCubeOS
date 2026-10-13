/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10-input_chipset
 * File: shell/shell_cmds_chipset.h
 * Purpose: Shell commands for the input + chipset work package
 *          (ACPI, PCI/PCIe, MSI, I2C, SMBus, touchpad). Registered
 *          from kernel/main.c via shell_cmds_chipset_register().
 */
#ifndef OC_SHELL_CMDS_CHIPSET_H
#define OC_SHELL_CMDS_CHIPSET_H

/* acpi - ACPI table dump (RSDP, table list, FADT key fields) */
int shell_cmd_acpi(const char *args);
/* acpidev - ACPI namespace device enumeration (AML interpreter) */
int shell_cmd_acpidev(const char *args);
/* acpitest - AML interpreter self-test + live DSDT object checks */
int shell_cmd_acpitest(const char *args);
/* acpipow - sleep states / PM1 / GPE / FACS waking vector status */
int shell_cmd_acpipow(const char *args);
/* msitest - LAPIC self-IPI + MSI/MSI-X caps walk + e1000 MSI fire */
int shell_cmd_msitest(const char *args);
/* pcidump <b:d.f> - config space dump (extended when MMCONFIG active) */
int shell_cmd_pcidump(const char *args);
/* pcibars - BAR map of every enumerated device */
int shell_cmd_pcibars(const char *args);
/* pcie - MMCONFIG state + capability walk */
int shell_cmd_pcie(const char *args);
/* i2cdetect - scan registered I2C/SMBus buses */
int shell_cmd_i2cdetect(const char *args);
/* i2cget <bus> <addr> [reg] - read a byte */
int shell_cmd_i2cget(const char *args);
/* i2cset <bus> <addr> <reg> <val> - write a byte */
int shell_cmd_i2cset(const char *args);
/* i2ctest - I2C core + controller status checks */
int shell_cmd_i2ctest(const char *args);
/* smbus - SMBus device list from the scan */
int shell_cmd_smbus(const char *args);
/* smbustest - SMBus protocol suite against the EEPROM device */
int shell_cmd_smbustest(const char *args);
/* touchpad - touchpad binding + gesture event status */
int shell_cmd_touchpad(const char *args);
/* touchtest - multitouch pipeline self-test (fixture-driven) */
int shell_cmd_touchtest(const char *args);

void shell_cmds_chipset_register(void);

#endif /* OC_SHELL_CMDS_CHIPSET_H */
