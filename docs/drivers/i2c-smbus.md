<!-- SPDX-License-Identifier: Apache-2.0 -->
# I2C / SMBus

## The drivers

`drivers/i2c/driver_i2c.c` (the core) + `driver_i2c_i801.c` (the
Intel SMBus controller) + `driver_i2c_dw.c` (DesignWare) +
`driver_smbus.c`.

## Commands

```
oc> i2cdetect    (bus scan)
oc> i2cget       (read a register)
oc> i2cset       (write a register)
oc> i2ctest      (bus self-test)
oc> smbus        (controller status)
oc> smbustest    (SMBus protocol test)
```

## What rides on it

I2C HID devices (touchpads), RTC chips, and board-specific
controllers - `i2cdetect` is always the first probe on real
hardware.
