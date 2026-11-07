<!-- SPDX-License-Identifier: Apache-2.0 -->
# The driver model

## Registration surfaces

Each family exposes an L1-registration API:

- `driver_block_register` + read/write/flush (+ set_ops) - block
  devices, per-instance names (ata0, usda, nvme0...)
- `driver_nic_register` + send/recv/link_status/get_mac
- `driver_snd_register` + play/stop/set_rate/set_volume/get_caps
- PCI: `driver_pci_find_class_exact/mask` for discovery

A driver probes its bus (PCI IDs, USB descriptors, i2c addresses),
claims the device, registers with its family, and from then on the
family-level commands and the VFS/socket layers see it uniformly.

## The PCI core

`drivers/pci/driver_pci.c` + pcie: config-space access, BAR decoding
(`pcibars`), capabilities; MSI where devices use it. `lspci`,
`pcidump`, `pcie` inspect from the shell.

## USB core

`drivers/usb/driver_usb.c` orchestrates four host-controller stacks
(UHCI/OHCI/EHCI/xHCI) with device enumeration, hubs to
USB_ENUM_DEPTH, and class drivers on top: MSC (mass storage), HID
(keyboards/mice), serial, audio. DMA discipline: xHCI refuses
above-4GiB addresses at probe and per-transfer.

## Error-path discipline

The audit history (BUG-0136..0238) made one thing non-negotiable: a
device that lies (truncated table, stuck NAK, garbage response) must
be met with a bounded, leak-free unwind. Every probe has a teardown
path; every transfer validates its bounds. Bring-up manuals per
family: [../drivers/](../drivers/).
