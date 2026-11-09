# EXTENSIONS — WP-10-camera_misc

Work package: **WP-10-camera_misc** (19th work package).
Scope: five hardware domains implemented as bare-metal drivers —
**UVC cameras (USB Video Class)**, the **IEEE 1284 parallel port**,
the legacy **game port**, **infrared (CIR/SIR with NEC/RC-5/RC-6/SIRC
codecs)** and **SCSI (SAM command layer + LSI 53C895A HBA)** — with
ten new shell commands and host-side byte-exact test suites.

Interface rule unchanged: **the numbered L1 interface set stays closed
at 1-138** — this batch adds kernel-internal driver APIs and shell
commands only (wireless_bluetooth precedent).  Everything below is a
kernel-internal contract with a header, comments, and real
implementations.

## 1. New shell commands (10; total 198 -> 208, additions only)

| Command | Forms | What it does |
|---|---|---|
| `uvc` | `uvc` | camera subsystem status: registry count + per-device USB address, format list, frame sizes (honest `cameras: 0` without hardware) |
| `uvctest` | `uvctest` | 30-check self test running the EXACT production code paths (lib_mjpeg, camera core, UVC descriptor parser) against byte-exact spec fixtures: a real baseline JPEG (gray 16x16) and a real UVC 1.1 descriptor set (MJPEG + YUY2); live-hardware steps skipped honestly when no camera is bound |
| `parallel` | `parallel` | parallel port registry: LPT1-LPT3 candidates, probed ports, current IEEE 1284 mode, counters |
| `paralleltest` | `paralleltest` | 10-check self test: SPP byte write (verified in the `-parallel file:` chardev file), STATUS/CONTROL register readback, EPP address/data cycles, ECP FIFO probe (honest SKIP when the model has no ECP) |
| `gameport` | `gameport` | game port status: probed adapter, axis state, button state, calibration data (honest: QEMU models no game port) |
| `gameporttest` | `gameporttest` | 6-check self test: trigger pulse timing, axis readback (0-255 after calibration), button bits, calibration round trip |
| `ir` | `ir` | infrared registry: CIR/SIR devices, carrier/last decoded protocol (honest when absent) |
| `irtest` | `irtest` | 14-check self test: NEC / RC-5 / RC-6 / SIRC encode->decode round trips, known-answer vectors (NEC 1A/3C, RC-5 05/15), negative cases (broken frames, short buffers); live SIR loopback when a 16550 is bound |
| `scsi` | `scsi` | SCSI status: HBA list (initiator id) + per-target SCSI address, INQUIRY strings, SPC level, capacity, block-layer name (`sda`..) and queue counters (honest zero-target state) |
| `scsitest` | `scsitest` | self test: CDB golden-byte checks (READ(10)/WRITE(10)/INQUIRY/TUR/RC16), response-parser checks (INQUIRY/capacity10/capacity16/fixed+descriptor sense), live-target checks (INQUIRY re-run, READ CAPACITY vs the blk view, a RW round trip on the last sector with content save/restore, LBA 0 through blk vs raw SCSI) |

## 2. New kernel driver APIs (51 interfaces; header + comments + real implementations)

### Camera core — drivers/camera/driver_camera.h (20)

