<!-- SPDX-License-Identifier: Apache-2.0 -->
# 输入设备

## PS/2 键盘

主要输入设备：scancode set 1 -> 内核中的 ASCII（`drivers/input/driver_input_keyboard.c`），直接提供给行编辑器。在 QEMU 中始终存在。

## COM1 串口

`screen_serial_in.c` - 串口 RX 输入到相同的键盘队列，因此 `-serial mon:stdio` 可提供完整控制台。

## USB HID

`driver_usb_hid.c` - 通过任何主机控制器的键盘和鼠标；`usb_kbd_test` / `usb_mouse_test` 门禁控制它们。

## I2C HID + 触控板

`driver_input_i2chid.c` + 触控板栈：`i2cdetect` 查找设备，`touchpad`/`touchtest` 测试它（真实硬件；QEMU 无模型）。

## 接口约定

所有输入源都汇聚到 shell/TUI 输入队列 - 添加源意味着输入到相同队列，而非新增输入路径。
