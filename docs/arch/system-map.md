<!-- SPDX-License-Identifier: Apache-2.0 -->
# System map: the four components and how they cooperate

Open Cube OS is not one program. Four independently linked binaries share
one repository and one version string, and pass control between each other
through fixed physical memory contracts. This page is the panoramic view:
what each component is, the collaboration diagram, the version chain, the
physical memory map that binds them, and every boot mode the system
supports. Deep dives: [boot-flow.md](boot-flow.md) for the loader chain,
[hidden-mechanisms.md](hidden-mechanisms.md) for the cross-component
conspiracies, [tree-map.md](tree-map.md) for the file tree.

## The four components

| Component | Location | Identity |
|---|---|---|
| **OCBL** | `ocbl/` | The boot loader (BIOS + UEFI flavors). Replaced GRUB on the release ISO in 3.0.0-WP05 B5-7. Scans every disk, classifies every bootable volume, runs the 10-second menu, hands the chosen kernel a GRUB-identical multiboot2 state. |
| **OCSE** | `ocse/` | "OpenCubeOS Setup Environment" - the installer. A small standalone kernel **vendored from git tag v1.0.0** (143 files, frozen on purpose: see `ocse/VENDORING.md`), carrying its own driver set (ATA/AHCI/NVMe/virtio/USB/ACPI), a 40-command shell (OCSECL), a 9-step GUI wizard and an 8-step install state machine. |
| **OCOS** | `ocos/` | The main system: the L0 service kernel (`ocos/kernel/`), the driver layer (`ocos/drivers/`), the 24 service families (`ocos/services/`), the ring-3 API contract (`ocos/interface/`) and 284 embedded ring-3 applications (`ocos/l1apps/`). Everything a running system is. |
| **OCIRE** | `ocire/` | The independent recovery kernel. An L0 *peer* of OCOS - own include paths, own linker script, no OCOS headers (the WP-01 independence rule). Own ATA driver, own FAT12/32 code, own SHA-256, own console, own IDT, own ACPI, own 6300ESB watchdog. Boots as supervisor (`ocire.mode=sup`) or full recovery shell, kexecs OCOS, and repairs the machine when OCOS cannot boot. |

Only three things are shared source-side: `include/ocbl_state.h` (the
byte-exact boot-state contract used by all of OCBL, OCOS, OCIRE), the
Chinese font data (OCIRE's `ocire/generated/ocire_cn_font_data.c` is
generated from `ocse/chinese/chinese_font_data.c`), and the L1 ABI that
the OCIRE log agent compiles against (an ABI, not an include).

## Collaboration diagram

```
 power-on
    |
    v
 +--------------------------------------------------------------+
 | OCBL  (BIOS stage1 @0x7C00 -> core @0x7E00  /  UEFI PE app)  |
 |  * 6-class volume scanner: OCSE OCOS OCIRE WINDOWS LINUX     |
 |    GENERIC  (+ synthesized Shutdown row)                     |
 |  * 3-key 10 s menu (Up/Down/Enter only), serial-mirrored     |
 |  * A/B slot judgment on the first OCOS volume (flag files)   |
 |  * boot-state monitor: reads 0x98000/0x9B000, arms 6300ESB   |
 |  * auto-route: pending BOOT_FAILED record -> OCIRE one-shot  |
 +----+----------------+----------------+----------------+------+
      |                |                |                |
   OCOS             OCIRE             OCSE          foreign VBR/ESP
      |                |                |                |
      v                v                v                v
 mb2 handoff      mb2 handoff      mb2 handoff      chainload
 EAX=0x36D76289   EAX=0x36D76289   EAX=0x36D76289   (0x7C00 jump /
 EBX=MBI@0x50000  EBX=MBI@0x50000  EBX=MBI@0x50000   LoadImage)
 32-bit, pg off    32-bit, pg off   32-bit, pg off
      |                |                |
      |         OCIRE kexec           +--> installs OCOS disks:
      |         OCOS ELF -> [1MiB,8MiB)     ocbl-install writes the
      |         MBI rebuilt @0x80000        OCBL MBR flavor, mkfs.fat32,
      |         agent module appended       tar payload, boot flags
      |                |
      v                v
 +-------------+  +--------------------+
 | OCOS kmain  |  | OCIRE supervisor   |
 | boot chain  |  | mailbox WAIT_SIGNAL|
 | 1/4 kernel  |  | arm wdt 60 s       |
 | 2/4 drivers |  | kexec OCOS ------->|  (OCOS boots with
 | 3/4 service |  +--------------------+   oc.* embed facts)
 | 4/4 iface   |
 | L1 ush      |  OCOS first seconds:
 |  ...        |   ocembed_boot_confirm(): mailbox SIGNAL_OK,
 +-------------+   disarm i6300esb, spawn ocire_guard agent,
                   write-protect the whole recovery disk.
```

