<!-- SPDX-License-Identifier: Apache-2.0 -->
# Boot flow, stage by stage

The release ISO boots through **OCBL**, the in-tree boot loader - GRUB
left the release chain in 3.0.0-WP05 B5-7 (it survives only as the
legacy `grub.cfg`/`grub-ab.cfg` disk contract and as the embedded
boot-data payload for the in-system `install`/`abdisk`/`grub-install`
commands). This page walks the whole chain: stage1 -> OCBL core ->
scanner -> menu -> multiboot2 handoff -> kernel entry -> kmain. The
component panorama is [system-map.md](system-map.md); the state
monitor's role is [hidden-mechanisms.md](hidden-mechanisms.md).

## Stage 1 - the 512-byte sector (`ocbl/bios/stage1.asm`)

One source, two flavors: `-D OCBL_ELTORITO` (ISO; the whole image is
preloaded, core already at 0x7E00, prints `OCBL stage1 ok`) and `-D OCBL_MBR` (installed disk; deliberately SILENT - the 512-byte budget
follows the GRUB boot.img / ISOLINUX stage1 pattern).

- Entry at 0x7C00; both `0000:0x7C00` and `0x07C0:0x0000` entry forms
  accepted via a `call .ip / pop ax` self-address check; anything else
  halts with a serial message.
- Writes `{boot drive, flavor, entry CS}` to linear 0x520..0x523 for
  the 64-bit core.
- **MBR flavor**: raw-reads the core to 0x7E00 with int 13h AH=42h
  using the patch block OCBL-install writes at image offset `+0x1A0`:
  `u32 magic 'OCBL'`, `u32 core_lba_lo`, `u32 core_lba_hi`, `u16 core_sectors`. The retry loop (12 attempts, ISA pause port 0x1ED)
  **never resets the drive** - a reset clears SeaBIOS's PIO-fallback
  state and loops forever (live-caught WP06b b5).
- A20: int 15h AX=2401 then port 0x92 (bit 0 = fast reset masked off);
  verification is two independent write patterns (orig^0x55,
  orig^0xAA) against the 0xFFFF:0x0510 alias - OFF only when BOTH
  alias tests fail (single-coincidence distrust, the B1 fix). The
  keyboard-controller method is intentionally absent (declared).
- GDT at 0x7C00 (0x08 code32, 0x10 data32), `cr0 |= 1`,
  `jmp 0x08:0x7E00`.
- Bytes 446..509 are zero in the image - `ocbl-install` copies the
  real partition table over them; 510..511 = 0xAA55.

## Stage 2 - the OCBL core, BIOS flavor (`ocbl/bios/core_head.S`)

