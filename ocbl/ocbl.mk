# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - 3.0.0-WP05-ocbl
# File: ocbl/ocbl.mk
# Purpose: build targets for the OCBL boot loader (the GRUB replacement).
#
# Included once from the top-level Makefile (x86_64 branch, right after
# ocire.mk).  OCBL is INDEPENDENT of both kernels: it compiles only
# ocbl/** sources with freestanding flags and produces its own boot
# images.  The UEFI flavor (ocbl_x64.efi) arrives in a later batch.
#
# Targets:
#   make ocbl-bios        - BIOS flavor boot image (build/ocbl/ocbl_bios.bin)
#   make ocbl-iso         - standalone OCBL test ISO (BIOS boot, no payload)
#   make ocbl-run-bios    - boot the test ISO in QEMU (SeaBIOS, serial stdio)

OCBL_DIR := $(OC_ROOT)/ocbl
OCBL_OUT := $(BUILD)/ocbl

$(OCBL_OUT):
	@mkdir -p $@

OCBL_CC := $(CC)
OCBL_CFLAGS := -ffreestanding -fno-stack-protector -fno-pie -fno-pic \
               -mno-red-zone -mno-sse -mno-mmx -mno-3dnow -mcmodel=small \
               -Wall -Wextra -Werror -O2 -std=gnu11 \
               -I$(OCBL_DIR)/common -I$(OCBL_DIR)/bios \
               -DOCBL_FLAVOR_BIOS \
               -DOCBL_RELEASE="\"$(OC_RELEASE_VERSION)\"" \
               -DOCBL_BUILD=$(OC_KERNEL_BUILD)