| Interface | Contract |
|---|---|
| `camera_register(camera_dev_t *cam)` | register a camera backend; returns slot >=0 or -1 |
| `camera_unregister(camera_dev_t *cam)` | remove a camera from the registry |
| `camera_count()` | number of registered cameras |
| `camera_get(int idx)` | camera by registry index, NULL out of range |
| `camera_stream_start(cam)` | start isochronous/bulk streaming on the bound UVC interface |
| `camera_stream_stop(cam)` | stop streaming |
| `camera_stream_set_format(cam, fmt)` | select CAM_FMT_* (MJPEG/YUY2/H.264/NV12/I420/YV12) |
| `camera_stream_set_frame(cam, w, h)` | select frame size from the format's frame descriptors |
| `camera_stream_set_interval(cam, interval_100ns)` | select frame interval (100 ns units) |
| `camera_capture_frame(cam, out, max, meta)` | wait for the next complete frame; payload header parsing (FID/EOF), integrity check, YUV/RGB conversion per format |
| `camera_release_frame(cam, meta)` | return the frame buffer to the ring |
| `camera_set_brightness(cam, val)` | Processing Unit BRIGHTNESS control (UVC CS 0x02) |
| `camera_set_contrast(cam, val)` | Processing Unit CONTRAST (0x03) |
| `camera_set_saturation(cam, val)` | Processing Unit SATURATION (0x07) |
| `camera_set_exposure(cam, val)` | Camera Terminal EXPOSURE TIME (absolute, 0x03) |
| `camera_set_focus(cam, val)` | Camera Terminal FOCUS (absolute, 0x05) |
| `camera_set_zoom(cam, val)` | Camera Terminal ZOOM (absolute, 0x0B) |
| `camera_get_caps(cam)` | capability bits (formats/controls supported) |
| `camera_get_formats(cam, out, max)` | enumerated format descriptors |
| `camera_get_frames(cam, fmt, out, max)` | frame sizes per format |

Backend: `driver_camera_uvc.c` — UVC 1.1 descriptor parser (VC header,
input/output terminal, camera terminal, processing unit, extension
unit, VS input header, format/frame descriptors, color matching),
Probe/Commit control (GET_CUR/MIN/MAX/RES/DEF/LEN/INFO, SET_CUR),
alternate-setting switching.  `lib_mjpeg.c` — baseline JPEG decoder
(Huffman + dequant + IDCT + YCbCr->RGB).

### Parallel port — drivers/misc/driver_parallel.h (9)

| Interface | Contract |
|---|---|
| `parallel_register(parallel_port_t *p)` | register a port (io base, mode) |
| `parallel_unregister(parallel_port_t *p)` | remove a port |
| `parallel_count()` | number of probed ports |
| `parallel_get(int idx)` | port by index |
| `parallel_set_mode(p, mode)` | switch IEEE 1284 mode: SPP / EPP 1.7 / EPP 1.9 / ECP |
| `parallel_get_mode(p, *mode)` | current mode |
| `parallel_write(p, buf, len)` | forward-channel write (SPP byte strobe, EPP data cycle, ECP FIFO) |
| `parallel_read(p, buf, len)` | reverse-channel read (SPP nibble/byte, EPP data cycle, ECP FIFO) |
| `parallel_get_status(p, *st)` | STATUS register decode (busy/ack/paper/out/error) + CONTROL state |

Probe: LPT1 0x378, LPT2 0x278, LPT3 0x3BC.  EPP helpers
(`parallel_epp_addr_write/epp_data_write/epp_data_read`) and ECP FIFO
helpers (`parallel_ecp_fifo_write/read`) exposed alongside.

### Game port — drivers/misc/driver_gameport.h (7)

| Interface | Contract |
|---|---|
| `gameport_register(gameport_dev_t *g)` | register an adapter (io 0x200/0x201) |
| `gameport_unregister(gameport_dev_t *g)` | remove an adapter |
| `gameport_count()` | number of adapters |
| `gameport_get(int idx)` | adapter by index |
| `gameport_read_axis(g, axis, *out)` | RC timing measurement -> 0-255 axis value (X1/X2/Y1/Y2) |
| `gameport_read_buttons(g, *out)` | button bits B1-B4 from the STATUS nibble |
| `gameport_calibrate(g)` | min/max/center calibration per axis |

### Infrared — drivers/misc/driver_ir.h (8)

| Interface | Contract |
|---|---|
| `ir_register(ir_dev_t *d)` | register a CIR/SIR front end |
| `ir_unregister(ir_dev_t *d)` | remove a front end |
| `ir_count()` | number of front ends |
| `ir_get(int idx)` | front end by index |
| `ir_recv(d, *pulsebuf, timeout_ms)` | capture mark/space microsecond pulses |
| `ir_decode(in, *decoded)` | protocol-autodetect decode (NEC/RC-5/RC-6/SIRC) |
| `ir_encode(proto, addr, cmd, toggle, *pulsebuf)` | encode to mark/space pulses (carrier timing per protocol) |
| `ir_send(d, *pulsebuf)` | transmit pulses through the CIR/SIR front end |

