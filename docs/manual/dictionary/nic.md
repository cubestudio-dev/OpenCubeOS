<!-- SPDX-License-Identifier: Apache-2.0 -->
# Application dictionary - Network cards (20)

### bcm57xx

| Field | Value |
|---|---|
| Name | bcm57xx |
| Category | Network cards |
| Function | BCM57xx NIC status |
| Usage | `bcm57xx` |
| Source | `ocos/services/net/src/svc_net_nic_l1.c` |

**Executed example**:

```
bcm57xx
bcm57xx: not present
ush> 
```

### bcm57xx_test

| Field | Value |
|---|---|
| Name | bcm57xx_test |
| Category | Network cards |
| Function | BCM57xx driver test |
| Usage | `bcm57xx_test` |
| Source | `ocos/services/net/src/svc_net_nic_l1.c` |

**Executed example**:

```
bcm57xx_test
[bcm57xx_test] device not present in this environment => SKIPPED (needs real hardware; no fake
    output is produced for absent devices)
ush> 
```

### bt

| Field | Value |
|---|---|
| Name | bt |
| Category | Network cards |
| Function | bluetooth status |
| Usage | `bt [status\|usb\|test]` |
| Source | `ocos/services/net/src/svc_net_nic_l1.c` |

**Executed example**:

```
bt
bluetooth: transport=none controller=not-present conns=0 l2cap=0
ush> 
```

### e1000e

| Field | Value |
|---|---|
| Name | e1000e |
| Category | Network cards |
| Function | e1000e NIC status |
| Usage | `e1000e` |
| Source | `ocos/services/net/src/svc_net_nic_l1.c` |

**Executed example**:

```
e1000e
e1000e: not present
ush> 
```

### e1000e_test

| Field | Value |
|---|---|
| Name | e1000e_test |
| Category | Network cards |
| Function | e1000e driver test |
| Usage | `e1000e_test` |
| Source | `ocos/services/net/src/svc_net_nic_l1.c` |

**Executed example**:

```
e1000e_test
[e1000e_test] device not present in this environment => SKIPPED (needs real hardware; no fake
    output is produced for absent devices)
ush> 
```

### igb

| Field | Value |
|---|---|
| Name | igb |
| Category | Network cards |
| Function | igb NIC status |
| Usage | `igb` |
| Source | `ocos/services/net/src/svc_net_nic_l1.c` |

**Executed example**:

```
i2cset
usage: i2cset <bus> <addr> <reg> <val>  (hex values)
ush> 
```

### igb_test

| Field | Value |
|---|---|
| Name | igb_test |
| Category | Network cards |
| Function | igb driver test |
| Usage | `igb_test` |
| Source | `ocos/services/net/src/svc_net_nic_l1.c` |

**Executed example**:

```
i2ctest
I2C core: 1 bus(es) registered
controller: PIIX4 (8086:7113) base 0x0700 xfers=80 errs=202
  diag: cfg0x90=0x00000701 sts=0x00
DesignWare: not present on this platform (honest skip)
10-bit addressing: supported on DesignWare bus (none present)
i2ctest: PASS
ush> 
```

### ixgbe

| Field | Value |
|---|---|
| Name | ixgbe |
| Category | Network cards |
| Function | ixgbe NIC status |
| Usage | `ixgbe` |
| Source | `ocos/services/net/src/svc_net_nic_l1.c` |

**Executed example**:

```
ir
ir devices: 0
  no IR device present (QEMU models neither CIR hardware nor an IrDA UART; the SIR probe runs against the secondary 16550 at boot)
ush> 
```

### ixgbe_test

| Field | Value |
|---|---|
| Name | ixgbe_test |
| Category | Network cards |
| Function | ixgbe driver test |
| Usage | `ixgbe_test` |
| Source | `ocos/services/net/src/svc_net_nic_l1.c` |

**Executed example**:

```
irqabitest
IRQ-entry ABI test (BUG-0137 align / BUG-0138 DF):
  timer-IRQ bits=0x3  int0x80 bits=0x3
  PASS
probe runs against the secondary 16550 at boot)
ush> 
```

### nic_rw_test

