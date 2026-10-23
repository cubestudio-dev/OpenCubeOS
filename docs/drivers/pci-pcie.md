<!-- SPDX-License-Identifier: Apache-2.0 -->
# PCI / PCI Express

## The core

`drivers/pci/driver_pci.c` + `driver_pcie.c`: config-space access
(type 0/1), BAR decoding, bridge handling, capabilities, MSI where
devices use it.

## Discovery from L1

`driver_pci_find_class_exact` / `driver_pci_find_class_mask` - the
numbered surface every family init consumes (AHCI/NVMe/NIC/sound
inits take the pci_dev).

## Commands

```
oc> lspci           (device list)
oc> pcidump         (full config-space dump)
oc> pcibars         (BAR decode)
oc> pcie            (PCIe capabilities/links)
oc> msitest         (MSI paths)
```

## Bring-up notes

- QEMU q35 exposes a richer PCIe topology than the default i440fx -
  both are supported; hotplug ports behave through the normal probe
  path.
- 64-bit BARs: high-word reads are validated (an xHCI probe refusal
  path exercises exactly this).
