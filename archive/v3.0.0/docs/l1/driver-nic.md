<!-- SPDX-License-Identifier: Apache-2.0 -->
# L1 guide: NIC drivers

## The surface

`drivers/nic/driver_nic.h` (34 declarations; the numbered 13):

```
driver_nic_register(name, ops, ctx)
driver_nic_send(dev, frame, len)
driver_nic_recv(dev, buf, maxlen)        -> length / 0 / <0
driver_nic_link_status(dev)              -> link up/down
driver_nic_get_mac(dev, out[6])
```

Family inits: `driver_nic_e1000e_init`, `_igb_init`, `_ixgbe_init`,
`_rtl8139_init`, `rtl8168_init`, `rtl8125_init`, `rtl810x_init`,
`bcm57xx_init`.

## What the stack expects

Frames are raw Ethernet; the stack (`net/net_core.c`) owns ARP and
above. RX can be polled or interrupt-driven per the ops you
register. `ifconfig`, `nicstat`, the `*_test` commands observe.

## The reference implementations

Read one Intel (e1000e) and one Realtek (rtl8169-family) driver
before writing yours: descriptor rings, link-state handling, and the
error paths (bus-master disable, reset on timeout) are laid out
differently but the contract is identical.
