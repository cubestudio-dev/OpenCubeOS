<!-- SPDX-License-Identifier: Apache-2.0 -->
# Misc devices and SCSI

## SCSI

`drivers/scsi/driver_scsi.c` + `driver_scsi_lsi.c` (the LSI 53C8xx
family). QEMU: `-device lsi53c810`. Commands: `scsi` (bus/device
status), `scsitest`. Devices register through the block family.

## Parallel port

`drivers/misc/driver_parallel.c` - `parallel` (status) +
`paralleltest`. The classic LP interface.

## Gameport

`drivers/misc/driver_gameport.c` - `gameport` + `gameporttest`
(analog joysticks).

## Infrared

`drivers/misc/driver_ir.c` - `ir` + `irtest` (raw pulse capture and
decode).
