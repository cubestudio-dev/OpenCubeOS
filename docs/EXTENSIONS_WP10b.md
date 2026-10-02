# WP-10b — NIC Drivers (Network Interface Cards)

**Status**: complete · **Release**: WP-10b · **License**: Apache 2.0
**Copyright**: cubestudio-dev <cubestudio@qq.com>

WP-10b adds a NIC driver framework plus nine driver families so the
kernel can detect and drive mainstream wired Ethernet adapters on real
machines, while keeping every earlier work package intact.

## 1. Framework (kernel/nic.h, kernel/nic.c)

A static registry (8 slots) in the same shape as the WP-10a block
layer.  The protocol stack (net.c) routes frames through the framework
when a WP-10b driver owns the hardware; the legacy built-in
e1000/virtio-net paths remain as fallbacks so WP-06..WP-09 behaviour is
unchanged.

L1 extension surface:

| Interface | Semantics |
|---|---|
| `nic_register(dev, ops)` | register a NIC; returns index or -1; `ops->send/recv` mandatory |
| `nic_send(dev, buf, len)` | hand one Ethernet frame to the driver; returns bytes queued or -1 |
| `nic_recv(dev, buf, maxlen)` | poll one received frame; >0 length, 0 none, -1 error |
| `nic_link_status(dev)` | 1 up / 0 down / -1 unknown |
| `nic_get_mac(dev, mac[6])` | station address; 0 / -1 |
| `nic_probe_all()` | probe every family in priority order; 0 = one NIC live |
| `nic_active()` | the device net.c routes through, or NULL |

## 2. Driver families

| Driver | File | PCI IDs | Bus | Descriptors | Verified in |
|---|---|---|---|---|---|
| e1000e | kernel/nic_e1000e.c | 8086:10D3, 10EA, 15B8 | MMIO | legacy 16 B | QEMU (`-device e1000e`) — TX/RX/DHCP/ping/HTTPS live |
| igb | kernel/nic_igb.c | 8086:10C9, 1521 | MMIO | advanced 16 B (2 RX + 2 TX queues) | QEMU (`-device igb`) — TX/RX/DHCP/ping/HTTPS live |
| ixgbe | kernel/nic_ixgbe.c | 8086:10FB, 1563 | MMIO | advanced 16 B | datasheet-derived; no QEMU model |
| rtl8139 | kernel/nic_rtl8139.c | 10EC:8139, 8138; 1113:1211 | PIO | 4 fixed TX slots + 64 KiB RX ring | QEMU (`-device rtl8139`) — TX/RX/DHCP/ping/HTTPS live |
| rtl8168 | kernel/nic_rtl8169.c | 10EC:8168, 8161 | PIO | ring, 16 B | datasheet-derived; no QEMU model |
| rtl8125 | kernel/nic_rtl8169.c | 10EC:8125, 3000 | PIO | ring, 16 B | datasheet-derived; no QEMU model |
| rtl810x | kernel/nic_rtl8169.c | 10EC:8136, 8137 | PIO | ring, 16 B | datasheet-derived; no QEMU model |
| bcm57xx | kernel/nic_bcm57xx.c | 14E4:1644/1653/1673/1677/4401/170C | MMIO | host rings + mailboxes | datasheet-derived; no QEMU model |
| other | kernel/nic_other.c | 10B7:3c59x, 10DE:nForce, 1969:AR81xx, 11AB:Yukon | PIO/MMIO | per-family | detect + core only; datasheet-derived |

Per-driver init entry points (all take `pci_dev_t *pdev`; NULL = scan
the PCI bus): `e1000e_init`, `igb_init`, `ixgbe_init`, `rtl8139_init`,
`rtl8168_init`, `rtl8125_init`, `rtl810x_init`, `bcm57xx_init`,
`other_nics_init`.

All DMA buffers/descriptor rings come from the identity-mapped PMM
region (a static `.bss` buffer truncates to a wrong 32-bit DMA address
under `-mcmodel=kernel` — this bit us once during bring-up and is
documented in nic_rtl8139.c).

## 3. Shell commands

New tests: `e1000e_test`, `igb_test`, `ixgbe_test`, `rtl8139_test`,
`rtl8168_test`, `rtl8125_test`, `rtl810x_test`, `bcm57xx_test`,
`other_nic_test`, `nic_rw_test`.

New status: `e1000e`, `igb`, `ixgbe`, `rtl8139`, `rtl8168`,
`rtl8125`, `rtl810x`, `bcm57xx`.

Updated: `ifconfig` (prints the actual driver name; link comes from the
framework driver), `lspci` (lists WP-10b devices already), `netstat`,
`ip` (unchanged behaviour; verified under the framework), and
`real_hw_test` (now also prints every NIC driver's state on bare metal).

Total: 116 unique commands registered at boot (was 98).

## 4. Tests and verification matrix

| Test | e1000e | igb | rtl8139 | others |
|---|---|---|---|---|
| registration | PASS | PASS | PASS | SKIPPED (no device) |
| MAC | PASS | PASS | PASS | SKIPPED |
| link up | PASS | PASS | PASS | SKIPPED |
| TX (60 B ARP broadcast) | PASS | PASS | PASS | SKIPPED |
| RX (ARP reply via stack poll) | PASS | PASS | PASS | SKIPPED |
| dhcp / ping / wget HTTPS E2E | PASS | PASS | PASS | not run |

Honest disclosure: QEMU ships device models only for e1000/e1000e/igb/
rtl8139/virtio-net from the WP-10b list.  ixgbe, RTL8168/8125/810x,
BCM57xx and the legacy others are implemented from their datasheets,
compile clean, PCI-probe (finding nothing under QEMU) and report
`SKIPPED` in their tests rather than fabricating results.  Real-machine
validation (`real_hw_test`) is NOT RUN in this sandbox: there is no
bare-metal machine here.

## 5. Regression

- WP-09 18/18 boot regression: all PASS (banner, uname, 12 user
  programs, p3_test, heaptest, l1test, crashlog).
- WP-10a storage: `ahci_test`, `nvme_test`, `virtio_blk_test`,
  `disk_rw_test`, `partition_test` (MBR+GPT) all PASS with four disks
  attached (IDE/AHCI/virtio/NVMe); virtio capacity fix intact.
- Boot: verified with SeaBIOS (BIOS) and OVMF (UEFI), both with the
  e1000e NIC attached.
