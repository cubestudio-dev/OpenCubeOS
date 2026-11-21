# SPDX-License-Identifier: Apache-2.0 */
# Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
# Open Cube OS - 3.0.0-WP-runtime-hardening-and-experience-rebase
# File: ocrmecl/ocrmmecl.mk
# Purpose: build targets for the OCRMECL boot compatibility layer
#          (Open Cube Runtime Machine Environment Compatibility Layer) -
#          the physically separate stage that runs BEFORE OCBL on both
#          firmware tracks and flattens the machine state QEMU/SeaBIOS/
#          OVMF/VMware each leave in a different shape.
#
#          BIOS track:  the El Torito composite image
#                       [ stage1 512B ][ core ][ OCBL.BIN ]  - SeaBIOS
#                       preloads it whole at 0x7C00, the core flattens
#                       the machine, moves the embedded OCBL.BIN down
#                       and hands off with the plain El Torito contract
#                       (interface/boot_handoff.h, BIOS section).
#
#          UEFI track:  BOOTX64.EFI = OCRMECL - it performs every
#                       firmware call of the boot (BlockIo ISO load,
#                       LoadImage-from-buffer of OCBL.EFI, GOP, memory
#                       map, ExitBootServices), rewrites GDT/IDT, strips
#                       the NX/RO page-table bits and jumps to OCBL's
#                       post-EBS raw entry (interface/boot_handoff.h,
#                       UEFI section).
#
# Targets:
#   make ocrmecl-bios   - the BIOS-track composite (build/ocrmecl/ocrmecl_bios.bin)
#   make ocrmecl-efi    - the UEFI-track PE (build/ocrmecl/ocrmecl_x64.efi)
#   make ocrmecl        - both
#
# Included from the top-level Makefile after ocbl/ocbl.mk (the BIOS
# composite embeds build/ocbl/ocbl_bios.bin; the EFI track links
# standalone - no OCBL object is ever linked into an OCRMECL image).

OCRMECL_DIR := $(OC_ROOT)/ocrmecl
OCRMECL_OUT := $(BUILD)/ocrmecl

$(OCRMECL_OUT):
	@mkdir -p $@

# ---- BIOS track ------------------------------------------------------
$(OCRMECL_OUT)/stage1.bin: $(OCRMECL_DIR)/bios/boot_ocrmmecl_bios_stage1.asm | $(OCRMECL_OUT)
	$(NASM) -f bin -o $@ $<
	python3 -c "b=open('$(abspath $@)','rb').read(); assert len(b)==512, 'ocrmecl stage1 %d bytes' % len(b); print('ocrmecl: stage1 512B ok')"

$(OCRMECL_OUT)/core.bin: $(OCRMECL_DIR)/bios/boot_ocrmmecl_bios_core.asm | $(OCRMECL_OUT)
	$(NASM) -f bin -o $@ $<
	python3 -c "b=open('$(abspath $@)','rb').read(); assert len(b)<=0x7800, 'ocrmecl core %d bytes over budget' % len(b); print('ocrmecl: core %dB ok' % len(b))"

# the composite: [stage1][core][OCBL.BIN] with the patch block filled
$(OCRMECL_OUT)/ocrmecl_bios.bin: $(OCRMECL_OUT)/stage1.bin $(OCRMECL_OUT)/core.bin \
		$(OCBL_OUT)/ocbl_bios.bin $(OCRMECL_DIR)/bios/build_ocr_image.py
	python3 $(OCRMECL_DIR)/bios/build_ocr_image.py $@ \
		$(OCRMECL_OUT)/stage1.bin $(OCRMECL_OUT)/core.bin $(OCBL_OUT)/ocbl_bios.bin

.PHONY: ocrmecl-bios
ocrmecl-bios: $(OCRMECL_OUT)/ocrmecl_bios.bin
	@echo "ocrmecl-bios: $(OCRMECL_OUT)/ocrmecl_bios.bin ($$(stat -c%s $(OCRMECL_OUT)/ocrmecl_bios.bin) bytes)"

# ---- UEFI track ------------------------------------------------------
# Same two-step pipeline the OCBL PE uses (mcmodel=large ELF with
# --emit-relocs -> ocbl/elf_to_efi.py -> PE/COFF with the complete
# .reloc directory); OCRMECL is built from its OWN sources only.
OCRMECL_EFI_CFLAGS := -ffreestanding -fno-stack-protector -fno-pie -fno-pic \
	-mno-red-zone -mno-sse -mno-mmx -mno-3dnow -mcmodel=large \
	-fno-ident -fno-asynchronous-unwind-tables \
	-Wall -Wextra -Werror -O2 -std=gnu11 \
	-I$(OCRMECL_DIR)/efi -I$(OCRMECL_DIR)/bios -I$(OC_ROOT)/include \
	-I$(OC_ROOT)/interface \
	-DOCBL_RELEASE="\"$(OC_RELEASE_VERSION)\"" \
	-DOCBL_BUILD=$(OC_KERNEL_BUILD)

OCRMECL_EFI_SRC := $(wildcard $(OCRMECL_DIR)/efi/*.c)
OCRMECL_EFI_OBJ := $(patsubst $(OCRMECL_DIR)/%.c,$(OCRMECL_OUT)/efiobj/%.o,$(OCRMECL_EFI_SRC))

$(OCRMECL_OUT)/efiobj/%.o: $(OCRMECL_DIR)/%.c $(wildcard $(OCRMECL_DIR)/efi/*.h) | $(OCRMECL_OUT)
	@mkdir -p $(dir $@)
	$(CC) $(OCRMECL_EFI_CFLAGS) -c $< -o $@

$(OCRMECL_OUT)/ocrmecl_x64.efi: $(OCRMECL_EFI_OBJ) $(OCBL_DIR)/elf_to_efi.py $(OCRMECL_DIR)/linker_efi.ld
	$(LD) -m elf_x86_64 --emit-relocs -n -nostdlib \
	      -T $(OCRMECL_DIR)/linker_efi.ld -o $(OCRMECL_OUT)/core_efi.elf \
	      $(OCRMECL_EFI_OBJ)
	python3 $(OCBL_DIR)/elf_to_efi.py $(OCRMECL_OUT)/core_efi.elf $@ ocr_efi_main
	python3 -c "d=open('$(abspath $@)','rb').read(); pe=int.from_bytes(d[0x3c:0x40],'little'); assert d[:2]==b'MZ' and d[pe:pe+4]==b'PE\x00\x00' and int.from_bytes(d[pe+4:pe+6],'little')==0x8664, 'bad PE/EFI header'; print('ocrmecl: ocrmecl_x64.efi %d bytes (x64 EFI app)' % len(d))"

.PHONY: ocrmecl-efi
ocrmecl-efi: $(OCRMECL_OUT)/ocrmecl_x64.efi
	@echo "ocrmecl-efi: $(OCRMECL_OUT)/ocrmecl_x64.efi ($$(stat -c%s $(OCRMECL_OUT)/ocrmecl_x64.efi) bytes)"

.PHONY: ocrmecl
ocrmecl: ocrmecl-bios ocrmecl-efi
	@echo "ocrmecl: both tracks ready"
