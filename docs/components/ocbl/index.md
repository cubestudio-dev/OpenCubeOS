<!-- SPDX-License-Identifier: Apache-2.0 -->
# OCBL - the Open Cube OS boot loader

The native boot loader: one shared core, two firmware flavors (BIOS
and UEFI), the boot-state monitor and the A/B slot machinery of the
recovery chain.

| Page | Contents |
|---|---|
| [Architecture and responsibilities](ocbl-architecture.md) | the two flavors, the handoff contract, the seven entry classes |
| [Dual-firmware boot and A/B slot decisions](ocbl-ab.md) | the two-partition recovery disk, the flags sector at LBA 1024, the state areas 0x98000/0x9B000, failure counting and the clear condition, the watchdog relay |
| [Troubleshooting and status codes](ocbl-troubleshooting.md) | the record kinds, the takeover menus, the /HANGAT hook, the declared UEFI limitation |

The Chinese edition of these pages lives under
[zh/components/ocbl/](../../zh/components/ocbl/index.md).
