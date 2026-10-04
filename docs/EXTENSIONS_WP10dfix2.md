<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 cubestudio-dev <cubestudio@qq.com> -->

# Open Cube OS — WP-10d-fix2 Extension Interfaces (Power Management + Structured Help)

**WP-10d-fix2 adds 7 new L1 extension points (items 123-129). Total L1
surface is now 129.**

WP-10d-fix2 brings real power management to the oc> shell (the system had
`reboot` but no way to power off) and replaces the registration-order
`help` listing with two structured views. All previous interfaces remain
unchanged. The header `kernel/power.h` is self-contained; the shell
extensions live in `kernel/shell.h`.

## 1. Power management (kernel/power.h)

Every path flushes all block devices first (`power_flush_blk()`), so an
A/B update and user data survive the power transition.

### item 123 — `int power_shutdown(void)`

Power off the machine. Tries, in order:

1. QEMU/PIIX4 ACPI PM1a_CNT — `outw(0x604, 0x2000)` (SLP_EN);
2. Bochs-compatible shutdown port — `outw(0xB004, 0x2000)`;
3. legacy APM shutdown word — `outw(0xF000, 0x0000)`.

Returns `OC_POWER_E_UNSUPPORTED` (-1) when the machine is still running
after all three attempts (the caller should then `power_halt()`).
**On success the function never returns.**

Shell: `shutdown` / `poweroff` (alias) — print `Shutting down...`, then
`Powering off...` on QEMU, or the honest fallback
`Power off not supported. System halted.`

### item 124 — `int power_suspend(void)`

Suspend to RAM (ACPI S3). Open Cube OS does not implement the ACPI S3
wake path yet (FADT parsing / wake vector / resume belong to WP-10e), so
the function returns `OC_POWER_E_UNSUPPORTED` and the shell prints the
honest message `Suspend not supported (ACPI required).` — no fake
suspend, no silent downgrade.

Shell: `suspend` / `sleep` (alias).

### item 125 — `void power_halt(void)`

Stop the CPU forever: print `System halted.`, hide the cursor, `cli`,
then an `hlt` loop (interrupts stay disabled so nothing wakes the CPU).
Never returns.

Shell: `halt`.

### item 126 — `void power_reboot(void)`

Flush every block device, print `Rebooting...`, then reset via the 8042
keyboard controller (`outb(0x64, 0xFE)`) with the ACPI RESET_REG
(`0xCF9`, sequence 0x02 then 0x06) as fallback. Never returns.

Shell: `reboot` (implementation moved from `ab_update.c`; the WP-10u
path is preserved bit-for-bit).

## 2. Structured help (kernel/shell.h)

### item 127 — `int shell_register_command_ex(name, handler, help, wp)`

Register a command with a work-package tag. Same table and semantics as
`shell_register_command()`; the tag (e.g. `"WP-10d"`,
`"WP-10c-selfhost"`) drives the `help -w` grouping. An empty tag reads
as `"WP-03"`. Returns 0 / -1 bad args / -2 table full.

### item 128 — `void shell_list_commands_a_z(void)`

Print every registered command sorted A-Z (case-insensitive), one per
line, `  <name><pad> - <help>` with a live `Total:` line. This is the
default `help` view (and `help -a`). The old registration-order listing
is retired.

### item 129 — `void shell_list_commands_by_wp(void)`

Print the commands grouped by work-package tag in canonical order
(`WP-01` .. `WP-10d-fix2`), each group internally sorted A-Z, headers
`=== <tag> (<description>) ===`, empty groups skipped, tags registered
by L1 code that are not in the table appended A-Z at the end.

Shell: `help` (default = A-Z), `help -a` (same), `help -w` (grouped).

## 3. Shell test suite (kernel/power_test_cmds.c)

| command | what it really does |
|---|---|
| `poweroff_test` | really calls `power_shutdown()` — on QEMU the process exits (verified: exit code 0 after `Powering off...`) |
| `suspend_test` | really calls `power_suspend()` and requires the honest unsupported contract |
| `halt_test` | verifies the halt path; a true `cli;hlt` cannot return, the live halt is verified by running `halt` (serial goes silent) |
| `reboot_test` | really flushes every block device; the live reset is verified by running `reboot` (boot banner reappears) |
| `help_default_test` | verifies the A-Z ordering of the full table |
| `help_wp_test` | verifies every known WP group is internally sorted |
| `help_a_test` | verifies `help -a` == the default A-Z view |

## 4. Live verification (QEMU, BIOS and UEFI)

- `shutdown` → `Shutting down...` / `Powering off...`, QEMU exits 0.
- `poweroff` → same (alias).
- `reboot` → `Rebooting...`, GRUB runs again, `kmain entered` twice.
- `halt` → `System halted.`, serial output silent from then on.
- `suspend` / `sleep` → `Suspending...` / `Suspend not supported
  (ACPI required).`
- `help` / `help -a` / `help -w` → 170 commands, sorted / grouped.
