<!-- SPDX-License-Identifier: Apache-2.0 -->
# 应用字典 - 摄像头 (10)

### gameport

| 字段 | 值 |
|---|---|
| 名称 | gameport |
| 分类 | 摄像头 |
| 功能 | 游戏端口状态 |
| 用法 | `gameport` |
| 来源 | `ocos/services/camera/src/svc_camera_l1.c` |

**实际执行示例**：

```
gameport
game ports: 1
  gp0 io=0x201 calibrated=no reads=0 timeouts=0
    axis 0: no stick (timeout) rc=0
    axis 1: no stick (timeout) rc=0
    axis 2: no stick (timeout) rc=0
    axis 3: no stick (timeout) rc=0
    buttons=0x00
ush> 
```

### gameporttest

| 字段 | 值 |
|---|---|
| 名称 | gameporttest |
| 分类 | 摄像头 |
| 功能 | 游戏端口测试 |
| 用法 | `gameporttest` |
| 来源 | `ocos/services/camera/src/svc_camera_l1.c` |

**实际执行示例**：

```
gameporttest
gameporttest: analog joystick port self test
  axis read0: PASS
  axis read1: PASS
  axis read2: PASS
  axis read3: PASS
  buttons read: PASS
  calibrate runs: PASS
  calibration windows: [0,0] [0,0] [0,0] [0,0] 
gameporttest: 0 failures - PASS
ush> 
```

### ir

| 字段 | 值 |
|---|---|
| 名称 | ir |
| 分类 | 摄像头 |
| 功能 | 红外接收状态 |
| 用法 | `ir` |
| 来源 | `ocos/services/camera/src/svc_camera_l1.c` |

**实际执行示例**：

```
ir
ir devices: 0
  no IR device present (QEMU models neither CIR hardware nor an IrDA UART; the SIR probe runs against the secondary 16550 at boot)
ush> 
```

### irtest

| 字段 | 值 |
|---|---|
| 名称 | irtest |
| 分类 | 摄像头 |
| 功能 | 红外接收测试 |
| 用法 | `irtest` |
| 来源 | `ocos/services/camera/src/svc_camera_l1.c` |

**实际执行示例**：

```
irtest
irtest: NEC / RC5 / RC6 / SIRC codec round trips
  nec known (1A/3C): PASS
  rc5 known (05/15 t1): PASS
  rc6 known (12/34 t0): PASS
  rt nec 0x12/0x34: PASS
  rt nec 0xFF/0x00: PASS
  rt nec repeat: PASS
  rt rc5 0x0A/0x0C t0: PASS
  rt rc5 0x1B/0x2D t1: PASS
  rt rc6 0x00/0xFF t0: PASS
  rt rc6 0xA5/0x5A t1: PASS
  rt sirc 0x15/0x01: PASS
  nec corrupted rejected: PASS
  live ir devices: 0
  (no live device: codec tests above ran on the real code paths)
irtest: 0 failures - PASS
ush> 
```

### parallel

| 字段 | 值 |
|---|---|
| 名称 | parallel |
| 分类 | 摄像头 |
| 功能 | 并口状态 |
| 用法 | `parallel` |
| 来源 | `ocos/services/camera/src/svc_camera_l1.c` |

**实际执行示例**：

```
parallel
parallel ports: 1
  lpt1 io=0x378 irq=7 mode=SPP ecp=no bidir=no tx=0 rx=0 timeouts=0 status=0xd9
ush> 
```

### paralleltest

| 字段 | 值 |
|---|---|
| 名称 | paralleltest |
| 分类 | 摄像头 |
| 功能 | 并口测试 |
| 用法 | `paralleltest` |
| 来源 | `ocos/services/camera/src/svc_camera_l1.c` |

**实际执行示例**：

```
paralleltest
paralleltest: IEEE 1284 register-level self test
  mode SPP: PASS
  spp forward write 8 bytes: PASS
  status read: PASS
  status: raw=0x59 busy=0 ack=1 sel=1 err=1
  nibble reverse honest: PASS
  mode EPP1.7: PASS
  epp1.7 write honest: PASS
  mode EPP1.9: PASS
  epp1.9 read honest: PASS
  epp addr write honest: PASS
  ecp capability: no
  ecp absent honest: PASS
paralleltest: 0 failures - PASS
ush> 
```

### scsi

| 字段 | 值 |
|---|---|
| 名称 | scsi |
| 分类 | 摄像头 |
| 功能 | SCSI 设备扫描 |
| 用法 | `scsi` |
| 来源 | `ocos/services/camera/src/svc_camera_l1.c` |

**实际执行示例**：

```
scsi
scsi HBAs: 0
scsi devices: 0
  (no SCSI targets present; QEMU models one via -device lsi)
ush> 
```

### scsitest

| 字段 | 值 |
|---|---|
| 名称 | scsitest |
| 分类 | 摄像头 |
| 功能 | SCSI 设备测试 |
| 用法 | `scsitest` |
| 来源 | `ocos/services/camera/src/svc_camera_l1.c` |

**实际执行示例**：

```
scsitest
scsitest: CDB goldens, parsers, live target checks
  read10 golden: PASS
  write10 golden: PASS
  inquiry golden: PASS
  tur golden: PASS
  rc16 action: PASS
  inq parse: PASS
  cap10 parse: PASS
  cap16 parse: PASS
  sense parse: PASS
  (no live SCSI target: pure-function checks above ran on the real code paths)
scsitest: 0 failures - PASS
ush> 
```

### uvc

| 字段 | 值 |
|---|---|
| 名称 | uvc |
| 分类 | 摄像头 |
| 功能 | UVC 摄像头状态 |
| 用法 | `uvc` |
| 来源 | `ocos/services/camera/src/svc_camera_l1.c` |

**实际执行示例**：

```
uvc
```

### uvctest

| 字段 | 值 |
|---|---|
| 名称 | uvctest |
| 分类 | 摄像头 |
| 功能 | UVC 摄像头测试 |
| 用法 | `uvctest` |
| 来源 | `ocos/services/camera/src/svc_camera_l1.c` |

**实际执行示例**：

```
uvctest
```
