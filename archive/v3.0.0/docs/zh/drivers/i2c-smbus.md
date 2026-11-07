<!-- SPDX-License-Identifier: Apache-2.0 -->
# I2C / SMBus

## 驱动

`drivers/i2c/driver_i2c.c`（核心）+ `driver_i2c_i801.c`（Intel SMBus 控制器）+ `driver_i2c_dw.c`（DesignWare）+ `driver_smbus.c`。

## 命令

```
oc> i2cdetect    (bus scan)
oc> i2cget       (read a register)
oc> i2cset       (write a register)
oc> i2ctest      (bus self-test)
oc> smbus        (controller status)
oc> smbustest    (SMBus protocol test)
```

## 其上承载的设备

I2C HID 设备（触摸板）、RTC 芯片以及特定于主板的控制器 - 在真实硬件上，`i2cdetect` 始终是首选的探测工具。