The reverse edges matter as much as the forward ones: when OCOS dies
during boot, the 6300ESB watchdog resets the machine, OCBL reads the
0x9B000 post-mortem record, judges the failure from the 0x98000 stage
island, and routes the next boot into OCIRE without any menu interaction
(the full chain is [crash-recovery.md](crash-recovery.md)).

## The version chain

One `OC_RELEASE_VERSION` and one `OC_KERNEL_BUILD` flow into all four
banners - the four components are released in lockstep by construction
(`Makefile` lines 90/103-104):

```make
OC_RELEASE_VERSION ?= 3.0.0-NAK
OC_KERNEL_BUILD_OFFSET := 21
OC_KERNEL_BUILD ?= $(shell expr $$(git rev-list --count HEAD ...) + 21)
```

| Component | Version stamp source | Notes |
|---|---|---|
| OCOS | `-D OC_RELEASE_VERSION/-D OC_KERNEL_BUILD` + `ocos/kernel/generated/version_gen.h` (`OC_VERSION_STAMP`, cmp-guarded so a version bump forces recompilation - the `-D` flags are invisible to `-MMD` dependency tracking, a live-caught stale-banner bug) | Banner: `Open Cube OS  [3.0.0-...]` |
| OCBL | `-DOCBL_RELEASE=\"$(OC_RELEASE_VERSION)\"` + `-DOCBL_BUILD=$(OC_KERNEL_BUILD)` (`ocbl/ocbl.mk`) | Fallback literals `3.0.0-WP05`/`0` in sources; no independent version |
| OCIRE | `OCIRE_VERSION ?= $(OC_RELEASE_VERSION)` + `ocire/generated/ocire_version_gen.h` (`OCIRE_STAMP`) | BUG #92 fix - it used to carry its own literal four milestones behind |
| OCSE | `-DOCSE_RELEASE=\"$(OC_RELEASE_VERSION)-ocse\"` + `OCSE_BUILD=$(OC_KERNEL_BUILD)` | The **only** component with a distinct suffix (`-ocse`), because it is a vendored v1.0.0 kernel wearing the current release number |

The `+21` offset keeps the public build sequence monotonic above the
already-published build 274. Per-component WP tags (WP-05, WP-07a, ...)
exist only in file-header comments, never in shipped banners.

## The physical memory contract

The four components never share code, but they share physical addresses.
These are load-bearing constants (defined in `ocbl/common/ocbl_boot.h`,
`ocbl/common/ocbl_types.h`, `include/ocbl_state.h`, the three linker
scripts):

```
   0x00500-0x0051F   OCBL A20 test area
   0x00520           stage1 facts: {boot drive, flavor, entry CS}
   0x007C00          OCBL stage1 (512 B, both flavors)
   0x007E00..        OCBL core (linker_bios.ld; entry core_head)
   0x30000           64 KiB read bounce window; EFI: 64->32 handoff blob
   0x40000-0x4FFFF   real-mode trampoline + VBE/geom scratch
                     (fallback PT block when the EBDA overlaps 0x8F000+)
   0x50000           multiboot2 info build buffer (OCBL_MBI_BASE;
                     OCIRE kexec rebuilds its MBI at 0x80000 instead)
   0x60000-0x61FFF   E820 / GetMemoryMap buffer
   0x62000 / 0x62800 OCBL fs sector window / 128 KiB FAT cache
   0x82800-0x8DFFF   OCBL core stack (top 0x8E000 - WP06f VMware fix)
   0x8F000-0x94FFF   OCBL identity page tables (2 MiB huge pages, 4 GiB)
   0x98000  4 KiB    OCST state area      magic "OCS1" - kernel stage/
                      heartbeat, blind-written by OCOS, read by OCBL/OCIRE
   0x9B000  4 KiB    OCST reserved area   magic "OCB1" - post-mortem
                      boot record (BOOT_FAILED / USER_REQUEST)
   0x9FC00            EBDA (SeaBIOS)

 Payload placements:
   0x100000 (1 MiB)   OCOS kernel PT_LOADs (link base). OCSE too
                      (KERNEL_LMA 0x100000). Both images must stay
                      below 0x400000 (linker ASSERT in both scripts).
   0x400000-0x600000  the "identity window" - user address spaces remap
                      this virtual range to an empty PT, so the kernel
                      image may never grow into it (see memory.md)
   0x800000           mb2 "self" module: raw copy of the OCOS kernel ELF
                      (the installer payload pattern)
   0x01000000 (16 MiB) OCIRE link base (OCIRE_LMA) - disjoint from the
                      OCOS 1 MiB range so the supervisor's kexec of OCOS
                      into [0x100000, 0x800000) cannot overwrite itself
```