OCBL_COMMON_C := $(wildcard $(OCBL_DIR)/common/*.c)
OCBL_COMMON_H := $(wildcard $(OCBL_DIR)/common/*.h)
OCBL_OBJ := $(patsubst $(OCBL_DIR)/%.c,$(OCBL_OUT)/%.o,$(OCBL_COMMON_C))

# ---- BIOS flavor backend implementations (B5-2 split: the int 13h /
#      VGA / E820 / VBE / VBR-chain / RSDP-scan units moved from common
#      to bios/; common is now firmware-agnostic and the UEFI flavor
#      provides its own versions of the same contracts) ----
OCBL_BIOS_IMPL_C := $(wildcard $(OCBL_DIR)/bios/*.c)
OCBL_BIOS_IMPL_H := $(wildcard $(OCBL_DIR)/bios/*.h)
OCBL_BIOS_IMPL_OBJ := $(patsubst $(OCBL_DIR)/bios/%.c,$(OCBL_OUT)/bios_impl/%.o,$(OCBL_BIOS_IMPL_C))

$(OCBL_OUT)/bios_impl/%.o: $(OCBL_DIR)/bios/%.c $(OCBL_COMMON_H) $(OCBL_BIOS_IMPL_H) | $(OCBL_OUT)
	@mkdir -p $(OCBL_OUT)/bios_impl
	$(OCBL_CC) $(OCBL_CFLAGS) -c $< -o $@

# ---- stage 1 (two flavors from one source) ----
$(OCBL_OUT)/stage1_eltorito.bin: $(OCBL_DIR)/bios/stage1.asm | $(OCBL_OUT)
	@mkdir -p $(OCBL_OUT)
	$(NASM) -f bin -D OCBL_ELTORITO -o $@ $<
	python3 -c "import sys; b=open('$(abspath $@)','rb').read(); assert len(b)==512, 'stage1 eltorito %d bytes' % len(b); print('ocbl: stage1 eltorito 512B ok')"

$(OCBL_OUT)/stage1_mbr.bin: $(OCBL_DIR)/bios/stage1.asm | $(OCBL_OUT)
	@mkdir -p $(OCBL_OUT)
	$(NASM) -f bin -D OCBL_MBR -o $@ $<
	python3 -c "import sys; b=open('$(abspath $@)','rb').read(); assert len(b)==512, 'stage1 mbr %d bytes' % len(b); print('ocbl: stage1 mbr 512B ok')"

# ---- real-mode trampoline (flat binary, embedded as data) ----
$(OCBL_OUT)/tramp16.bin: $(OCBL_DIR)/bios/tramp16.asm | $(OCBL_OUT)
	$(NASM) -f bin -o $@ $<
	python3 -c "b=open('$(abspath $@)','rb').read(); assert len(b)<=0x300, 'tramp blob %d B overflows the 0x100 code area' % len(b); print('ocbl: tramp16.bin %dB ok' % len(b))"

$(OCBL_OUT)/bios/tramp_data.o: $(OCBL_DIR)/bios/tramp_data.asm $(OCBL_OUT)/tramp16.bin | $(OCBL_OUT)
	@mkdir -p $(OCBL_OUT)/bios
	$(NASM) -f elf64 -I$(OCBL_OUT)/ -o $@ $<

$(OCBL_OUT)/bios/ocbl_bios_asm.o: $(OCBL_DIR)/bios/ocbl_bios_asm.S | $(OCBL_OUT)
	@mkdir -p $(OCBL_OUT)/bios
	$(NASM) -f elf64 -o $@ $<

# ---- core (32-bit head + 64-bit C) ----
$(OCBL_OUT)/bios/core_head.o: $(OCBL_DIR)/bios/core_head.S | $(OCBL_OUT)
	@mkdir -p $(OCBL_OUT)/bios
	$(NASM) -f elf64 -g -F dwarf -o $@ $<

$(OCBL_OUT)/common/%.o: $(OCBL_DIR)/common/%.c $(OCBL_COMMON_H) | $(OCBL_OUT)
	@mkdir -p $(OCBL_OUT)/common
	$(OCBL_CC) $(OCBL_CFLAGS) -c $< -o $@

OCBL_BIOS_OBJS := $(OCBL_OUT)/bios/tramp_data.o $(OCBL_OUT)/bios/ocbl_bios_asm.o

$(OCBL_OUT)/core.elf: $(OCBL_OUT)/bios/core_head.o $(OCBL_BIOS_OBJS) $(OCBL_OBJ) $(OCBL_BIOS_IMPL_OBJ) $(OCBL_DIR)/linker_bios.ld
	$(LD) -n -nostdlib -T $(OCBL_DIR)/linker_bios.ld \
              -z max-page-size=0x1000 -z noexecstack -o $@ \
              $(OCBL_OUT)/bios/core_head.o $(OCBL_BIOS_OBJS) $(OCBL_OBJ) $(OCBL_BIOS_IMPL_OBJ)
	strip --strip-debug $@
	python3 -c "import sys; b=open('$(abspath $@)','rb').read(); print('ocbl: core.elf %d bytes' % len(b))"

$(OCBL_OUT)/core.bin: $(OCBL_OUT)/core.elf
	objcopy -O binary $< $@

# ---- BIOS boot image = stage1 + core (core padded to a 512 multiple) ----
$(OCBL_OUT)/ocbl_bios.bin: $(OCBL_OUT)/stage1_eltorito.bin $(OCBL_OUT)/core.bin
	python3 $(OCBL_DIR)/build_image.py $@ $(OCBL_OUT)/stage1_eltorito.bin $(OCBL_OUT)/core.bin

# ---- UEFI flavor (B5): a plain PE/COFF x64 application ----
# linked as an intermediate ELF (`ld -m elf_x86_64 --emit-relocs` over
# linker_efi.ld, VMA base 0x10000) and converted to a PE/COFF EFI
# application by our own ocbl/elf_to_efi.py, which derives the
# COMPLETE .reloc directory from the emitted relocations (the plain
# ld i386pep route emitted a partial directory and OVMF silently
# mis-relocated half the image - B5-3 live-caught, see the converter
# header).  mcmodel=large keeps every address reference a 64-bit
# movabs the converter can relocate.
OCBL_EFI_CFLAGS := -ffreestanding -fno-stack-protector -fno-pie -fno-pic \
                   -mno-red-zone -mno-sse -mno-mmx -mno-3dnow -mcmodel=large \
                   -fno-ident -fno-asynchronous-unwind-tables \
                   -Wall -Wextra -Werror -O2 -std=gnu11 \
                   -I$(OCBL_DIR)/common -I$(OCBL_DIR)/efi \
                   -DOCBL_FLAVOR_UEFI \
                   -DOCBL_RELEASE="\"$(OC_RELEASE_VERSION)\"" \
                   -DOCBL_BUILD=$(OC_KERNEL_BUILD)

# flavor-independent common sources the EFI build links (B5-3: the
# scanner chain - fs/part/scan - is firmware-agnostic since B5-2;
# menu/load/acpi/wdt join as their backends land in B5-4/5/6)
OCBL_EFI_SRC := $(wildcard $(OCBL_DIR)/efi/*.c) \
                $(OCBL_DIR)/common/ocbl_serial.c \
                $(OCBL_DIR)/common/ocbl_fs.c \
                $(OCBL_DIR)/common/ocbl_part.c \
                $(OCBL_DIR)/common/ocbl_scan.c
OCBL_EFI_OBJ := $(patsubst $(OCBL_DIR)/%.c,$(OCBL_OUT)/efiobj/%.o,$(OCBL_EFI_SRC))

$(OCBL_OUT)/efiobj/%.o: $(OCBL_DIR)/%.c $(OCBL_COMMON_H) $(OCBL_DIR)/efi/efi.h | $(OCBL_OUT)
	@mkdir -p $(dir $@)
	$(OCBL_CC) $(OCBL_EFI_CFLAGS) -c $< -o $@

# ---- EFI link: ELF with emitted relocs -> our own PE converter ----
# (B5-3 live-caught: `ld -m i386pep` emitted a PARTIAL .reloc
#  directory - only the front of .text got relocation entries, later
#  movabs absolute references kept their link-time values under
#  OVMF's high load address, and HandleProtocol matched the WRONG
#  protocol (the FAT SimpleFileSystem) with the corrupted GUID
#  argument - see elf_to_efi.py's header for the full chain.  The
#  two-step link keeps every R_X86_64_64 and builds the complete
#  directory deterministically.)
$(OCBL_OUT)/ocbl_x64.efi: $(OCBL_EFI_OBJ) $(OCBL_DIR)/elf_to_efi.py $(OCBL_DIR)/linker_efi.ld
	$(LD) -m elf_x86_64 --emit-relocs -n -nostdlib \
	      -T $(OCBL_DIR)/linker_efi.ld -o $(OCBL_OUT)/core_efi.elf \
	      $(OCBL_EFI_OBJ)
	python3 $(OCBL_DIR)/elf_to_efi.py $(OCBL_OUT)/core_efi.elf $@ efi_main
	python3 -c "d=open('$(abspath $@)','rb').read(); pe=int.from_bytes(d[0x3c:0x40],'little'); assert d[:2]==b'MZ' and d[pe:pe+4]==b'PE\x00\x00' and int.from_bytes(d[pe+4:pe+6],'little')==0x8664 and int.from_bytes(d[pe+0x5c:pe+0x5e],'little')==10, 'bad PE/EFI header'; print('ocbl: ocbl_x64.efi %d bytes (x64 EFI app)' % len(d))"

.PHONY: ocbl-efi
ocbl-efi: $(OCBL_OUT)/ocbl_x64.efi
	@echo "ocbl-efi: $(OCBL_OUT)/ocbl_x64.efi ($$(stat -c%s $(OCBL_OUT)/ocbl_x64.efi) bytes)"

# ---- standalone test ISO: DUAL-FIRMWARE once the EFI flavor exists ----
# NOTE: build_test_iso.py is a hard prerequisite (B1 live-caught: a
# script-only fix left the ISO stale because the file target looked fresh
# - the same stale-dependency class as WP-02 F5; scripts in the recipe
# chain must be dependencies).
$(OCBL_OUT)/ocbl_test.iso: $(OCBL_OUT)/ocbl_bios.bin $(OCBL_OUT)/ocbl_x64.efi $(OCBL_DIR)/build_test_iso.py
	rm -rf $(OCBL_OUT)/iso-stage
	mkdir -p $(OCBL_OUT)/iso-stage
	cp $(OCBL_OUT)/ocbl_bios.bin $(OCBL_OUT)/iso-stage/OCBL.BIN
	cp $(OCBL_OUT)/ocbl_x64.efi $(OCBL_OUT)/iso-stage/BOOTX64.EFI
	python3 $(OCBL_DIR)/build_test_iso.py $@ $(OCBL_OUT)/iso-stage OCBL.BIN BOOTX64.EFI

.PHONY: ocbl-bios ocbl-iso ocbl-run-bios ocbl-run-uefi

ocbl-bios: $(OCBL_OUT)/ocbl_bios.bin $(OCBL_OUT)/stage1_mbr.bin
	@echo "ocbl-bios: $(OCBL_OUT)/ocbl_bios.bin ($$(stat -c%s $(OCBL_OUT)/ocbl_bios.bin) bytes)"
	@echo "ocbl-bios: $(OCBL_OUT)/stage1_mbr.bin (512 bytes, disk flavor)"

ocbl-iso: $(OCBL_OUT)/ocbl_test.iso
	@echo "ocbl-iso: $(OCBL_OUT)/ocbl_test.iso ($$(stat -c%s $(OCBL_OUT)/ocbl_test.iso) bytes, dual-firmware)"

ocbl-run-bios: $(OCBL_OUT)/ocbl_test.iso
	$(QEMU) -L $(QEMU_DATADIR) -L $(SEABIOS_DIR) \
          -m 256M -cdrom $(OCBL_OUT)/ocbl_test.iso -boot d \
          -no-reboot -display none -serial stdio -monitor none -vga std

# UEFI flavor: the SAME dual-firmware ISO, booted through OVMF (the
# acceptance gate for the EFI chain; OVMF logs land on the same
# serial line as the OCBL banner itself)
ocbl-run-uefi: $(OCBL_OUT)/ocbl_test.iso
	$(QEMU) -L $(QEMU_DATADIR) -L $(SEABIOS_DIR) \
          -m 256M -cdrom $(OCBL_OUT)/ocbl_test.iso -boot d \
          -bios $(OVMF_FD) \
          -no-reboot -display none -serial stdio -monitor none -vga std

# ---- B2b scanner test fleet (QEMU bring-up disks) ----
$(OCBL_OUT)/scan_disks/hd0.img: ocbl/make_scan_disks.py | $(OCBL_OUT)
	python3 ocbl/make_scan_disks.py $(OCBL_OUT)/scan_disks

.PHONY: ocbl-scan-disks
ocbl-scan-disks: $(OCBL_OUT)/scan_disks/hd0.img
	@echo "ocbl: scan fleet ready"

.PHONY: ocbl-run-scan
ocbl-run-scan: ocbl-iso $(OCBL_OUT)/scan_disks/hd0.img
	$(QEMU) -L $(QEMU_DATADIR) -L $(SEABIOS_DIR) \
          -m 256M \
          -drive file=$(OCBL_OUT)/scan_disks/hd0.img,if=ide,index=0,media=disk,format=raw \
          -drive file=$(OCBL_OUT)/scan_disks/hd1.img,if=ide,index=1,media=disk,format=raw \
          -drive file=$(OCBL_OUT)/scan_disks/hd2.img,if=virtio,format=raw \
          -drive file=$(OCBL_OUT)/scan_disks/hd3.img,if=virtio,format=raw \
          -drive file=$(OCBL_OUT)/scan_disks/hd4.img,if=virtio,format=raw \
          -drive file=$(OCBL_OUT)/scan_disks/fat0.img,if=virtio,format=raw \
          -drive file=$(OCBL_OUT)/scan_disks/bare0.img,if=virtio,format=raw \
          -drive file=$(OCBL_OUT)/scan_disks/cd0.iso,if=ide,index=3,media=cdrom,format=raw \
          -cdrom $(OCBL_OUT)/ocbl_test.iso -boot d \
          -no-reboot -display none -serial stdio -monitor none -vga std
