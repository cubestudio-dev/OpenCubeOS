<!-- SPDX-License-Identifier: Apache-2.0 -->
# USB 点亮手册

## 主控制器

| 栈 | QEMU | 备注 |
|-------|------|-------|
| UHCI | `-device usb-uhci` | 帧列表，TD 回收共享代码 |
| OHCI | `-device usb-ohci` | ED/TD 池，7 位切换表 |
| EHCI | `-device usb-ehci` | 异步 + 周期性调度，IAA 解链 |
| xHCI | `-device qemu-xhci` | 带 LINK 循环的环，暂存区，64-bar 高字 |

## 设备 + 测试

| 类别 | QEMU 设备 | 命令 | 测试 |
|-------|-------------|---------|------|
| MSC 存储设备 | `-device usb-storage` | `usb`, `usbdev` | `usb_storage_test` |
| HID 键盘 | `-device usb-kbd` | (输入队列提供者) | `usb_kbd_test` |
| HID 鼠标 | `-device usb-mouse` | (指针设备) | `usb_mouse_test` |
| 串口 | `-device usb-serial` | `usbdev` | `usb_serial_test` |
| 音频设备 | `-device usb-audio` | `usbaudio` | `usb_audio_test` |
| 集线器 | `-device usb-hub` | `usb` | `usb_hub_test` |

`usb_hotplug_test` 涵盖了连接/断开处理。

## 点亮顺序

1. `usb` 列出控制器 + 设备；枚举深度通过集线器达到
   USB_ENUM_DEPTH。
2. 类驱动绑定（MSC 注册块设备；HID 填充
   输入队列；串口创建端口）。
3. 系列测试命令是门禁（`usb_storage_test` 5/5 等）。

## 注意事项（审计历史）

- xHCI DMA 高于 4 GiB：在探测 + 每次传输时拒绝。
- C_PORT_RESET 等待带超时；集线器速度解码；递归
   枚举边界。
- MSC：在一个繁忙窗口内出现 SENSE（从不发送 sense 的错误），
   每实例名称为 usda/usdb。
- ISO 音频端点零填充尾部（无 OOB 读取）。
