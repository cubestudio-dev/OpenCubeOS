<!-- SPDX-License-Identifier: Apache-2.0 -->
# Bluetooth

## The stack

`drivers/bluetooth/`: HCI (`driver_bt_hci.c`), H4 transport
(`driver_bt_h4.c`), L2CAP, RFCOMM, SDP, SPP (serial profile), HID
(input devices), and USB-attached controllers (`driver_bt_usb.c`).

## Command

```
oc> bt    (controller status, inquiry, services)
```

## Bring-up

HCI transport init (H4 UART or USB dongle), controller reset, then
L2CAP/RFCOMM channels for SPP. The layered tests run through `bt`.
