<!-- SPDX-License-Identifier: Apache-2.0 -->
# Application dictionary - Input (2)

### touchpad

| Field | Value |
|---|---|
| Name | touchpad |
| Category | Input |
| Function | touchpad status |
| Usage | `touchpad` |
| Source | `ocos/services/display/src/svc_display_input_l1.c` |

**Executed example**:

```
touchpad
touchpad: no i2c-hid device bound
  (QEMU ships no I2C-HID model; pipeline covered by touchtest)
reports decoded: 0  events queued total: 0
ush> 
```

### touchtest

| Field | Value |
|---|---|
| Name | touchtest |
| Category | Input |
| Function | touchpad test |
| Usage | `touchtest` |
| Source | `ocos/services/display/src/svc_display_input_l1.c` |

**Executed example**:

```
touchtest
touchtest: descriptor parse + TAP/SCROLL/PINCH/SWIPE  PASS
ush> 
```
