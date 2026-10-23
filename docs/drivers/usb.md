<!-- SPDX-License-Identifier: Apache-2.0 -->
# USB bring-up manual

## Host controllers

| Stack | QEMU | Notes |
|-------|------|-------|
| UHCI | `-device usb-uhci` | frame lists, TD retirement shared code |
| OHCI | `-device usb-ohci` | ED/TD pools, 7-bit toggle table |
| EHCI | `-device usb-ehci` | async + periodic schedules, IAA unlink |
| xHCI | `-device qemu-xhci` | rings with LINK cycle, scratchpad, 64-bar high-word |

## Devices + tests

| Class | QEMU device | Command | Test |
|-------|-------------|---------|------|
| MSC storage | `-device usb-storage` | `usb`, `usbdev` | `usb_storage_test` |
| HID keyboard | `-device usb-kbd` | (feeds input queue) | `usb_kbd_test` |
| HID mouse | `-device usb-mouse` | (pointer) | `usb_mouse_test` |
| Serial | `-device usb-serial` | `usbdev` | `usb_serial_test` |
| Audio | `-device usb-audio` | `usbaudio` | `usb_audio_test` |
| Hub | `-device usb-hub` | `usb` | `usb_hub_test` |

`usb_hotplug_test` covers attach/detach handling.

## Bring-up order

1. `usb` lists controllers + devices; enumeration depth reaches
   USB_ENUM_DEPTH through hubs.
2. The class driver binds (MSC registers a block device; HID feeds
   the input queues; serial creates a port).
3. The family test command is the gate (`usb_storage_test` 5/5 etc.).

## Sharp edges (audit history)

- xHCI DMA above 4 GiB: refused at probe + per-transfer.
- C_PORT_RESET wait with timeout; hub speed decode; recursive
  enumeration bounds.
- MSC: SENSE inside one busy window (the never-sends-sense bug),
  per-instance names usda/usdb.
- ISO audio endpoints zero-pad tails (no OOB reads).