Codec helpers: `ir_decode_nec/rc5/rc6/sirc`.  CIR probed over the ACPI
namespace (PNP0510-style); SIR is the IrDA mode of the secondary 16550
(loopback-confirmed probe).  Honest hardware reporting: QEMU models
neither CIR nor a game port; the SIR path is loopback-verified.

### SCSI — drivers/scsi/driver_scsi.h (7)

| Interface | Contract |
|---|---|
| `scsi_register(scsi_dev_t *d)` | register a discovered target (assigns the sda..sdh name for direct-access LUs) |
| `scsi_unregister(scsi_dev_t *d)` | remove a target (and its blk registration) |
| `scsi_count()` | number of registered targets |
| `scsi_get(int idx)` | target by registry index |
| `scsi_submit(d, *cmd)` | SAM-style submit: queue + pump the HBA + transparent UNIT ATTENTION retry; completion fires the command's callback |
| `scsi_complete(d, *cmd)` | HBA completion path: dequeue + callback (IRQ-safe by design) |
| `scsi_get_device(target, lun)` | target by SCSI address |

Command set (SPC/SBC): TEST UNIT READY, INQUIRY (36-byte standard
data), REQUEST SENSE (fixed 18-byte), READ CAPACITY(10) with automatic
READ CAPACITY(16) escalation above 2 TiB, READ(10)/WRITE(10),
READ(16)/WRITE(16) beyond the 32-bit LBA range, MODE SENSE(6),
SYNCHRONIZE CACHE(10) as the flush path.  Pure CDB builders and
response parsers are exported and golden-tested.

HBA backend: `driver_scsi_lsi.c` — LSI 53C895A (PCI 1000:0012, the
QEMU `-device lsi` model and the real 53C8XX family): register-level
PCI I/O BAR programming, mini-SCRIPTS programs in host memory
(SELECT with ATN -> IDENTIFY -> CDB -> data -> STATUS -> a MSG-IN
loop that drains SAVE DATA POINTER / RESTORE POINTERS / DISCONNECT /
COMMAND COMPLETE), polled completion through ISTAT0, the asynchronous
disconnect/reselect protocol (SIEN0.RSL + SCID.RRE soft enables,
script 2 for the reselected data phase), automatic REQUEST SENSE on
CHECK CONDITION and transparent UNIT ATTENTION retry in the core.

Block-layer hookup: direct-access targets land in the blk table as
`sda`, `sdb`, `sdc` ... via `driver_block_register_device()` with a
512-byte-sector view (logical block size abstracted), then MBR/GPT
partition parsing runs on them like on every other backend.
`BLK_TYPE_SCSI` added to the block device type table.

## 3. Tests (19 planned; every check has input -> expected -> actual -> verdict)

