<!-- SPDX-License-Identifier: Apache-2.0 -->
# 应用字典 - 输入 (2)

### touchpad

| 字段 | 值 |
|---|---|
| 名称 | touchpad |
| 分类 | 输入 |
| 功能 | 触摸板状态 |
| 用法 | `touchpad` |
| 来源 | `ocos/services/display/src/svc_display_input_l1.c` |

**实际执行示例**：

```
touchpad
touchpad: no i2c-hid device bound
  (QEMU ships no I2C-HID model; pipeline covered by touchtest)
reports decoded: 0  events queued total: 0
ush> 
```

### touchtest

| 字段 | 值 |
|---|---|
| 名称 | touchtest |
| 分类 | 输入 |
| 功能 | 触摸板测试 |
| 用法 | `touchtest` |
| 来源 | `ocos/services/display/src/svc_display_input_l1.c` |

**实际执行示例**：

```
touchtest
touchtest: descriptor parse + TAP/SCROLL/PINCH/SWIPE  PASS
ush> 
```
