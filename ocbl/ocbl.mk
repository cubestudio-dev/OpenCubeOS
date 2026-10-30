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

# ---- stage 1 (two flavors from one source) ----
$(OCBL_OUT)/stage1_eltorito.bin: $(OCBL_DIR)/bios/stage1.asm | $(OCBL_OUT)
	@mkdir -p $(OCBL_OUT)
	$(NASM) -f bin -D OCBL_ELTORITO -o $@ $<
	python3 -c "import sys; b=open('$(abspath $@)','rb').read(); assert len(b)==512, 'stage1 eltorito %d bytes' % len(b); print('ocbl: stage1 eltorito 512B ok')"

$(OCBL_OUT)/stage1_mbr.bin: $(OCBL_DIR)/bios/stage1.asm | $(OCBL_OUT)
	@mkdir -p $(OCBL_OUT)
	$(NASM) -f bin -D OCBL_MBR -o $@ $<
	python3 -c "import sys; b=open('$(abspath $@)','rb').read(); assert len(b)==512, 'stage1 mbr %d bytes' % len(b); print('ocbl: stage1 mbr 512B ok')"

# ---- core (32-bit head + 64-bit C) ----
$(OCBL_OUT)/bios/core_head.o: $(OCBL_DIR)/bios/core_head.S | $(OCBL_OUT)
	@mkdir -p $(OCBL_OUT)/bios
	$(NASM) -f elf64 -g -F dwarf -o $@ $<

$(OCBL_OUT)/common/%.o: $(OCBL_DIR)/common/%.c $(OCBL_COMMON_H) | $(OCBL_OUT)
	@mkdir -p $(OCBL_OUT)/common
	$(OCBL_CC) $(OCBL_CFLAGS) -c $< -o $@

$(OCBL_OUT)/core.elf: $(OCBL_OUT)/bios/core_head.o $(OCBL_OBJ) $(OCBL_DIR)/linker_bios.ld
	$(LD) -n -nostdlib -T $(OCBL_DIR)/linker_bios.ld \
	      -z max-page-size=0x1000 -z noexecstack -o $@ \
	      $(OCBL_OUT)/bios/core_head.o $(OCBL_OBJ)
	strip --strip-debug $@
	python3 -c "import sys; b=open('$(abspath $@)','rb').read(); print('ocbl: core.elf %d bytes' % len(b))"

$(OCBL_OUT)/core.bin: $(OCBL_OUT)/core.elf
	objcopy -O binary $< $@

# ---- BIOS boot image = stage1 + core (core padded to a 512 multiple) ----
$(OCBL_OUT)/ocbl_bios.bin: $(OCBL_OUT)/stage1_eltorito.bin $(OCBL_OUT)/core.bin
	python3 $(OCBL_DIR)/build_image.py $@ $(OCBL_OUT)/stage1_eltorito.bin $(OCBL_OUT)/core.bin

# ---- standalone test ISO (BIOS El Torito only for now; the UEFI flavor
#      joins when the EFI build lands) ----
# NOTE: build_test_iso.py is a hard prerequisite (B1 live-caught: a
# script-only fix left the ISO stale because the file target looked fresh
# - the same stale-dependency class as WP-02 F5; scripts in the recipe
# chain must be dependencies).
$(OCBL_OUT)/ocbl_test.iso: $(OCBL_OUT)/ocbl_bios.bin $(OCBL_DIR)/build_test_iso.py
	rm -rf $(OCBL_OUT)/iso-stage
	mkdir -p $(OCBL_OUT)/iso-stage
	cp $(OCBL_OUT)/ocbl_bios.bin $(OCBL_OUT)/iso-stage/OCBL.BIN
	python3 $(OCBL_DIR)/build_test_iso.py $@ $(OCBL_OUT)/iso-stage OCBL.BIN

.PHONY: ocbl-bios ocbl-iso ocbl-run-bios

ocbl-bios: $(OCBL_OUT)/ocbl_bios.bin $(OCBL_OUT)/stage1_mbr.bin
	@echo "ocbl-bios: $(OCBL_OUT)/ocbl_bios.bin ($$(stat -c%s $(OCBL_OUT)/ocbl_bios.bin) bytes)"
	@echo "ocbl-bios: $(OCBL_OUT)/stage1_mbr.bin (512 bytes, disk flavor)"

ocbl-iso: $(OCBL_OUT)/ocbl_test.iso
	@echo "ocbl-iso: $(OCBL_OUT)/ocbl_test.iso ($$(stat -c%s $(OCBL_OUT)/ocbl_test.iso) bytes)"

ocbl-run-bios: $(OCBL_OUT)/ocbl_test.iso
	$(QEMU) -L $(QEMU_DATADIR) -L $(SEABIOS_DIR) \
	  -m 256M -cdrom $(OCBL_OUT)/ocbl_test.iso -boot d \
	  -no-reboot -display none -serial stdio -monitor none -vga std
