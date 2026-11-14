# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - 3.0.0-WP06a-ocse
# File: ocse/ocse.mk
# Purpose: build targets for the OCSE install-environment kernel.
#
# Included once from the top-level Makefile (right after ocbl.mk).
# OCSE = OpenCubeOS Setup Environment: a small standalone kernel vendored
# from tag v1.0.0 (see ocse/VENDORING.md), launched by OCBL as a multiboot2
# ELF (OCBL_ENT_OCSE path). It is INDEPENDENT of the OCOS service tree.
#
# Targets:
#   make ocse-elf        - build/ocse/ocse.elf (the OCSE kernel)
#   make ocse-iso        - build/ocse.iso (dual-firmware install media)
#   make ocse-clean      - remove OCSE build artifacts
#
# The vendored sources are re-created by tools/vendor_ocse.sh and then
# patched by tools/ocse_edits.py; never hand-edit ocse/{kernel,drivers,
# fs,l1,boot,chinese}/*.c - a re-vendor would erase it.

OCSE_DIR := $(OC_ROOT)/ocse
OCSE_OUT := $(BUILD)/ocse

$(OCSE_OUT):
	@mkdir -p $@

# ---- vendoring integrity gate: ocse/ must be vendor+edits output ----
.PHONY: ocse-vendor-check
ocse-vendor-check:
	@if [ ! -f "$(OCSE_DIR)/kernel/types.h" ]; then \
	    echo "ocse: vendored sources missing - run tools/vendor_ocse.sh" \
	         "then tools/ocse_edits.py"; exit 1; \
	fi

# ---- flags: v1.0.0 kernel caliber (freestanding, no redzone, kernel model)
OCSE_CC := $(CC)
OCSE_CFLAGS := -ffreestanding -fno-stack-protector -fno-pie -fno-pic \
               -mno-red-zone -mno-sse -mno-mmx -mno-3dnow -mcmodel=kernel \
               -fno-asynchronous-unwind-tables -Wall -Wextra -Werror -O2 -g \
               -std=gnu11 \
               -I$(OCSE_DIR) \
               -I$(OCSE_DIR)/kernel -I$(OCSE_DIR)/kernel/arch/x86_64 \
               -I$(OCSE_DIR)/kernel/core -I$(OCSE_DIR)/kernel/lib \
               -I$(OCSE_DIR)/kernel/mem \
               -I$(OCSE_DIR)/boot -I$(OCSE_DIR)/fs -I$(OCSE_DIR)/l1 \
               -I$(OCSE_DIR)/chinese \
               -I$(OCSE_DIR)/drivers/acpi -I$(OCSE_DIR)/drivers/block \
               -I$(OCSE_DIR)/drivers/display -I$(OCSE_DIR)/drivers/i2c \
               -I$(OCSE_DIR)/drivers/input -I$(OCSE_DIR)/drivers/pci \
               -I$(OCSE_DIR)/drivers/time -I$(OCSE_DIR)/drivers/usb \
               -DOC_RELEASE_VERSION=\"$(OC_RELEASE_VERSION)\" \
               -DOC_KERNEL_BUILD=$(OC_KERNEL_BUILD) \
               -DOCSE_RELEASE=\"$(OC_RELEASE_VERSION)-ocse\" \
               -DOCSE_BUILD=$(OC_KERNEL_BUILD) \
               -MMD -MP

OCSE_AS := nasm
OCSE_ASFLAGS := -f elf64 -F dwarf -g

