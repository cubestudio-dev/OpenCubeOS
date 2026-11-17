<!-- SPDX-License-Identifier: Apache-2.0 -->
# 应用字典 - 网卡 (20)

### bcm57xx

| 字段 | 值 |
|---|---|
| 名称 | bcm57xx |
| 分类 | 网卡 |
| 功能 | BCM57xx 网卡状态 |
| 用法 | `bcm57xx` |
| 来源 | `ocos/services/net/src/svc_net_nic_l1.c` |

**实际执行示例**：

```
bcm57xx
bcm57xx: not present
ush> 
```

### bcm57xx_test

| 字段 | 值 |
|---|---|
| 名称 | bcm57xx_test |
| 分类 | 网卡 |
| 功能 | BCM57xx 驱动测试 |
| 用法 | `bcm57xx_test` |
| 来源 | `ocos/services/net/src/svc_net_nic_l1.c` |

**实际执行示例**：

```
bcm57xx_test
[bcm57xx_test] device not present in this environment => SKIPPED (needs real hardware; no fake
    output is produced for absent devices)
ush> 
```

### bt

| 字段 | 值 |
|---|---|
| 名称 | bt |
| 分类 | 网卡 |
| 功能 | 蓝牙状态 |
| 用法 | `bt [status|usb|test]` |
| 来源 | `ocos/services/net/src/svc_net_nic_l1.c` |

**实际执行示例**：

```
bt
bluetooth: transport=none controller=not-present conns=0 l2cap=0
ush> 
```

### e1000e

| 字段 | 值 |
|---|---|
| 名称 | e1000e |
| 分类 | 网卡 |
| 功能 | e1000e 网卡状态 |
| 用法 | `e1000e` |
| 来源 | `ocos/services/net/src/svc_net_nic_l1.c` |

**实际执行示例**：

```
e1000e
```

### e1000e_test

| 字段 | 值 |
|---|---|
| 名称 | e1000e_test |
| 分类 | 网卡 |
| 功能 | e1000e 驱动测试 |
| 用法 | `e1000e_test` |
| 来源 | `ocos/services/net/src/svc_net_nic_l1.c` |

**实际执行示例**：

```
e1000e_test
```

### igb

| 字段 | 值 |
|---|---|
| 名称 | igb |
| 分类 | 网卡 |
| 功能 | igb 网卡状态 |
| 用法 | `igb` |
| 来源 | `ocos/services/net/src/svc_net_nic_l1.c` |

**实际执行示例**：

```
igb
igb: not present
ush> 
```

### igb_test

| 字段 | 值 |
|---|---|
| 名称 | igb_test |
| 分类 | 网卡 |
| 功能 | igb 驱动测试 |
| 用法 | `igb_test` |
| 来源 | `ocos/services/net/src/svc_net_nic_l1.c` |

**实际执行示例**：

```
igb_test
[igb_test] device not present in this environment => SKIPPED (needs real hardware; no fake
    output is produced for absent devices)
ush> 
```

### ixgbe

| 字段 | 值 |
|---|---|
| 名称 | ixgbe |
| 分类 | 网卡 |
| 功能 | ixgbe 网卡状态 |
| 用法 | `ixgbe` |
| 来源 | `ocos/services/net/src/svc_net_nic_l1.c` |

**实际执行示例**：

```
ixgbe
ixgbe: not present
ush> 
```

### ixgbe_test

| 字段 | 值 |
|---|---|
| 名称 | ixgbe_test |
| 分类 | 网卡 |
| 功能 | ixgbe 驱动测试 |
| 用法 | `ixgbe_test` |
| 来源 | `ocos/services/net/src/svc_net_nic_l1.c` |

**实际执行示例**：

```
ixgbe_test
[ixgbe_test] device not present in this environment => SKIPPED (needs real hardware; no fake
    output is produced for absent devices)
ush> 
```

### nic_rw_test

| 字段 | 值 |
|---|---|
| 名称 | nic_rw_test |
| 分类 | 网卡 |
| 功能 | 网卡回环测试 |
| 用法 | `nic_rw_test` |
| 来源 | `ocos/services/net/src/svc_net_nic_l1.c` |

**实际执行示例**：

```
nic_rw_test
[nic_rw_test] input: 0 registered NIC(s)
[nic_rw_test] no NICs registered => SKIP
ush> 
```

### other_nic_test

| 字段 | 值 |
|---|---|
| 名称 | other_nic_test |
| 分类 | 网卡 |
| 功能 | 其他网卡驱动测试 |
| 用法 | `other_nic_test` |
| 来源 | `ocos/services/net/src/svc_net_nic_l1.c` |

**实际执行示例**：

