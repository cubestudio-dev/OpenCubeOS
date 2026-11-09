<!-- SPDX-License-Identifier: Apache-2.0 -->
# NIC bring-up manual

## Families

| Driver | PCI IDs (family) | QEMU | Command | Test |
|--------|------------------|------|---------|------|
| e1000 | 8086:100e etc. | `-device e1000` (default) | `ifconfig` | `dhcptest` via dhcp |
| e1000e | 8086:10d3 etc. | `-device e1000e` | `e1000e` | `e1000e_test` |
| igb | 8086:1533 era | (passthrough) | `igb` | `igb_test` |
| ixgbe | 8086:10fb era | (passthrough) | `ixgbe` | `ixgbe_test` |
| rtl8139 | 10ec:8139 | `-device rtl8139` | `rtl8139` | `rtl8139_test` |
| rtl8169-family | 10ec:8168/8125/810x | (real hw) | `rtl8168`/`rtl8125`/`rtl810x` | `*_test` |
| BCM57xx | 14e4:16xx era | (real hw) | `bcm57xx` | `bcm57xx_test` |

## Bring-up order

1. `lspci` shows the NIC; the family init binds it.
2. `ifconfig` shows the interface + MAC; `nicstat` the counters.
3. `dhcp` (or static `ip`/`ifconfig` configuration).
4. `ping 10.0.2.2` then `dns example.com` then `wget <host>`.
5. Gates: `nic_rw_test`, `tcptest`, the SSH/HTTPS E2E.

## Loopback between two instances

Two QEMUs on a shared socket/udp multicast make a real two-node
network for the NIC tests - the `*_test` commands are written for
both single-node and peer scenarios.

## Sharp edges

- RX checksum chain: any NIC change reruns `tcpstats` + `tcptest`
  (the double-complement bug lived exactly there).
- Link state polling vs interrupts differs per family; the
  `link_status` op is the contract.
