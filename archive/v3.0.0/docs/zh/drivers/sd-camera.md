<!-- SPDX-License-Identifier: Apache-2.0 -->
# SD 卡与摄像头

## SD/SDHCI

`drivers/sd/driver_sdhci.c` - 主控制器系列；卡以块实例（`sd0`）形式出现。在实际硬件上，此部分为板卡特定实现。

## 摄像头（UVC + MJPEG）

`drivers/camera/driver_camera.c` + `driver_camera_uvc.c`（USB 视频类）+ `lib_mjpeg.c`（解码器）。

```
oc> uvc        (UVC device status)
oc> uvctest    (frame capture test)
```

帧通过树内 MJPEG 解码器解码 - ISO 末端填充零填充（无 OOB 读取），且捕获路径由 `usb_camera_test` 控制。
