<!-- SPDX-License-Identifier: Apache-2.0 -->
# Process scheduling

## The model

`kernel/core/core_sched.{c,h}`: preemptive, priority-based with
time-slice round-robin inside a priority level. Task control blocks
carry state, priority, slices, and the saved frame; the context
switch (`arch/x86_64/arch_context_switch.S`) is ABI-probe-verified
(SysV 16-byte stack alignment at the dispatch call - BUG-0137).

## Ticks

The PIT fires IRQ0 at 100 Hz: tick counter, `g_boot_time_ms`, the
soft-timer wheel, and the scheduler's preemption all hang off it.
`sched`, `ps`, `nice`, `sleep`, `kill`, `spawn`, `multi` are the
user-facing commands.

## Background tasks and the policy watchdog

Long-running work (`g6_test &`, spinners) runs as background tasks.
The watchdog subsystem (`kernel/core/core_watchdog.c`) adds monitored
slots: a task that must feed its slot and stops doing so is killed -
with the reason logged - while the kernel itself stays alive. The
hardware watchdogs (TCO/WDAT) protect the whole machine instead: see
[internals/watchdog.md](../internals/watchdog.md).

## Signals and processes

Ring-3 processes get signals through the syscall interface; the user
test programs (`run signal_test`, `run fork_test`, `run exec_test`,
`run pipe_test`, `run select_test`) demonstrate the full Unix-shaped
process model: fork, exec, pipes, select, mmap.