32-bit entry `core_head` at 0x8200 (the image links at 0x7E00 via
`ocbl/linker_bios.ld`, `.text.head` first): flat segments, own GDT
(32+64-bit code), zero .bss, identity page tables (PML4@0x8F000 ->
PDPT@0x90000 -> PD@0x91000..0x94000, 2 MiB huge pages = 4 GiB),
PAE+LME+PG, far jump to 64-bit, stack top 0x8E000 (the WP06f VMware
fix - the old 0x9F000 collided with VMware's larger EBDA). The **EBDA
guard** (#134): reads BDA 0x40E; on overlap it relocates the 6-page PT
block to 0x40000.

All firmware services run through the real-mode trampoline
(`tramp16.asm` blob copied to 0x40000; `ocbl_bios_int(int_no, regs*)`
is the 64-bit wrapper - reg block at 0x40300, far jump to selector
0x08 -> 0x40000, resume via saved RIP/RSP). Backends built on it: int
10h VGA text console, int 1Ah clock (54.9 ms ticks), int 16h PS/2 +
int 14h serial keys, int 13h disks (AH=48h geometry, AH=42h extended
reads, drives 0x80..0x8F + boot CD 0xE0+), int 15h E820 (stored
multiboot2-shaped at 0x60000), VBE (mode enumeration + LFB set;
ModeInfoBlock offsets live-verified against SeaBIOS), RSDP scan (EBDA
then 0xE0000-0xFFFFF, 20/36-byte checksums), and the VBR chainload.

## Stage 2' - the OCBL core, UEFI flavor (`ocbl/efi/`)

Built as an intermediate ELF (`ocbl/linker_efi.ld`, `.` = 0x11000 =
PE ImageBase 0x10000 + RVA 0x1000 - "every link-time absolute value
equals its final virtual address") and converted by the in-tree
`ocbl/elf_to_efi.py`, which derives the **complete** `.reloc` directory
from `ld --emit-relocs` (plain `ld -m i386pep` emitted a partial
directory -> OVMF mis-relocation -> the wrong protocol GUID matched ->
#UD; documented live-hunt).

`efi_main` (MS x64 ABI): zero .bss by hand (EDK2 does not guarantee
the raw-vs-virtual slack - live-caught `g_ndisks` = 2.9 billion);
reserve the fixed windows with `EFI_ALLOCATE_ADDRESS` (0x30000/0x10000,
0x50000/0x10000, 0x60000/0x23000 - collision is a FATAL halt because
the layout would corrupt firmware data); statemon window reserve
(failure only disables the monitor + `oc.stmon=off`); BlockIo disk
init (GUID 964e5b**21** - not the SimpleFileSystem ...22 one-digit
trap, live-caught); then the same scan/menu/handoff as BIOS, with GOP
instead of VBE, GetMemoryMap -> E820 instead of int 15h, and an
ExitBootServices retry loop (3 attempts) before the 64->32 handoff
blob at 0x30000. OVMF's 5-minute default watchdog is disarmed
(`set_watchdog_timer(0,...)`) before EBS.

## Stage 3 - the six-class scanner (`ocbl/common/ocbl_scan.c`)

`ocbl_scan_all()` enumerates drives (BIOS int-13 ranges / UEFI BlockIo
handles), gates CD-likes to 2048-byte sectors, and per drive runs a
partition sweep then a whole-disk "superfloppy" fallback (the OCIRE 16
MiB 0xEE-shadow volume decodes this way when the 0xEE entry itself is
the volume). `judge_volume()` short-circuit priority:
**OCSE -> OCOS -> OCIRE -> WINDOWS -> LINUX -> GENERIC**:

| Class | Detection |
|---|---|
| OCSE | ISO9660 volume carrying `/ocse.mrk` (the 8.3-safe marker; judged FIRST - an install medium never boots as OCOS even with a kernel on it). Path `/boot/ocse.elf` |
| OCOS | Any volume with `/boot/opencube.elf` whose first 8 KiB contain mb2 magic `0xE85250D6`. Marker-less ISOs register too |
| OCIRE | Any volume (incl. the 0xEE shadow and ISOs) with `/boot/ocire.elf` |
| WINDOWS | `/EFI/Microsoft/Boot/bootmgfw.efi` |
| LINUX | ESP with `/EFI/<vendor>/{grubx64,shimx64,systemd-bootx64}.efi` (the vendor dir names the label; `BOOT` skipped -> falls to GENERIC) |
| GENERIC | `/EFI/BOOT/BOOTX64.EFI` |
| SHUTDOWN | Synthesized, always last, can never scroll away |

Declared limit: no ext2/3/4 driver - Linux/Windows are detected via
their ESP, never via ext/NTFS roots. A volume carrying BOTH
`opencube.elf` and `ocire.elf` exposes the OCIRE recovery entry too
(WP06g3-4.4 - the short-circuit used to hide it, "postmortem disaster
4"). The read-only volume layer behind the scanner
(`ocbl/common/ocbl_fs.c`) is FAT12/16/32 with LFN read + ISO9660,
sharing a 2 KiB sector window at 0x62000 and a 128 KiB FAT cache at
0x62800. Partition traversal (`ocbl_part.c`) knows MBR, GPT and
**SHADOW** (MBR with one type-0xEE entry and NO GPT header - the OCIRE
recovery-volume shape; 0xEE does not blindly mean "read the GPT").

### The A/B judgment chain (native, inside `judge_volume()`)

When the first OCOS volume of a disk carries any of `/boot/next_B`,
`/boot/bootfail_B`, `/boot/ok_B` (partition 1 = flags volume):

- `want = (next_b && !bootfail) || (!next_b && ok_b) ? 3 : 2` -
  **partition 3 = slot B, partition 2 = slot A**;
- `next_B && bootfail_B` -> slot A, label suffix `" rollback"`
  (**automatic rollback**: a slot-B kernel writes `bootfail_B`
  pessimistically at boot and only a fully-confirmed boot removes it);
- the chosen slot volume is re-mounted and the entry's
  drive/base_lba/fs re-pointed at it (`e->slot` = 1=A / 2=B / 0=plain);
- partitions 2/3 of that disk are "slot-consumed" - they cannot
  register their own entries.

The identical flag semantics exist in `grub.cfg` (ISO medium),
`grub-ab.cfg` (installed disk) and OCBL-native - three implementations,
one contract. [ota-ab.md](ota-ab.md) continues the story on the OCOS
side.

## Stage 4 - the 3-key menu (`ocbl/common/ocbl_menu.c`)

80x25 text menu: rows 0 title / 1 hint / 2 countdown / 4..22 a
scrolling 19-row window. **Exactly three keys are live: Up (0x48),
Down (0x50), Enter (0x1C)** - everything else is consumed and ignored
(the contract in `ocbl_menu.h`; there are no hidden keys). The
countdown is `OCBL_MENU_TIMEOUT_SEC = 10`; Up/Down cancel it
("(timer cancelled)"). The Shutdown row is synthesized last and can
never scroll away. The menu is fully drivable over **serial** (a
complete ANSI mirror of the VGA menu - inverse video, cursor
addressing; CSI arrows `ESC [ A/B`, `\r`/`\n` Enter) which is how
headless automation selects entries (`ocbl/menu_drive.py` drives QEMU
monitor sendkey with a serial `MENU-READY` poll - PS/2 is THE injection
path; CSI bytes via tcp socket arrive too late). Every menu heartbeat
kicks the 6300ESB watchdog.

Before the menu runs, `ocbl_run()` has already: printed the banner
(`OCBL_RELEASE` + `OCBL_BUILD`, CPUID brand, stack), run the B2a probe
(trampoline + E820 dump + drive sweep + CD PVD at LBA 16 / own boot
image at LBA 34), run the **statemon boot check** (may judge the
previous boot failed and reset - not return), scanned and dumped all
volumes, probed/armed the watchdog 60 s, and consulted
`ocbl_statemon_route_pending()` - a pending BOOT_FAILED record makes
the OCIRE entry auto-selected one-shot.

## Stage 5 - loading and the multiboot2 handoff (`ocbl/common/ocbl_load.c`)

`ocbl_load_elf(v, path, want_base, &size)` reads the ELF64 header + up
to 32 program headers through the 0x30000 bounce window; every PT_LOAD
is copied to its `p_paddr` and zero-filled to `p_memsz`; segments
outside `[want_base, 0x40000000)` are refused; the BUG #46 fix made the
`p_offset`/`p_filesz`/`mem_end` bounds overflow-safe (the old unchecked
forms allowed near-2^64 values to load attacker bytes or zero the
whole window). OCOS and OCSE load at 0x100000; OCIRE at 0x1000000.
`ocbl_load_raw()` whole-file copies the "self" module - the raw kernel
ELF placed at 0x800000 (the installer payload pattern: the running
system can install itself onto other disks from module memory).

`ocbl_build_mbi()` writes a GRUB-shaped multiboot2 info at **0x50000**:
(1) cmdline `oc.slot=A|B|ISO` plus statemon extras (e.g. ` oc.hang=N`
from the `/HANGAT` probe - see [hidden-mechanisms.md](hidden-mechanisms.md));
(2) bootloader name "OCBL 3.0.0"; (3) module "self" [start,end] for
OCOS; (4) meminfo; (6) mmap with 24-byte entries - **the state monitor
pages [0x98000,0x9C000) are carved out as E820 reserved**
(split-emit below/overlap/above); (8) the 40-byte RGB framebuffer tag;
(15/14) the RSDP copy (36 B rev>=2 / 20 B rev1); (0) end.

Handoff contract (identical to GRUB, so the kernels' `boot.S` cannot
tell the difference): 32-bit protected mode, paging OFF, GDT may be
ours, `EAX=0x36D76289`, `EBX=MBI physical`, jump to the kernel's
32-bit entry. BIOS flavor: in-place 64->32 downgrade. EFI flavor:
ExitBootServices, then the handoff blob at 0x30000.

## Stage 6 - OCOS entry (`ocos/kernel/`)

`core_boot_entry.S` (`_start`): loads its own GDT, builds boot page
tables (PML4 + 4 PDPT entries + 4 PD tables of 2 MiB huge pages = 4
GiB identity map), enables PAE+LME+NXE+PG+WP, far-jumps to
`long_mode_start` (`core_boot_longmode.S`), which reloads 64-bit data
selectors, sets `rsp = __boot_stack_top` (16 KiB boot stack in .bss)
and calls `kmain(magic, mbi)` - with the mb2 magic kept in `edi` and
the mbi pointer in `esi` from the handoff. The multiboot2 header
(`core_boot_mb2header.S`, `.rodata.mbthdr` first in the image)
requests an **800x600x32 RGB framebuffer** - OCOS hard-fails without
the fb tag (which is why OCBL sets VBE/GOP mode before handoff, and
why OCIRE's kexec hands the tag over verbatim).

`arch_multiboot2.c` parses the tag list (cmdline, bootloader name,
modules - including the "self" module lookup, meminfo, mmap,
framebuffer, RSDP tags 14/15) hostile-input hardened: total_size
bounds, per-tag minimums, string NUL-termination, truncated-tag
rejection (the BUG-0142 chain).

## Stage 7 - kmain, the composition root (`core_boot_main.c`)

In boot-log order, with the printed markers:

1. COM1 serial 115200 8N1 + serial-encoder tee.
2. mb2 magic validation + mbi parse (fail -> serial `FATAL:` + hlt).
3. `boot_state_early_init(cmdline)` - the state monitor takes over the
   6300ESB watchdog, writes stage 1 `KM_AIN_ENTERED`.
4. `screen_fb_init` (RGB 32bpp only; masks from the tag handle BGRX
   BIOS-VBE and RGBX UEFI-GOP) -> console -> CJK renderer ->
   `lib_log_init` -> `klog_init`. "boot chain 1/4: kernel core
   mechanisms".
5. IDT/GDT/TSS (256 gates), 8259 remap, PIT 100 Hz, PS/2 keyboard,
   COM1 RX -> key queue, line editor, `sti`, exception self-test
   (#DE/#UD/#PF caught, kernel alive), PMM/VMM/heap, `klog_grow()`
   (the 24-entry bootstrap ring becomes the 1024-entry heap ring -
   nothing is lost).
6. scheduler + sync + usermode; **registry collect**
   (`drv/svc/core_registry` - logs `registry: drv=N svc=N app=N`);
   ACPI/AML/PCIe/watchdog-probe/i2c/RTC/HPET.
   "boot chain 2/4: drivers + driver-free services", stage 2
   `SCHED_READY`.
7. VFS + ramfs (+ klog dump to /var/log/kernel.log), block cache +
   probe, SCSI, USB probe + resident poll thread; FAT32/exFAT/ext4
   registries, config, PM timer, A/B slots, wireless/SDIO/Bluetooth,
   camera/legacy ports, LAPIC, net stack + 10 ms timer, health
   sampler, audio probe. "boot chain 3/4: driver-dependent services",
   stage 3 `DRIVERS_READY`.
8. Crash infrastructure armed (`core_dump_alloc` once up-front, panic
   handlers, crashlog, soft watchdog - [crash-recovery.md](crash-recovery.md));
   optional async update check; `ota_update_confirm_boot()` (clears the
   slot-B bootfail marker - the other end of the A/B chain);
   `ocembed_boot_confirm()` on supervised boots (mailbox SIGNAL_OK,
   i6300esb disarm, whole-disk write protect, ocire_guard spawn).
   "boot chain 4/4: interfaces ready", stage 4 `SERVICES_READY`.
9. "L0 ready, waiting for L1" -> `l1_launcher_boot_default()` spawns
   the ring-3 **ush** shell (the default `OC_L1_APP_DECL` entry in
   `core_boot_l1launch.c`), marks stage 5 `L1_LAUNCHED`, and parks the
   boot task in a yield loop; kmain ends in `for(;;) hlt`. The FIRST
   syscall from ush flips stage 6 `L1_READY`.

The scanner test fleet (`ocbl/make_scan_disks.py`) locks this chain
down: hd0 (MBR 3-part A/B FAT32 OCOS flags volume -> slot B entry),
hd1 (0xEE shadow FAT12 OCIRE), hd2 (Windows ESP LFN), hd3 (ubuntu
ESP), hd4 (generic ESP), fat0 (superfloppy FAT16), bare0 (garbage -> 0
entries), cd0.iso (per-content). The chainload fleet adds chain0 (an
ESP whose BOOTX64.EFI is OCBL itself - the self-chainload proof) and
chain1 (a non-PE stub that must be refused with EFI_LOAD_ERROR).

For the user-facing side of the same story:
[../manual/boot-modes.md](../manual/boot-modes.md); for OCSE's own
24-stage ladder and OCIRE's supervisor decision loop:
[system-map.md](system-map.md) and [../dev/ocire.md](../dev/ocire.md).

## WP-07: the recovery disk's own boot (dual-slot selection)

The WP-07 recovery disk is SELF-booting: OCBL's stage1 + core live
at LBA 0 of the recovery disk itself (the `+0x1A0` boot patch block
carries the core LBA/count, the same installer contract), so the
machine boots straight into the loader without any other medium.

The scan's OCIRE branch (`judge_volume`) handles the dual-slot
shape: a disk whose first two partitions BOTH look like OCIRE slots
(FAT32 + `/boot/ocire.elf` + `/mailbox.bin`) is recognized through
the OCIREAB1 flags sector at LBA 1024; OCBL picks the flags-selected
slot (a pending `try_slot` gets the boot with its attempt counter
bumped; after two failed loads the try is abandoned and the active
slot boots), mounts THAT partition and registers exactly ONE OCIRE
entry (the second slot volume is consumed). Legacy single-volume
media (the old 0xEE FAT12 shadow, single FAT32 volumes, the ISO)
still register the plain way.

If the selected slot's kernel will not load, `ocbl_run()` retries
the OTHER slot immediately and rewrites the flags so the next boot
starts from the survivor - the anti-brick guarantee.

UEFI note: the EFI flavor's BlockIo drives are numbered FROM ZERO
(the BIOS flavor uses 0x80+), so every "drive seen" sentinel in the
common scanner uses 0xFF, never 0.
