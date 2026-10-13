/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10-input_chipset
 * File: drivers/acpi/driver_acpi_aml.h
 * Purpose: Minimal AML (ACPI Machine Language) interpreter - namespace
 *          construction from DSDT/SSDT term lists plus evaluation of
 *          control-method objects.
 *
 * Covered AML (the subset QEMU/SeaBIOS/OVMF and mainstream firmware
 * actually use for the objects the kernel needs):
 *   - Namespace: Scope, Device, Name, Method, OpRegion, Field,
 *     IndexField, Mutex, Alias, Processor/ThermalZone/PowerRes (as
 *     named containers), CreateByte/Word/DWord/QWord/BitField
 *   - Data: Integer (64-bit), Buffer, Package, String, constants
 *     (Zero/One/Ones/Byte/Word/DWord/QWord/Revision)
 *   - Control flow: If/Else/While/Break/Continue/Return/Noop
 *   - Arithmetic: Add/Subtract/Multiply/Divide/Mod/And/Or/Xor/Not/
 *     Nand/Nor/ShiftLeft/ShiftRight/Increment/Decrement
 *   - Logical: LAnd/LOr/LNot/LEqual/LGreater/LLess + derived
 *   - Object access: Store/RefOf/DerefOf/Index/SizeOf/ObjectType/
 *     CopyObject/Mid/CondRefOf/Match
 *   - Conversion: ToBuffer/ToInteger/ToString/ToHexString/
 *     ToDecimalString/FromBCD/ToBCD/Concatenate/ConcatenateResTemplate
 *   - Field access on SystemIO/SystemMemory/PCI_Config OpRegions
 *   - Methods with up to 7 args + 8 locals, recursion depth 8,
 *     op-budget per invocation (524288) to bound runaway firmware.
 *
 * Out of scope (honestly reported if hit): Load/Unload, DataRegion,
 * BankField, Event/Wait/Signal, Serialized mutex semantics (Acquire/
 * Release are recorded but non-blocking), GPE method dispatch is
 * implemented in the power module on top of this namespace.
 */
#ifndef OC_DRIVER_ACPI_AML_H
#define OC_DRIVER_ACPI_AML_H

#include "types.h"

/* Load (parse + define) a DSDT/SSDT at its physical address.
 * The definition block runs once: named objects enter the namespace,
 * control methods are stored for later invocation. Returns 0 on
 * success, -1 on parse error (boot log carries the detail). */
int driver_acpi_aml_load_table(u64 table_phys);

/* 1 when at least one table was loaded. */
int driver_acpi_aml_loaded(void);

/* Namespace statistics for the `acpidev` / `acpitest` commands. */
u32 driver_acpi_aml_node_count(void);
u32 driver_acpi_aml_method_count(void);

/* One device discovered in the namespace. */
typedef struct acpi_aml_dev_info {
    char path[96];      /* full path, e.g. \_SB_.PCI0.SATA */
    char hid[12];       /* evaluated _HID ("PNP0C50", "80860F41", ...) */
    char cid[12];       /* evaluated first _CID entry ("" when absent) */
    u32  adr;           /* evaluated _ADR (0 when absent) */
    int  sta;           /* evaluated _STA (-1 when absent, then "present") */
    int  is_device;     /* 1 = Device node, 0 = Processor/ThermalZone/PowerRes */
} acpi_aml_dev_info_t;

/* Enumerate Device/Processor/ThermalZone nodes. Returns the total
 * count; `idx` + `out` fetch one entry (0 = success). */
int driver_acpi_aml_device_count(void);
int driver_acpi_aml_device_info(int idx, acpi_aml_dev_info_t *out);

/* Evaluate an integer-valued object/method: _STA, _ADR, _S0.. package
 * elements go through the package helper below. Returns 0 on success. */
int driver_acpi_aml_eval_integer(const char *path, u64 *out);

/* Evaluate a package and fetch element `idx` as an integer (_S5). */
int driver_acpi_aml_eval_package_integer(const char *path, int idx, u64 *out);
int driver_acpi_aml_package_elements(const char *path, int *count);

/* Evaluate _HID / _CID of a device path as a string ("PNP0C50" form or
 * the raw integer rendered as 8 hex digits, e.g. "0C0D8086"->EisaId).
 * Returns 0 on success, -1 when absent. */
int driver_acpi_aml_eval_hid(const char *path, char *out, int outlen);
int driver_acpi_aml_eval_cid(const char *path, char *out, int outlen);

/* Evaluate a buffer-valued object/method (_CRS, _DSM blobs).
 * Returns 0 on success, -1 when absent, -2 when buffer too small. */
int driver_acpi_aml_eval_buffer(const char *path, u8 *out, int outcap, int *outlen);

/* Invoke the \_GPE._Lxx/_Exx method for a GPE number (power module). */
int driver_acpi_aml_gpe_method_exists(u8 gpe_number);
int driver_acpi_aml_run_gpe_method(u8 gpe_number, int level_triggered);

/* Power button notification path: runs \_SB.PWRB._EVT when the firmware
 * namespace defines the power-button device, otherwise logs. Called
 * from the SCI handler on PM1 PWRBTN_STS. */
void driver_acpi_aml_notify_power_button(void);

/* Interpreter self-test (fixture tables compiled in; no hardware).
 * Runs against a throwaway namespace: call BEFORE loading the live
 * DSDT/SSDT (boot does this); the result is cached and reported by
 * driver_acpi_aml_selftest_result() afterwards. */
int driver_acpi_aml_selftest(void);
u32  driver_acpi_aml_selftest_result(void);

#endif /* OC_DRIVER_ACPI_AML_H */
