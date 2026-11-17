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
g6_oom
g6_oom: start

*** EXCEPTION 14: #PF Page Fault ***
  error_code=0x0000000000000007  rip=0x00000000004000D4  cs=0x0000000000000033  rflags=0x0000000000010246
  rsp=0x000000003FFFFEE0  ss=0x000000000000003B
  cr2=0x000000003C000000  (faulting address)
*** KILLING user process (pid=28) due to exception 14, kernel continues ***
[user] process killed by exception (vector=14), kernel continues
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
g6_test
g6_test: start
g6_test: T2 fork-mmap-inherit start
S0
S1
S2
SP
SC
g6_test: =PASS=
g6_test: T5 per-proc-cwd start
g6_test: =PASS=
g6_test: T1 exit0-reap start
[user] process 33 exited (code=42)
g6_test: =PASS=
g6_test: T3 write2-no-reader start
g6_test: =PASS=
g6_test: T4 sig-restorer start
g6_test: T4 handler entered
g6_test: T4 back from handler
g6_test: =PASS=
g6_test: T7 solib-reject start
g6_test: =PASS=
g6_test: T6 solib-window start
g6_test: T6 dlopen count = 3734g6_test: =PASS=
g6_test: failed tests = 0g6_test: PASS
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
install
usage: install <device>  (install the running system; DESTROYS existing data)
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
ip
IP: 0.0.0.0
Mask: 0.0.0.0
Gateway: 0.0.0.0
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
camera subsystem: 0 cameras
  no camera bound (UVC class driver active; QEMU has no UVC model - attach real hardware or check `usb`)
ush> 
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
uvctest: lib_mjpeg + camera core + UVC descriptor parse
  mjpeg decode rc: PASS
  mjpeg size 16x16: PASS
  mjpeg pixels all 200: PASS
  mjpeg truncated rejected: PASS
  mjpeg small buffer -> -2: PASS
  yuy2->rgb BT.601: PASS
  camera register: PASS
  camera get: PASS
  set format: PASS
  set format unknown rejected: PASS
  set frame: PASS
  set frame mismatch rejected: PASS
  set interval: PASS
  capture refused before start: PASS
  stream start: PASS
  capture while streaming: PASS
  release frame: PASS
  brightness step snap: PASS
  stream stop: PASS
  camera unregister: PASS
  uvc bind: PASS
  uvc version 1.1: PASS
  uvc 2 formats: PASS
  uvc MJPEG 640x480: PASS
  uvc intervals 666666/1333333: PASS
  uvc YUY2 320x240: PASS
  uvc terminals CT=1 PU=2: PASS
  uvc caps: PASS
  uvc descriptor dump: PASS
  uvc disconnect: PASS
  live cameras: 0 (none bound - no UVC hardware in this machine; the tests above ran the real code paths on spec fixtures)
uvctest: 0 failures - PASS
ush> 
```