Both monitor islands are carved out as **E820 reserved** in the mb2
memory map OCBL builds, so no OS ever allocates them; they were
live-measured to survive QEMU warm resets (that is the whole point - a
watchdog reset must not erase the evidence). The full story of these two
blocks is [hidden-mechanisms.md](hidden-mechanisms.md), the byte layout
lives in `include/ocbl_state.h`.

## Boot modes

OCBL supports eight distinct paths out of `ocbl_menu_run()`; the first
five are the "boot modes" proper:

1. **BIOS / El Torito (ISO)** - the whole El Torito image is preloaded
   by SeaBIOS; stage1 at 0x7C00 finds the core already at 0x7E00, prints
   `OCBL stage1 ok`, enables A20 (int 15h AX=2401, then port 0x92, with
   two independent alias write-tests - both must fail before A20 is
   declared off), and jumps through its own GDT into 32-bit
   `core_head`.
2. **BIOS / MBR (installed disk)** - the silent 512-byte stage1 variant.
   A patch block at image offset `+0x1A0` (`'OCBL'` magic, core LBA,
   sector count - written by `ocbl-install`) drives int 13h AH=42h reads
   of the core into 0x7E00. The retry loop deliberately never resets the
   drive: a reset clears SeaBIOS's PIO-fallback state and loops forever
   (live-caught WP06b b5). Bytes 446..509 stay zero in the image so the
   real partition table can be copied over them at install time.
3. **UEFI** - `BOOTX64.EFI`, a PE/COFF app built by the in-tree
   `ocbl/elf_to_efi.py` converter (complete `.reloc` directory derived
   from `ld --emit-relocs`; the plain `ld -m i386pep` output emitted a
   partial directory and OVMF mis-relocated it). Same scanner, menu and
   handoff, with GOP/GetMemoryMap/ExitBootServices substitutions and a
   64->32 downgrade blob at 0x30000.
4. **VBR chainload (BIOS)** - foreign Windows/Linux/generic volumes:
   read the volume boot record, verify 0xAA55, copy to 0x7C00, drop to
   real mode and jump (the GRUB `chainloader +1` contract). An ESP that
   only carries `BOOTX64.EFI` is **refused honestly** ("needs the UEFI
   flavor").
5. **UEFI chainload** - `LoadImage`/`StartImage` of the foreign ESP
   loader, boot services kept alive, OVMF watchdog disarmed first.
6. **Shutdown** - ACPI S5 through OCBL's own FADT/\_S5 walk.
7. **Auto-recovery route** - a pending BOOT_FAILED record makes OCBL
   boot the OCIRE entry one-shot, no menu interaction.
8. **A/B slot select** - flag-file protocol on the first OCOS volume.

### A/B slots and automatic rollback

The slot layout (`tools/make_ab_disk.sh`, documented in
[ota-ab.md](ota-ab.md)): partition 1 = FAT32 flags volume carrying
`/boot/next_B`, `/boot/bootfail_B`, `/boot/ok_B`; partition 2 = slot A;
partition 3 = slot B. OCBL's `judge_volume()` implements the same
semantics as `grub.cfg` and `grub-ab.cfg` (byte-identical flag contract
in all three implementations):

- `next_B` present AND `bootfail_B` absent -> boot partition 3 (slot B);
- `next_B` AND `bootfail_B` both present -> boot partition 2 (slot A),
  label suffix `" rollback"` - **the automatic rollback**: a slot-B
  kernel writes `bootfail_B` pessimistically at boot and removes it only
  when `ota_update_confirm_boot()` declares the system fully up, so a B
  kernel that dies mid-boot rolls the machine back to A on the next
  reset;
- `ok_B` present -> slot B;
- otherwise -> slot A.

The chosen slot's volume is re-mounted and the entry re-points at it;
partitions 2/3 of that disk are "slot-consumed" and cannot register
their own entries. A volume carrying both `/boot/opencube.elf` and
`/boot/ocire.elf` exposes the OCIRE recovery entry too (WP06g3-4.4 -
the scanner's short-circuit used to hide it).

## Where to go next

- The loader chain, scanner classes and handoff details:
  [boot-flow.md](boot-flow.md)
- What lives in every directory of the tree: [tree-map.md](tree-map.md)
- The cross-component mechanisms (state islands, watchdog relay, guard
  channel): [hidden-mechanisms.md](hidden-mechanisms.md)
- The ring-3 API contract: [private-headers.md](private-headers.md)
- OCIRE in depth: [../dev/ocire.md](../dev/ocire.md), and the
  user-facing side: [../manual/boot-modes.md](../manual/boot-modes.md),
  [../manual/recovery.md](../manual/recovery.md)
