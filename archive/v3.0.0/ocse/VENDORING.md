# OCSE vendoring record (WP-06a)

OCSE (OpenCubeOS Setup Environment) is the installer pre-boot
environment: a small standalone kernel launched by OCBL as a multiboot2
ELF, providing hardware detection, a GUI + CLI installer front-end and
disk identification before OpenCubeOS itself is installed.

## Base

Vendored from tag **v1.0.0** (commit bdc47047) - the last pre-restructure
monolithic kernel. Rationale:

* OCSE runs before any OCOS service exists; the WP-03+ service tree
  (L0/L1 split, OCIRE embed, OTA, net stack) is installer dead weight.
* v1.0.0 already contains every driver an installer needs:
  framebuffer+font+console, USB HID (mouse/keyboard), i2c/SMBus
  (touchpad), ATA block layer + partition scan, PCI, ACPI incl. the
  AML interpreter (power ops) and HPET/TSC/CMOS time.
* A vendored copy (not a shared build) keeps OCSE frozen against
  future OCOS churn: the installer image must keep booting even as the
  main kernel evolves.

## Procedure

1. `tools/vendor_ocse.sh` - re-creates `ocse/` from the pristine
   v1.0.0 sources (143 files, this script is the file-list source of
   truth).  WP06f: OCSE-native additions inside the vendored subtrees
   (the PS/2 mouse driver, the regenerated Chinese font data) are
   stashed and restored automatically; on a fresh bootstrap a missing
   font data file must be regenerated with the FIXED
   `tools/gen_chinese_font.py` (see `tools/ocse_edits.py` E20).
2. `tools/ocse_edits.py` - applies every modification on top (E1-E20;
   never edit vendored files by hand: a re-vendor would
   erase them).
3. WP06f: `ocse/chinese/chinese_font_data.c` is GENERATED data from
   `tools/gen_chinese_font.py` (committed output, same precedent as
   `drivers/display/screen_font_data.c`).  The v1.0.0 tag carries the
   pre-WP06f glyph data (baseline-clipped strokes); never restore it.

## Excluded on purpose (v1.0.0 areas not vendored)

kernel/ota, kernel/crypto, net/*, shell/, tui/, userprogs/, libs/ld_so,
drivers/{bluetooth,camera,nic,scsi,sd,snd,wireless,misc},
drivers/usb/{serial,audio,msc} entries, kernel/core/{syscall,usermode,
g7test,g8test,userprogs_data}, boot/grub_*, l1/ (all but l1_ext and
l1_selftest), pinyin/IME data.

## Layout

`ocse/` mirrors the v1.0.0 tree layout (kernel/, drivers/, fs/, boot/,
l1/, chinese/). OCSE-specific new code lives at the ocse/ top level
(ocse_main.c, ocse_detect.c, ocse_i18n.c, ocse_disk.c, ocse_gui.c,
ocse_cl.c) plus linker_ocse.ld / ocse.mk.
