<!-- SPDX-License-Identifier: Apache-2.0 -->
# Debugging

## Read the boot log first

Every subsystem prints one `.... OK` line at bring-up. The last line
that did NOT print is your failing subsystem. Capture it:

```
qemu-system-x86_64 -m 512 -cdrom build/opencube.iso \
    -serial file:boot.log -display none
```

## In-guest tools

```
oc> dmesg            (kernel ring buffer)
oc> logcat           (live follow of the persistent log)
oc> logctl           (log levels + filters)
oc> health           (CPU/mem/tasks/IO + alarms)
oc> exc              (exception self-test - proves the trap path)
oc> crashlog         (past crash records on /etc)
oc> dumpinfo         (crash dump details)
```

## The panic path

A kernel panic dumps the exception frame (registers + fault address)
to serial and writes a crashlog to /etc. `crashtest` triggers a
deliberate one - use it to validate that your /etc volume and serial
capture are wired correctly before you need them.

## Host-side debugging of the kernel

- Debug symbols: the pipeline links with `-g`, then strips the final
  ELF; `build/opencube_base.elf` (pre-`strip`, produced during
  `make kernel`) is your symbol carrier.
- In QEMU: `-d int,cpu_reset -D qemu.log` logs interrupts and resets;
  `-s -S` plus gdb (`target remote localhost:1234`) gives you the
  classic remote-debug loop.
- The reproducible build means a crash reproduced twice is *the same
  bytes* - no heisen-builds across attempts.

## Host unit tests

`make test` compiles and runs the harnesses under `tests/host_*.c`
against the real kernel sources (crypto vectors, tar, gzip, string,
FAT structures, mbi parsing with ASAN on the host compiler). A
regression here is much faster to iterate on than a boot loop.

## Known-sharp edges (from the audit trail)

- xHCI DMA above 4 GiB is refused at probe and per-transfer - check
  your QEMU memory size if a xHCI device vanishes.
- ATA-DMA alignment depends on the heap's 32-byte block header; if
  you touch `mem_heap.c`, run `fstest` + a FAT32 mount test.
- The multiboot2 parser validates tag sizes; a "hand-built" mbi in
  tests must respect the per-tag minimums or the parse stops early.
