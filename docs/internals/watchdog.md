<!-- SPDX-License-Identifier: Apache-2.0 -->
# Internals: the watchdog subsystem

`kernel/core/core_watchdog.{c,h}` - two watchdog worlds, one header.

## Policy watchdogs (monitored slots)

A task registers a slot with a timeout:

```
slot = watchdog_register("mydaemon", 5000, policy);
watchdog_start(slot);
... watchdog_feed(slot);   /* at least every 5 s */
```

Timeout fires -> the TASK is killed, the reason lands in the kernel
log (`test log ERROR` in the E2E), the KERNEL lives on. This is the
right tool for "my background worker may hang".

## Hardware watchdogs

- **Intel TCO** (LPC): PMBASE discovered from the chipset, TCOBASE
  derived, the timer armed; expiry hard-resets the machine. Live on
  QEMU q35 - the E2E S12 prints the discovery line, arms, starves,
  and asserts the REAL second boot.
- **ACPI WDAT**: the table's entries program the hardware; same
  verify-by-reboot cycle (E2E S13).

`watchdog` shows both worlds: hardware support + slot table + policy
tests.

## The E2E scenarios that gate this

S7 (status/slots), S8 (policy kill + log), S12 (TCO real reset +
second boot), S13 (WDAT real reset + second boot). Fourteen total in
`tools/wp12_watchdog_logging_e2e.py` - all fourteen must PASS at
release.
