<!-- SPDX-License-Identifier: Apache-2.0 -->
# Input devices

## PS/2 keyboard

The primary input: scancode set 1 -> ASCII in the kernel
(`drivers/input/driver_input_keyboard.c`), feeding the line editor
directly. Always present in QEMU.

## COM1 serial

`screen_serial_in.c` - serial RX feeds the same keyboard queue, so
`-serial mon:stdio` gives you a full console.

## USB HID

`driver_usb_hid.c` - keyboards and mice through any host controller;
`usb_kbd_test` / `usb_mouse_test` gate them.

## I2C HID + touchpads

`driver_input_i2chid.c` + the touchpad stack: `i2cdetect` finds the
device, `touchpad`/`touchtest` exercise it (real hardware; QEMU has
no model).

## The contract

All input sources converge on the shell/TUI input queues - adding a
source means feeding the same queue, not new input paths.
