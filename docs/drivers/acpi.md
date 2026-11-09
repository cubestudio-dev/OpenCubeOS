<!-- SPDX-License-Identifier: Apache-2.0 -->
# ACPI

## The driver

`drivers/acpi/driver_acpi.c` - RSDP discovery (multiboot2 tags 14/15
on EFI, memory scan on BIOS), RSDT/XSDT, FADT, DSDT, MCFG.
`driver_acpi_aml.c` - a real AML namespace walker (the `acpidev`
command lists the namespace it finds).
`driver_acpi_power.c` + `arch_acpi_s3*.S` - S3 suspend/resume.

## What ACPI feeds

- Power control (FADT PM1a_CNT): `acpipow`, shutdown/reboot paths.
- The WDAT watchdog table (E2E-verified arming + real reset).
- GPE/SCI events.
- The S3 suspend path (suspend/suspend_test commands; the resume
  path returns through the assembly trampoline).

## Commands

```
oc> acpi        (table dump: RSDP/FADT/DSDT/MCFG)
oc> acpidev     (AML namespace devices)
oc> acpitest    (parser self-test)
oc> acpipow     (power control status)
```

## Bring-up notes

- q35 vs i440fx differ in table layouts - both parse.
- The AML walker is defensive: malformed tables stop the walk with
  a message, not a fault.
