# WP-10d — USB Host Stack: L0→L1 Extension Interfaces

Copyright 2026 cubestudio-dev <cubestudio@qq.com>
SPDX-License-Identifier: Apache-2.0

WP-10d turns the WP-10c minimal UHCI-only USB stack into a full host
stack: four controller backends (UHCI / OHCI / EHCI / XHCI), a core
with the four transfer types, external-hub support, hot-plug, and a
class-driver registry (HID keyboard/mouse, MSC storage, CDC-ACM/FTDI
serial, UAC audio).

Every interface below follows the L0 contract: **declared in a kernel
header, documented in the header, implemented with a working default,
and usable from `oc>` without any host-side tool**.

New kernel header: `drivers/usb/driver_usb.h` (rewritten for WP-10d).

---

## 1. `int driver_usb_register_host(driver_usb_host_t *host, const driver_usb_hc_ops_t *ops)`

* **Header**: `drivers/usb/driver_usb.h`
* **Doc**: "Register a host controller with the USB core.  The caller
  fills host->name and keeps host->priv for itself.  Returns 0 on
  success (host->index assigned), -1 on error."
* **Implementation**: `drivers/usb/driver_usb.c` — appends to the core host table,
  sets `up = 1`, logs `usb: registered host <type>`.
* **Default behaviour**: every backend (`driver_usb_uhci_probe_one`,
  `driver_usb_ohci_probe_one`, `driver_usb_ehci_probe_one`, `driver_usb_xhci_probe_one`) calls it after
  its hardware bring-up; L1 code can plug additional host controllers
  the same way.
* **From `oc>`**: `usb` lists every registered host.

## 2. `int driver_usb_enumerate_host(driver_usb_host_t *host)`

* **Header**: `drivers/usb/driver_usb.h`
* **Doc**: "Enumerate the devices hanging off `host` (root-hub ports
  and any hubs).  Returns the number of devices found (>= 0)."
* **Implementation**: `drivers/usb/driver_usb.c` — walks root-hub ports via
  `ops->port_count/port_status`, calls `driver_usb_enum_one()` per empty
  connected port, then recursively walks newly-found external hubs.
* **Default behaviour**: `driver_usb_enumerate()` (WP-10c signature kept)
  iterates every registered host; `driver_usb_probe_all()` calls it once all
  controllers are up; `driver_usb_poll()` re-runs it on hot-plug.

