<!-- SPDX-License-Identifier: Apache-2.0 -->
# 应用字典 - USB (9)

### usb

| 字段 | 值 |
|---|---|
| 名称 | usb |
| 分类 | USB |
| 功能 | USB 主机状态 |
| 用法 | `usb` |
| 来源 | `ocos/services/usb/src/svc_usb_l1.c` |

**实际执行示例**：

```
usb
USB: not present
class drivers: 8 registered | keyboards=0 mice=0 msc=0 serial=0
  msc: none
  serial: none
  mouse: none
USB Audio: not present
ush> 
```

### usb_core_test

| 字段 | 值 |
|---|---|
| 名称 | usb_core_test |
| 分类 | USB |
| 功能 | USB 核心测试 |
| 用法 | `usb_core_test` |
| 来源 | `ocos/services/usb/src/svc_usb_l1.c` |

**实际执行示例**：

```
usb_core_test
[usb_core_test] input: USB core invariants + live transfers
[usb_core_test] usb host controllers registered => FAIL (none)
[usb_core_test] device table consistent (0 devices) => PASS
[usb_core_test] GET_STATUS live on every device => FAIL (no devices attached)
[usb_core_test] class drivers registered (8) => PASS
[usb_core_test] re-enumeration idempotent => PASS
[usb_core_test] summary FAIL
ush> 
```

### usb_hotplug_test

| 字段 | 值 |
|---|---|
| 名称 | usb_hotplug_test |
| 分类 | USB |
| 功能 | USB 热插拔测试 |
| 用法 | `usb_hotplug_test [...]` |
| 来源 | `ocos/services/usb/src/svc_usb_l1.c` |

**实际执行示例**：

```
usb_hotplug_test
[usb_hotplug_test] poll + enumerate round done (new devices: 0) => PASS
  (no devices)
ush> 
```

### usb_hub_test

| 字段 | 值 |
|---|---|
| 名称 | usb_hub_test |
| 分类 | USB |
| 功能 | USB 集线器测试 |
| 用法 | `usb_hub_test` |
| 来源 | `ocos/services/usb/src/svc_usb_l1.c` |

**实际执行示例**：

```
usb_hub_test
[usb_hub_test] external hub attached => FAIL (no hub device)
ush> 
```

### usb_kbd_test

| 字段 | 值 |
|---|---|
| 名称 | usb_kbd_test |
| 分类 | USB |
| 功能 | USB 键盘测试 |
| 用法 | `usb_kbd_test` |
| 来源 | `ocos/services/usb/src/svc_usb_l1.c` |

**实际执行示例**：

```
usb_kbd_test
[usb_kbd_test] HID keyboard attached => FAIL (none enumerated)
ush> 
```

### usb_mouse_test

| 字段 | 值 |
|---|---|
| 名称 | usb_mouse_test |
| 分类 | USB |
| 功能 | USB 鼠标测试 |
| 用法 | `usb_mouse_test` |
| 来源 | `ocos/services/usb/src/svc_usb_l1.c` |

**实际执行示例**：

```
usb_mouse_test
[usb_mouse_test] HID mouse attached => FAIL (none enumerated)
ush> 
```

### usb_serial_test

| 字段 | 值 |
|---|---|
| 名称 | usb_serial_test |
| 分类 | USB |
| 功能 | USB 串口测试 |
| 用法 | `usb_serial_test` |
| 来源 | `ocos/services/usb/src/svc_usb_l1.c` |

**实际执行示例**：

```
usb_serial_test
[usb_serial_test] USB serial port attached => FAIL (no CDC-ACM/FTDI device enumerated)
ush> 
```

### usb_storage_test

| 字段 | 值 |
|---|---|
| 名称 | usb_storage_test |
| 分类 | USB |
| 功能 | USB 大容量存储测试 |
| 用法 | `usb_storage_test` |
| 来源 | `ocos/services/usb/src/svc_usb_l1.c` |

**实际执行示例**：

```
usb_storage_test
[usb_storage_test] USB storage drive attached => FAIL (no MSC device enumerated)
ush> 
```

### usbdev

| 字段 | 值 |
|---|---|
| 名称 | usbdev |
| 分类 | USB |
| 功能 | USB 设备详情 |
| 用法 | `usbdev [slot]` |
| 来源 | `ocos/services/usb/src/svc_usb_l1.c` |

**实际执行示例**：

```
usbdev
usbdev: no devices
ush> 
```
