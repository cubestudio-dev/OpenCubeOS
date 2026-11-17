<!-- SPDX-License-Identifier: Apache-2.0 -->
# Application dictionary - Cameras (10)

### gameport

| Field | Value |
|---|---|
| Name | gameport |
| Category | Cameras |
| Function | game port status |
| Usage | `gameport` |
| Source | `ocos/services/camera/src/svc_camera_l1.c` |

**Executed example**:

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

| Field | Value |
|---|---|
| Name | gameporttest |
| Category | Cameras |
| Function | game port test |
| Usage | `gameporttest` |
| Source | `ocos/services/camera/src/svc_camera_l1.c` |

**Executed example**:

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

| Field | Value |
|---|---|
| Name | ir |
| Category | Cameras |
| Function | infrared receiver status |
| Usage | `ir` |
| Source | `ocos/services/camera/src/svc_camera_l1.c` |

**Executed example**:

```
ir
ir devices: 0
  no IR device present (QEMU models neither CIR hardware nor an IrDA UART; the SIR probe runs against the secondary 16550 at boot)
ush> 
```

### irtest

| Field | Value |
|---|---|
| Name | irtest |
| Category | Cameras |
| Function | IR receiver test |
| Usage | `irtest` |
| Source | `ocos/services/camera/src/svc_camera_l1.c` |

**Executed example**:

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

| Field | Value |
|---|---|
| Name | parallel |
| Category | Cameras |
| Function | parallel port status |
| Usage | `parallel` |
| Source | `ocos/services/camera/src/svc_camera_l1.c` |

**Executed example**:

```
parallel
parallel ports: 1
  lpt1 io=0x378 irq=7 mode=SPP ecp=no bidir=no tx=0 rx=0 timeouts=0 status=0xd9
ush> 
```

### paralleltest

| Field | Value |
|---|---|
| Name | paralleltest |
| Category | Cameras |
| Function | parallel port test |
| Usage | `paralleltest` |
| Source | `ocos/services/camera/src/svc_camera_l1.c` |

**Executed example**:

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

| Field | Value |
|---|---|
| Name | scsi |
| Category | Cameras |
| Function | SCSI device scan |
| Usage | `scsi` |
| Source | `ocos/services/camera/src/svc_camera_l1.c` |

**Executed example**:

```
scsi
scsi HBAs: 0
scsi devices: 0
  (no SCSI targets present; QEMU models one via -device lsi)
ush> 
```

### scsitest

| Field | Value |
|---|---|
| Name | scsitest |
| Category | Cameras |
| Function | SCSI device test |
| Usage | `scsitest` |
| Source | `ocos/services/camera/src/svc_camera_l1.c` |

**Executed example**:

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

| Field | Value |
|---|---|
| Name | uvc |
| Category | Cameras |
| Function | UVC camera status |
| Usage | `uvc` |
| Source | `ocos/services/camera/src/svc_camera_l1.c` |

**Executed example**:

```
uvc
camera subsystem: 0 cameras
  no camera bound (UVC class driver active; QEMU has no UVC model - attach real hardware or check `usb`)
ush> 
```

### uvctest

| Field | Value |
|---|---|
| Name | uvctest |
| Category | Cameras |
| Function | UVC camera test |
| Usage | `uvctest` |
| Source | `ocos/services/camera/src/svc_camera_l1.c` |

**Executed example**:

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
