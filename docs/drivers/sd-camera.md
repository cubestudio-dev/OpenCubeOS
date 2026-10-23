<!-- SPDX-License-Identifier: Apache-2.0 -->
# SD and cameras

## SD/SDHCI

`drivers/sd/driver_sdhci.c` - the host controller family; cards
appear as block instances (`sd0`). Board-specific on real hardware.

## Cameras (UVC + MJPEG)

`drivers/camera/driver_camera.c` + `driver_camera_uvc.c` (the USB
Video Class) + `lib_mjpeg.c` (the decoder).

```
oc> uvc        (UVC device status)
oc> uvctest    (frame capture test)
```

Frames decode through the in-tree MJPEG decoder - ISO endpoints
zero-pad tails (no OOB reads), and the capture path is
`usb_camera_test`-gated.