```
other_nic_test
[other_nic_test] no legacy NIC present in this environment => SKIPPED
ush> 
```

### rtl810x

| 字段 | 值 |
|---|---|
| 名称 | rtl810x |
| 分类 | 网卡 |
| 功能 | RTL810x 网卡状态 |
| 用法 | `rtl810x` |
| 来源 | `ocos/services/net/src/svc_net_nic_l1.c` |

**实际执行示例**：

```
rtl810x
rtl810x: not present
ush> 
```

### rtl810x_test

| 字段 | 值 |
|---|---|
| 名称 | rtl810x_test |
| 分类 | 网卡 |
| 功能 | RTL810x 驱动测试 |
| 用法 | `rtl810x_test` |
| 来源 | `ocos/services/net/src/svc_net_nic_l1.c` |

**实际执行示例**：

```
rtl810x_test
[rtl810x_test] device not present in this environment => SKIPPED (needs real hardware; no fake
    output is produced for absent devices)
ush> 
```

### rtl8125

| 字段 | 值 |
|---|---|
| 名称 | rtl8125 |
| 分类 | 网卡 |
| 功能 | RTL8125 网卡状态 |
| 用法 | `rtl8125` |
| 来源 | `ocos/services/net/src/svc_net_nic_l1.c` |

**实际执行示例**：

```
rtl8125
rtl8125: not present
ush> 
```

### rtl8125_test

| 字段 | 值 |
|---|---|
| 名称 | rtl8125_test |
| 分类 | 网卡 |
| 功能 | RTL8125 驱动测试 |
| 用法 | `rtl8125_test` |
| 来源 | `ocos/services/net/src/svc_net_nic_l1.c` |

**实际执行示例**：

```
rtl8125_test
[rtl8125_test] device not present in this environment => SKIPPED (needs real hardware; no fake
    output is produced for absent devices)
ush> 
```

### rtl8139

| 字段 | 值 |
|---|---|
| 名称 | rtl8139 |
| 分类 | 网卡 |
| 功能 | RTL8139 网卡状态 |
| 用法 | `rtl8139` |
| 来源 | `ocos/services/net/src/svc_net_nic_l1.c` |

**实际执行示例**：

```
rtl8139
rtl8139: not present
ush> 
```

### rtl8139_test

| 字段 | 值 |
|---|---|
| 名称 | rtl8139_test |
| 分类 | 网卡 |
| 功能 | RTL8139 驱动测试 |
| 用法 | `rtl8139_test` |
| 来源 | `ocos/services/net/src/svc_net_nic_l1.c` |

**实际执行示例**：

```
rtl8139_test
[rtl8139_test] device not present in this environment => SKIPPED (needs real hardware; no fake
    output is produced for absent devices)
ush> 
```

### rtl8168

| 字段 | 值 |
|---|---|
| 名称 | rtl8168 |
| 分类 | 网卡 |
| 功能 | RTL8168 网卡状态 |
| 用法 | `rtl8168` |
| 来源 | `ocos/services/net/src/svc_net_nic_l1.c` |

**实际执行示例**：

```
rtl8168
rtl8168: not present
ush> 
```

### rtl8168_test

| 字段 | 值 |
|---|---|
| 名称 | rtl8168_test |
| 分类 | 网卡 |
| 功能 | RTL8168 驱动测试 |
| 用法 | `rtl8168_test` |
| 来源 | `ocos/services/net/src/svc_net_nic_l1.c` |

**实际执行示例**：

```
rtl8168_test
[rtl8168_test] device not present in this environment => SKIPPED (needs real hardware; no fake
    output is produced for absent devices)
ush> 
```

### wifi

| 字段 | 值 |
|---|---|
| 名称 | wifi |
| 分类 | 网卡 |
| 功能 | 无线状态/扫描 |
| 用法 | `wifi [status|scan|...]` |
| 来源 | `ocos/services/net/src/svc_net_nic_l1.c` |

**实际执行示例**：

```
wifi
wireless families: 7  devices: 7
  ath11k
  ath10k
  ath9k
  brcmfmac
  b43
  iwlwifi
  rtlwifi
  dev0 ath11k bus=auto vid=0000 did=0000 state=down
  dev1 ath10k bus=auto vid=0000 did=0000 state=down
  dev2 ath9k bus=auto vid=0000 did=0000 state=down
  dev3 brcmfmac bus=auto vid=0000 did=0000 state=down
  dev4 b43 bus=auto vid=0000 did=0000 state=down
  dev5 iwlwifi bus=auto vid=0000 did=0000 state=down
  dev6 rtlwifi bus=auto vid=0000 did=0000 state=down
(QEMU has no 802.11 models; down = device not present)
ush> 
```
