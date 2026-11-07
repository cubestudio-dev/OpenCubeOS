<!-- SPDX-License-Identifier: Apache-2.0 -->
# Boot failure triage

## The boot log is a checklist

The kernel prints one line per subsystem IN BRING-UP ORDER (the
order is [arch/boot-flow.md](../arch/boot-flow.md)). Diagnosis:

1. **Nothing at all on serial** - GRUB didn't run or the ISO entry
   is wrong. Boot the ISO's other slot entry; try BIOS vs UEFI.
2. **Stops before `long mode entry OK`** - boot.S stage: the
   multiboot2 handshake failed (bad magic/mbi) - capture with
   `-serial file:` and check the mbi tag parse.
3. **Stops at a driver line** - that family's probe failed on your
   hardware; the message above it names the device. Remove the
   device (or the QEMU flag) to confirm.
4. **Reaches `boot complete` but no prompt** - shell init; extremely
   rare (the 233-command registration line would be the tell).
5. **Reboots after the prompt** - panic or watchdog: check /etc
   crashlog after the second boot (`dumpinfo`, `crashlog`), and the
   serial dump during the event.

## The self-tests are your friends

The boot sequence already proves: exception path (3/3), shell
registration (233), crash recovery armed. If those lines printed,
the machine underneath is sound - look at the specific feature, not
the platform.

## Second-boot analysis

The A/B flag + crashlog survive reboots on /etc. After any
unexpected reset: `dmesg` (current boot), `crashlog`/`dumpinfo`
(previous boot), `abcfg` (what GRUB tried to boot).
