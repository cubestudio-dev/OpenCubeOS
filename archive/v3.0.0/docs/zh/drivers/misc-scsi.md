<!-- SPDX-License-Identifier: Apache-2.0 -->
# 杂项设备与 SCSI

## SCSI

`drivers/scsi/driver_scsi.c` + `driver_scsi_lsi.c`（LSI 53C8xx 系列）。QEMU：`-device lsi53c810`。命令：`scsi`（总线/设备状态）、`scsitest`。设备通过块设备家族注册。

## 并行端口

`drivers/misc/driver_parallel.c` - `parallel`（状态）+ `paralleltest`。经典的 LP 接口。

## 游戏端口

`drivers/misc/driver_gameport.c` - `gameport` + `gameporttest`（模拟摇杆）。

## 红外线

`drivers/misc/driver_ir.c` - `ir` + `irtest`（原始脉冲捕获与解码）。