# ---- sources: vendored v1.0.0 subset + OCSE-specific top level ----
OCSE_K_C      := $(wildcard $(OCSE_DIR)/kernel/*.c) \
                 $(wildcard $(OCSE_DIR)/kernel/core/*.c) \
                 $(wildcard $(OCSE_DIR)/kernel/lib/*.c) \
                 $(wildcard $(OCSE_DIR)/kernel/mem/*.c)
OCSE_ARCH_C   := $(wildcard $(OCSE_DIR)/kernel/arch/x86_64/*.c)
OCSE_D_C      := $(wildcard $(OCSE_DIR)/drivers/acpi/*.c) \
                 $(wildcard $(OCSE_DIR)/drivers/block/*.c) \
                 $(wildcard $(OCSE_DIR)/drivers/display/*.c) \
                 $(wildcard $(OCSE_DIR)/drivers/i2c/*.c) \
                 $(wildcard $(OCSE_DIR)/drivers/input/*.c) \
                 $(wildcard $(OCSE_DIR)/drivers/pci/*.c) \
                 $(wildcard $(OCSE_DIR)/drivers/time/*.c) \
                 $(wildcard $(OCSE_DIR)/drivers/usb/*.c)
# WP06a-E2: driver_block_disk_setup.c is GRUB-era install tooling
# (writes grub boot.img/core.img; pulls core_boot_grubdata.h + shell.h).
OCSE_D_C := $(filter-out $(OCSE_DIR)/drivers/block/driver_block_disk_setup.c,$(OCSE_D_C))
# WP06a-E2: driver_block_disk_setup.c is GRUB-era install tooling
# (writes grub boot.img/core.img; pulls core_boot_grubdata.h + shell.h).
OCSE_D_C := $(filter-out $(OCSE_DIR)/drivers/block/driver_block_disk_setup.c,$(OCSE_D_C))
# WP06a-E2: driver_block_disk_setup.c is GRUB-era install tooling
# (writes grub boot.img/core.img; pulls core_boot_grubdata.h + shell.h).
OCSE_D_C := $(filter-out $(OCSE_DIR)/drivers/block/driver_block_disk_setup.c,$(OCSE_D_C))
# WP06a-E2: driver_block_disk_setup.c is GRUB-era install tooling
# (writes grub boot.img/core.img; pulls core_boot_grubdata.h + shell.h).
OCSE_D_C := $(filter-out $(OCSE_DIR)/drivers/block/driver_block_disk_setup.c,$(OCSE_D_C))
# WP06a-E2: driver_block_disk_setup.c is GRUB-era install tooling
# (writes grub boot.img/core.img; pulls core_boot_grubdata.h + shell.h).
OCSE_D_C := $(filter-out $(OCSE_DIR)/drivers/block/driver_block_disk_setup.c,$(OCSE_D_C))
# WP06a-E2: driver_block_disk_setup.c is GRUB-era install tooling
# (writes grub boot.img/core.img; pulls core_boot_grubdata.h + shell.h).
OCSE_D_C := $(filter-out $(OCSE_DIR)/drivers/block/driver_block_disk_setup.c,$(OCSE_D_C))
# WP06a-E2: driver_block_disk_setup.c is GRUB-era install tooling
# (writes grub boot.img/core.img; pulls core_boot_grubdata.h + shell.h).
OCSE_D_C := $(filter-out $(OCSE_DIR)/drivers/block/driver_block_disk_setup.c,$(OCSE_D_C))
# WP06a-E2: driver_block_disk_setup.c is GRUB-era install tooling
# (writes grub boot.img/core.img; pulls core_boot_grubdata.h + shell.h).
OCSE_D_C := $(filter-out $(OCSE_DIR)/drivers/block/driver_block_disk_setup.c,$(OCSE_D_C))
OCSE_FS_C     := $(wildcard $(OCSE_DIR)/fs/*.c)
OCSE_L1_C     := $(wildcard $(OCSE_DIR)/l1/*.c)
OCSE_CN_C     := $(wildcard $(OCSE_DIR)/chinese/*.c)
OCSE_TOP_C    := $(wildcard $(OCSE_DIR)/ocse_*.c)

OCSE_ARCH_S   := $(wildcard $(OCSE_DIR)/kernel/arch/x86_64/*.S)
OCSE_BOOT_S   := $(wildcard $(OCSE_DIR)/boot/*.S)

OCSE_C        := $(OCSE_K_C) $(OCSE_ARCH_C) $(OCSE_D_C) $(OCSE_FS_C) \
                 $(OCSE_L1_C) $(OCSE_CN_C) $(OCSE_TOP_C)
OCSE_ASM      := $(OCSE_ARCH_S) $(OCSE_BOOT_S)
OCSE_OBJ      := $(patsubst $(OCSE_DIR)/%.c,$(OCSE_OUT)/%.o,$(OCSE_C)) \
                 $(patsubst $(OCSE_DIR)/%.S,$(OCSE_OUT)/%.o,$(OCSE_ASM))
OCSE_ELF      := $(OCSE_OUT)/ocse.elf

# ---- compile rules ----
$(OCSE_OUT)/%.o: $(OCSE_DIR)/%.c | $(OCSE_OUT)
	@mkdir -p $(dir $@)
	$(OCSE_CC) $(OCSE_CFLAGS) -c $< -o $@

$(OCSE_OUT)/%.o: $(OCSE_DIR)/%.S | $(OCSE_OUT)
	@mkdir -p $(dir $@)
	$(OCSE_AS) $(OCSE_ASFLAGS) $< -o $@

# ---- link: single stage (no grub_boot_data second stage in OCSE) ----
$(OCSE_ELF): $(OCSE_OBJ) $(OCSE_DIR)/linker_ocse.ld
	$(LD) -n -nostdlib -T $(OCSE_DIR)/linker_ocse.ld \
	      -z max-page-size=0x1000 -z noexecstack \
	      -Map=$(OCSE_OUT)/ocse.map -o $@ $(OCSE_OBJ)
	@strip --strip-debug $@
	@python3 -c "import sys; b=open('$(abspath $@)','rb').read(); print('ocse: elf %d bytes' % len(b))"

.PHONY: ocse-elf
ocse-elf: ocse-vendor-check $(OCSE_ELF)

.PHONY: ocse-iso
ocse-iso: ocse-elf ocbl-bios
	bash tools/build_ocse_iso.sh

.PHONY: ocse-clean
ocse-clean:
	rm -rf $(OCSE_OUT) $(BUILD)/ocse.iso

-include $(OCSE_OBJ:.o=.d)