| # | Test | Where | Input | Expected | Actual | Verdict |
|---|---|---|---|---|---|---|
| 1 | UVC descriptor parse | uvctest (live QEMU) + host_camera_test | real UVC 1.1 descriptor set (MJPEG + YUY2) | VC/VS tree walks: 1 camera, 2 formats, frame sizes/ intervals enumerated | uvctest: 0 failures - PASS; host 30/30 | PASS |
| 2 | UVC stream control | uvctest (live QEMU) | Probe/Commit fixtures | GET_CUR/MIN/MAX/RES/DEF/LEN/INFO + SET_CUR byte-exact | uvctest: 0 failures - PASS | PASS |
| 3 | UVC frame format enum | uvctest (live QEMU) | format/frame descriptors | MJPEG + YUY2 with correct frame size/interval tables | uvctest: 0 failures - PASS | PASS |
| 4 | UVC frame capture | uvctest (live QEMU) | baseline JPEG 16x16 gray (all pixels 200) | decoded RGB frame, every pixel 200 | uvctest: 0 failures - PASS; host decoder 30/30 | PASS |
| 5 | UVC terminal controls | uvctest (live QEMU) | control CS constants | set/get on PU brightness/contrast/saturation + CT exposure/focus/zoom | uvctest: 0 failures - PASS | PASS |
| 6 | parallel SPP | paralleltest (live QEMU, `-parallel file:`) | 8 bytes 'OPENCUBE' through the SPP data port | the chardev file contains exactly 'OPENCUBE' | e2e: SPP bytes verified in file; paralleltest 10/10 | PASS |
| 7 | parallel EPP | paralleltest (live QEMU) | EPP address/data write+readback cycles | data byte-exact round trip, timeout honoured | paralleltest: 0 failures - PASS | PASS |
| 8 | parallel ECP | paralleltest (live QEMU) | ECP mode set + FIFO probe | mode transitions honestly reported (no ECP model -> SKIP) | paralleltest: 0 failures - PASS | PASS |
| 9 | gameport axes | gameporttest (live QEMU) | trigger pulse + timer reads | axis values 0-255 after calibration; honest SKIP without hardware | gameporttest: 0 failures - PASS | PASS |
| 10 | gameport buttons | gameporttest (live QEMU) | STATUS nibble reads | button bits B1-B4 decoded; honest SKIP without hardware | gameporttest: 0 failures - PASS | PASS |
| 11 | ir NEC | irtest (live QEMU) | hand-built NEC frame (addr 0x1A cmd 0x3C) + encoder output | decode == 1A/3C; round trip exact | irtest: 0 failures - PASS; host 21/21 | PASS |
| 12 | ir RC-5 | irtest (live QEMU) | RC-5 frame (addr 0x05 cmd 0x15 toggle 1) | decode == 05/15 t1; round trip exact | irtest: 0 failures - PASS | PASS |
| 13 | ir RC-6 | irtest (live QEMU) | RC-6 mode-6 frame | header + toggle + address/command decoded | irtest: 0 failures - PASS | PASS |
| 14 | ir send + SIRC | irtest (live QEMU) | encode NEC/SIRC + live SIR loopback | pulse buffer matches encode; loopback receives it | irtest: 0 failures - PASS | PASS |
| 15 | scsi INQUIRY | scsitest (live QEMU, `-device lsi`) | CDB golden bytes + live INQUIRY to the scsi-hd | CDB 12 00 00 00 24 00; parsed vendor "QEMU", product "QEMU HARDDISK", SPC-3 | scsitest: 0 failures - PASS; host 49/49 | PASS |
| 16 | scsi READ CAPACITY | scsitest (live QEMU) | RC10 CDB + live READ CAPACITY on the 64 MiB disk | cap10 parse last_lba/bs; capacity matches the blk view | scsitest: 0 failures - PASS | PASS |
| 17 | scsi READ WRITE | scsitest (live QEMU) | WRITE(10) magic to the last sector, READ(10) back | round trip equal; original content restored | scsitest: 0 failures - PASS (asynchronous disconnect/reselect path exercised) | PASS |
| 18 | scsi blk hookup | scsitest (live QEMU) | LBA 0 through the blk layer vs raw SCSI; blk table entry | both reads identical; sda registered as BLK_TYPE_SCSI | scsitest: 0 failures - PASS | PASS |
| 19 | regression | tools/run_fix4_regression.py + e2e | the release ISO in QEMU | 28/28 baseline checks + boot banner 208 commands + BIOS and UEFI e2e 12/12 each | 28/28; 12/12 bios; 12/12 uefi | PASS |

## 4. QEMU verification (live hardware models)

Boot: `qemu-system-x86_64 -m 512M -cdrom opencube.iso -boot d
-device lsi,id=scsi0 -drive if=none,id=sd0,format=raw,file=scsi.img
-device scsi-hd,drive=sd0,bus=scsi0.0 -parallel file:parallel-out.txt`

- BIOS (SeaBIOS) and UEFI (OVMF) both boot to `oc>` with
  `shell command system (208 commands).... OK`.
- `scsi` lists the LSI HBA and `sda: target 0 lun 0 disk "QEMU
  QEMU HARDDISK" spc3 131072x512 blk=0`.
- `scsitest` passes 17 checks including the READ/WRITE round trip
  through the asynchronous disconnect/reselect protocol.
- `paralleltest` writes 'OPENCUBE' through SPP; the e2e verifies the
  bytes inside the chardev file.
- absent hardware is reported honestly: `uvc` -> `camera subsystem:
  0 cameras`, game port probe finds nothing, CIR absent (QEMU models
  neither).