| Field | Value |
|---|---|
| Name | nic_rw_test |
| Category | Network cards |
| Function | NIC loopback test |
| Usage | `nic_rw_test` |
| Source | `ocos/services/net/src/svc_net_nic_l1.c` |

**Executed example**:

```
nic_rw_test
[nic_rw_test] input: 0 registered NIC(s)
[nic_rw_test] no NICs registered => SKIP
ush> 
```

### other_nic_test

| Field | Value |
|---|---|
| Name | other_nic_test |
| Category | Network cards |
| Function | other NIC driver test |
| Usage | `other_nic_test` |
| Source | `ocos/services/net/src/svc_net_nic_l1.c` |

**Executed example**:

```
other_nic_test
[other_nic_test] no legacy NIC present in this environment => SKIPPED
ush> 
```

### rtl810x

| Field | Value |
|---|---|
| Name | rtl810x |
| Category | Network cards |
| Function | RTL810x NIC status |
| Usage | `rtl810x` |
| Source | `ocos/services/net/src/svc_net_nic_l1.c` |

**Executed example**:

```
rtl810x
rtl810x: not present
ush> 
```

### rtl810x_test

| Field | Value |
|---|---|
| Name | rtl810x_test |
| Category | Network cards |
| Function | RTL810x driver test |
| Usage | `rtl810x_test` |
| Source | `ocos/services/net/src/svc_net_nic_l1.c` |

**Executed example**:

```
rtl810x_test
[rtl810x_test] device not present in this environment => SKIPPED (needs real hardware; no fake
    output is produced for absent devices)
ush> 
```

### rtl8125

| Field | Value |
|---|---|
| Name | rtl8125 |
| Category | Network cards |
| Function | RTL8125 NIC status |
| Usage | `rtl8125` |
| Source | `ocos/services/net/src/svc_net_nic_l1.c` |

**Executed example**:

```
rtl8125
rtl8125: not present
ush> 
```

### rtl8125_test

| Field | Value |
|---|---|
| Name | rtl8125_test |
| Category | Network cards |
| Function | RTL8125 driver test |
| Usage | `rtl8125_test` |
| Source | `ocos/services/net/src/svc_net_nic_l1.c` |

**Executed example**:

```
rtl8125_test
[rtl8125_test] device not present in this environment => SKIPPED (needs real hardware; no fake
    output is produced for absent devices)
ush> 
```

### rtl8139

| Field | Value |
|---|---|
| Name | rtl8139 |
| Category | Network cards |
| Function | RTL8139 NIC status |
| Usage | `rtl8139` |
| Source | `ocos/services/net/src/svc_net_nic_l1.c` |

**Executed example**:

```
rtl8139
rtl8139: not present
ush> 
```

### rtl8139_test

| Field | Value |
|---|---|
| Name | rtl8139_test |
| Category | Network cards |
| Function | RTL8139 driver test |
| Usage | `rtl8139_test` |
| Source | `ocos/services/net/src/svc_net_nic_l1.c` |

**Executed example**:

```
rtl8139_test
[rtl8139_test] device not present in this environment => SKIPPED (needs real hardware; no fake
    output is produced for absent devices)
ush> 
```

### rtl8168

| Field | Value |
|---|---|
| Name | rtl8168 |
| Category | Network cards |
| Function | RTL8168 NIC status |
| Usage | `rtl8168` |
| Source | `ocos/services/net/src/svc_net_nic_l1.c` |

**Executed example**:

```
rtl8168
rtl8168: not present
ush> 
```

### rtl8168_test

| Field | Value |
|---|---|
| Name | rtl8168_test |
| Category | Network cards |
| Function | RTL8168 driver test |
| Usage | `rtl8168_test` |
| Source | `ocos/services/net/src/svc_net_nic_l1.c` |

**Executed example**:

```
rtl8168_test
[rtl8168_test] device not present in this environment => SKIPPED (needs real hardware; no fake
    output is produced for absent devices)
ush> 
```

### wifi

| Field | Value |
|---|---|
| Name | wifi |
| Category | Network cards |
| Function | wireless status/scan |
| Usage | `wifi [status\|scan\|...]` |
| Source | `ocos/services/net/src/svc_net_nic_l1.c` |

**Executed example**:

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
