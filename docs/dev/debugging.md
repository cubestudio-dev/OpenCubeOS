<!-- SPDX-License-Identifier: Apache-2.0 -->
# Debugging (entry page)

The short orientation. The full compendium - serial extraction,
the klog ring, crash dumps and register snapshots, the
silent-failure methodology, and the fault-injection tools - now
lives in [debug-handbook.md](debug-handbook.md); the inventory of
test programs and hidden flags lives in
[debug-interfaces.md](debug-interfaces.md). This page keeps the
daily-driver essentials and points onward.

## Read the boot log first

Every subsystem prints one `.... OK` line at bring-up. The last
line that did NOT print is your failing subsystem. Capture it:

```
qemu-system-x86_64 -m 512 -cdrom build/opencube.iso \
    -serial file:boot.log -display none
```

## In-guest tools

```
ush> dmesg            (kernel ring replay; dmesg N for more lines)
ush> logcat -f        (live follow of the kernel log)
ush> logctl status    (levels, filters, file state)
ush> health           (CPU/mem/tasks/IO + the three alarm bits)
ush> exc              (exception self-test - proves the trap path)
ush> crashlog         (past crash records)
ush> dumpinfo         (core dump status)
ush> crashtest dump   (capture a dump WITHOUT panicking - wiring check)
```

Line format everywhere: `[SSS.mmm] LEVEL module: message`.

## The panic path

A kernel panic dumps the exception frame (registers + fault
address + traceback) to serial and writes the crashlog ring;
`/var/crash/core.dump` carries the OCDUMP01 image when a
filesystem is mounted. `crashtest` triggers a deliberate one -
use it to validate that your capture wiring works *before* you
need it. What each register means per exception class:
[debug-handbook.md](debug-handbook.md) section 3.

## Nothing is wrong but nothing works?

Work the silent-failure ladder: health alarms -> the log *ring*
(not the live console) -> the kcap capture trap (captured ops
swallow live output - check the routing style) -> return-value
conventions (an L1SVC call "succeeds" when it routes; failure text
lives inside `out`) -> recognize the honest-skip strings. Full
methodology: [debug-handbook.md](debug-handbook.md) section 4.

## Host-side debugging of the kernel

- Debug symbols: the pipeline links with `-g`, then strips the
  final ELF; `build/opencube_base.elf` (pre-`strip`, produced
  during `make kernel`) is your symbol carrier.
- In QEMU: `-d int,cpu_reset -D qemu.log` logs interrupts and
  resets; `-s -S` plus gdb (`target remote localhost:1234`) gives
  the classic remote-debug loop.
- The reproducible build means a crash reproduced twice is *the
  same bytes* - no heisen-builds across attempts
  (`make reproducible-check`).

## Host unit tests

`make test` compiles and runs the harnesses under `tests/host_*.c`
against the real kernel sources (crypto vectors, tar, gzip,
string, FAT structures, MB2 parsing with ASAN on the host
compiler). A regression here is much faster to iterate on than a
boot loop. Inventory: [host-tests.md](host-tests.md).

## Known-sharp edges (from the audit trail)

- xHCI DMA above 4 GiB is refused at probe and per-transfer -
  check your QEMU memory size if an xHCI device vanishes.
- ATA-DMA alignment depends on the heap's 32-byte block header; if
  you touch `mem_heap.c`, run `fstest` + a FAT32 mount test.
- The multiboot2 parser validates tag sizes; a "hand-built" mbi in
  tests must respect the per-tag minimums or the parse stops
  early.
- QEMU drops serial RX bytes right after an HMP screendump -
  functional rounds never take screenshots
  ([qemu-testing.md](qemu-testing.md) section 4).
- A captured L1SVC op suppresses BOTH the framebuffer and the
  serial tee while it runs - ops that park or own a live session
  must route direct (the kcap trap,
  [debug-handbook.md](debug-handbook.md) 4.3).
- A `#DF` panic is deliberately minimal (one static serial line,
  no record/dump/flush) - do not expect a dump from a double
  fault.

## Symptom -> first move (quick reference)

| Symptom | First move | Why |
|---|---|---|
| Boot stops mid-ladder | read the serial boot log; last missing `OK` line = failing subsystem | every subsystem prints one `.... OK` at bring-up |
| Machine hangs, console dead | `/HANGAT`-style? check watchdog status + `crashlog` after reset | the 15 s soft dog + the 6300ESB relay produce evidence, not silence |
| Kernel panic | copy the serial banner + register dump; `crashlog`/`dumpinfo` after reboot | the OCDUMP01 image + 32-slot ring survive |
| User program misbehaves | run it under `run <name>`; `ps` before/after; check `exc` | ring-3 faults are kills, not panics - the kernel continues |
| Command "does nothing" | check the routing style + the return convention; look for honest-skip strings | an L1SVC call "succeeds" when it routes; failure text is inside `out` |
| Output vanished mid-op | the kcap capture trap | captures suppress live channels |
| Storage corruption suspicion | `fsck <dev>`, `disk_rw_test <dev>`, cache stats in `lsblk` | the write-back cache only releases dirty slots on successful write-back |
| Network stall | `netstat`, `tcpstats` (RTO/cwnd/SACK state), `net_rx_bad_checksum` | the conn table tells you which layer stalled |
| Suspected entropy/crypto issue | `cryptotest`, `dhtest` | NIST-vector gates on the exact primitives TLS/SSH use |

## Filing a debuggable report

Before reporting a kernel-side problem, collect this checklist -
every item is a mechanism the tree already gives you:

1. **The build identity**: `uname -a` output (version string +
   the numeric build id that the OTA comparison uses) - the
   reproducible build means those two numbers pin the exact
   bytes.
2. **The full serial log** (`-serial file:boot.log`), not a
   screenshot of the tail - the boot ladder's last missing `OK`
   line names the failing subsystem.
3. **The klog ring tail**: `dmesg 200` (and `logctl status` so
   filters/levels are known); note the footer
   `-- <shown> lines (ring <count>/1024, dropped <n>) --`.
4. **Crash evidence, if any**: the panic banner + register dump
   from serial, then after reboot `crashlog` (and `crashlog flush`) plus `dumpinfo`; the OCDUMP01 image from
   `/var/crash/core.dump` when a filesystem was mounted.
5. **The health snapshot** (`health`) - the alarms line and the
   five metric sections rule the usual suspects in or out.
6. **Reproduction steps** on a clean `make run-bios` boot, and
   whether `make reproducible-check` passes on your tree (a
   crash reproduced twice is the same bytes).
7. For QEMU: the exact command line (machine type matters - TCO
   exists on q35 only) and the QEMU version.

## Where to go next

| Need | Page |
|---|---|
| Serial/klog/crash evidence, register reading, fault injection | [debug-handbook.md](debug-handbook.md) |
| The ~93 test programs, diag depth, hidden flags | [debug-interfaces.md](debug-interfaces.md) |
| Internal call paths (execve, L1SVC, RX, block IO, crash, OTA) | [call-chains.md](call-chains.md) |
| Kernel structures and who touches them | [data-structures.md](data-structures.md) |
| QEMU errata, sendkey, OVMF | [qemu-testing.md](qemu-testing.md) |
| Subsystem deep dives (klog, crash, health, watchdog, crypto, OTA, OCIRE embed) | [../internals/index.md](../internals/index.md) |