## 3. `int driver_usb_control_transfer(driver_usb_dev_t *d, const driver_usb_setup_t *setup,
                                void *buf, u16 len)`

* **Header**: `drivers/usb/driver_usb.h`
* **Doc**: "Blocking control transfer (HOST->dev SETUP, then optional
  DATA stage, then STATUS).  Returns 0 on success, negative on
  error / timeout."
* **Implementation**: dispatches to `ops->control` under the global
  USB transfer mutex; every backend implements the full three-stage
  sequence natively (UHCI/ OHCI/ EHCI: qTD/TD chains; XHCI: SETUP /
  DATA / STATUS TRBs on EP0).

## 4. `int driver_usb_bulk_transfer(driver_usb_dev_t *d, u8 ep_addr, void *buf, u16 len)`

* **Header**: `drivers/usb/driver_usb.h`
* **Doc**: "Blocking bulk transfer on endpoint `ep_addr` (bit7 = IN).
  Returns bytes transferred (>= 0) or a negative error."
  A `_timeout` variant (`driver_usb_bulk_transfer_timeout`) exposes the
  deadline; the plain call uses 3 s.
* **Implementation**: `ops->bulk` per backend.  The MSC class driver
  chunks BOT data phases with `ops->bulk_max` so every backend gets a
  working split (UHCI/OHCI 512, EHCI 4096, XHCI 1024/4096).

## 5. `int driver_usb_interrupt_transfer(driver_usb_dev_t *d, u8 ep_addr, void *buf, u16 len)`

* **Header**: `drivers/usb/driver_usb.h`
* **Doc**: "One blocking interrupt-IN/OUT transaction on `ep_addr`.
  Returns bytes transferred (>= 0), -OC_USB_ETIMEDOUT when the
  endpoint NAKs for the whole timeout window (a normal "no event"
  result for HID devices)."
* **Implementation**: UHCI/OHCI/EHCI schedule interrupt TDs/qTDs on
  their periodic structures; XHCI arms a Normal TRB on the endpoint
  ring and latches late completions so the next poll picks the data
  up.  The HID class driver polls keyboards and mice through it.

## 6. `int driver_usb_isochronous_transfer(driver_usb_dev_t *d, u8 ep_addr, void *buf, u16 len)`

* **Header**: `drivers/usb/driver_usb.h`
* **Doc**: "Schedule one isochronous packet (audio: one per 1 ms
  frame)."
* **Implementation**: dispatches to `ops->driver_usb_iso_out` / `ops->driver_usb_iso_in`.
  UHCI keeps the WP-10c per-frame ISO TD path (used by the USB audio
  class driver); XHCI arms an ISOCH TRB; EHCI/OHCI return
  `-OC_USB_EINVAL` (no current class driver consumes ISO there).

## 7. `int driver_usb_register_driver(const char *name, u8 class_code,
                               driver_usb_probe_fn probe,
                               driver_usb_disconnect_fn disconnect)`

* **Header**: `drivers/usb/driver_usb.h`
* **Doc**: "Register a class driver.  The core calls probe() for every
  device whose interface/device class matches class_code
  (0xff = match everything).  disconnect() runs on hot-unplug."
* **Implementation**: `drivers/usb/driver_usb.c` registry (8 slots) +
  `driver_usb_match_probe()` at the end of every enumeration and
  `disconnect` from `driver_usb_kill_slot()` on unplug.
* **Built-in class drivers** (registered from `kmain` before
  `driver_usb_probe_all`): `hid-kbd`, `hid-mouse`, `usb-msc`, `usb-serial`
  (CDC-ACM + FTDI SIO), `usb-audio`, plus the built-in hub walker.

## 8. `int driver_usb_uhci_init(const driver_pci_dev_t *dev)` — `drivers/usb/driver_usb.c`

* **Header**: `drivers/usb/driver_usb.h`
* **Doc**: "Probe/claim one PCI function as the given controller
  type, reset it, start the schedule and register it with the core.
  Returns 0 on success, -1 when the controller is absent or init
  fails.  driver_usb_probe_all() calls all four in turn."
* **Implementation**: Intel PIIX3/PIIX4 UHCI (8086:7020 / 8086:7112),
  PIO BAR, frame list + QH/TD schedule, per-address/endpoint/direction
  software data toggles.

## 9. `int driver_usb_ohci_init(const driver_pci_dev_t *dev)` — `drivers/usb/driver_usb_ohci.c`

* **Header**: `drivers/usb/driver_usb.h`
* **Doc**: same contract as `driver_usb_uhci_init`.
* **Implementation**: PCI class 0x0c0310, HCCA + control/bulk/int
  ED/TD pools, HcControl HCFS=USBOPERATIONAL with CLE/BLE/PLE.
  Live-verified in QEMU (pci-ohci): keyboard + mouse enumeration with
  product strings, HID attach, usb_core_test 5/5, MSC capacity/MBR/
  write-read-back, FAT32 mkfs + mount + file round-trip, real
  hot-plug (device_add during a wait window).  Control transfers
  set TD_R (buffer rounding) on the DATA TD and the status TD
  direction is a full 32-bit field - both were STALL/underrun bugs
  fixed in the device-level bring-up.

## 10. `int driver_usb_ehci_init(const driver_pci_dev_t *dev)` — `drivers/usb/driver_usb_ehci.c`

* **Header**: `drivers/usb/driver_usb.h`
* **Doc**: same contract as `driver_usb_uhci_init`.
* **Implementation**: PCI class 0x0c0320, async ring (ctrl QH is the
  head of the reclamation list — H bit, spec 4.9.1.1) + periodic
  frame list, qTD engine with spec-correct buffer pointers (bufptr[0]
  keeps the in-page offset), async-advance doorbell (IAAD) with IAA
  clearing, port power/enable preservation on PORTSC writes.
  Live-verified in QEMU: keyboard + mouse + storage enumeration,
  MSC read/write round-trips, FAT32 mount and file round-trip.

## 11. `int driver_usb_xhci_init(const driver_pci_dev_t *dev)` — `drivers/usb/driver_usb_xhci.c`

* **Header**: `drivers/usb/driver_usb.h`
* **Doc**: same contract as `driver_usb_uhci_init`.
* **Implementation**: PCI class 0x0c0330 (qemu-xhci, NEC, real HW),
  command/event rings with LINK-TRB cycle handling, DCBAA +
  scratchpad, AddressDevice two-stage (BSR=1 keeps the device at
  address 0 while the core reads desc8; the core's SET_ADDRESS is
  intercepted and mapped onto the native AddressDevice), lazy
  ConfigureEndpoint on first non-default endpoint use, endpoint
  contexts with spec layout (DW1 type+MPS, DW2 dequeue|DCS), PORTSC
  with the 16-byte register stride.  Reset Endpoint / Set TR
  Dequeue carry the endpoint ID in control bits 20:16 (spec Table
  6-42/6-44) - an EPID of 0 made the controller retire the command
  with TRB Error and left a halted endpoint unrecoverable.  Live-
  verified in QEMU: keyboard + mouse enumeration and HID event
  flow, usb_core_test 5/5, MSC attach + INQUIRY/READ CAPACITY and
  full BOT sequences on the bus (honest gap: qemu-xhci's
  usb-storage still STALLs the BOT CSW handback after the first
  STALL recovery; UHCI/OHCI/EHCI MSC fully verified).

---

## Shell surface (usable from `oc>`, no host tools)

* `usb` — hosts, device tree, class-driver counters.
* `usbdev [slot]` — full device detail (descriptors, endpoints,
  assigned driver).
* `usb_core_test` — host registration, device-table consistency,
  live GET_STATUS on every device, re-enumeration idempotence.
* `usb_kbd_test` / `usb_mouse_test` — HID attach + event counters.
* `usb_storage_test` — MSC attach, capacity, MBR read, write/read-back
  verify on the last sector.
* `usb_serial_test` — CDC-ACM/FTDI loopback (with the runner's echo
  service).
* `usb_hotplug_test` — wait for a device add/remove and verify the
  class disconnect hooks.
* `usb_hub_test` — hub enumeration counters.
* `usb_audio_test` — WP-10c UAC path re-validated through the new
  core (kept from WP-10c).

## Honest status per backend (this work package)

| Backend | host reg | port reset | enumeration | HID | MSC | notes |
|---------|----------|------------|-------------|-----|-----|-------|
| UHCI    | PASS     | PASS       | PASS        | PASS| PASS| audio PASS (WP-10c path) |
| EHCI    | PASS     | PASS       | PASS        | PASS| PASS| FAT32 mount + file round-trip PASS |
| XHCI    | PASS     | PASS       | PASS        | PASS| n/a | qemu-xhci's storage model is
          |          |            |             |     |     | SuperSpeed; BOT on SS is an
          |          |            |             |     |     | open gap (see report) |
| OHCI    | PASS     | PASS       | open        | open| open| TDs verified executed;
          |          |            |             |     |     | device-level STALL under
          |          |            |             |     |     | investigation |
