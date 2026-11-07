<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 cubestudio-dev <cubestudio@qq.com> -->

# Open Cube OS — WP-10d-fix2 Extension Interfaces (Power Management + Structured Help)

**WP-10d-fix2 adds 7 new L1 extension points (items 123-129). Total L1
surface is now 129.**

WP-10d-fix2 brings real power management to the oc> shell (the system had
`reboot` but no way to power off) and replaces the registration-order
`help` listing with two structured views. All previous interfaces remain
unchanged. The header `kernel/core/core_power.h` is self-contained; the shell
extensions live in `shell/shell.h`.

## 1. Power management (kernel/core/core_power.h)

Every path flushes all block devices first (`core_power_flush_blk()`), so an
A/B update and user data survive the power transition.

### item 123 — `int core_power_shutdown(void)`

Power off the machine. Tries, in order:

1. QEMU/PIIX4 ACPI PM1a_CNT — `outw(0x604, 0x2000)` (SLP_EN);
2. Bochs-compatible shutdown port — `outw(0xB004, 0x2000)`;
3. legacy APM shutdown word — `outw(0xF000, 0x0000)`.

Returns `OC_POWER_E_UNSUPPORTED` (-1) when the machine is still running
after all three attempts (the caller should then `core_power_halt()`).
**On success the function never returns.**

Shell: `shutdown` / `poweroff` (alias) — print `Shutting down...`, then
`Powering off...` on QEMU, or the honest fallback
`Power off not supported. System halted.`

### item 124 — `int core_power_suspend(void)`

Suspend to RAM (ACPI S3). Since WP-10-chinese_support-fix2 the full S3
path is implemented: FACS waking vector + a 16/32/64-bit real-mode
resume trampoline (the firmware delivers the waking vector in 16-bit
big real mode - SeaBIOS farcall16big) + post-resume re-arm (PIT
reprogram without wiping the soft-timer table, PCI command restore, NIC
register replay). Returns 0 after a live resume,
`OC_POWER_E_UNSUPPORTED` when the platform offers no \_S3. fix2 root
cause note: the pre-fix2 trampoline was 32-bit flat code, but SeaBIOS
delivers the waking vector through farcall16big in 16-bit big real
mode - every resume crashed in the trampoline before the kernel ever
ran again. The reworked trampoline resumes correctly on QEMU 8.1.5 TCG
and QEMU 10.0.13 TCG (see KNOWN_ISSUES.md for the wake procedure).

Shell: `suspend` (real S3); `sleep <seconds>` is the BUG-0346 delay
command, not suspend.

### item 125 — `void core_power_halt(void)`

Stop the CPU forever: print `System halted.`, hide the cursor, `cli`,
then an `hlt` loop (interrupts stay disabled so nothing wakes the CPU).
Never returns.

Shell: `halt`.

### item 126 — `void core_power_reboot(void)`

Flush every block device, print `Rebooting...`, then reset via the 8042
keyboard controller (`outb(0x64, 0xFE)`) with the ACPI RESET_REG
(`0xCF9`, sequence 0x02 then 0x06) as fallback. Never returns.

Shell: `reboot` (implementation moved from `ota_ab_update.c`; the WP-10u
path is preserved bit-for-bit).

## 2. Structured help (shell/shell.h)

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

## 3. Shell test suite (shell/shell_cmds_power_test.c)

| command | what it really does |
|---|---|
| `poweroff_test` | really calls `core_power_shutdown()` — on QEMU the process exits (verified: exit code 0 after `Powering off...`) |
| `suspend_test` | read-only probe of the S3 decision path (sleep mask + SLP_TYP on ACPI platforms, honest unsupported report otherwise); the live suspend->wake cycle is verified in the dedicated QEMU session |
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
- `suspend` → real ACPI S3 on platforms with \_S3 (`Entering S3
  (suspend to RAM)...` then `Resumed from S3.`); `Suspend not supported
  (ACPI required).` only on ACPI-less machines; `suspend_test` is the
  read-only capability probe.
- `help` / `help -a` / `help -w` → 170 commands, sorted / grouped.
