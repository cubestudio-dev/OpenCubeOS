# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - WP-13-build_system
# File: arch/riscv/build.mk
# Purpose: cross-compile rules for the RISC-V 64 (rv64) bring-up image.
#          Included by the top-level Makefile when ARCH=riscv.
#
# Toolchain: riscv64-linux-gnu-gcc (Debian gcc-riscv64-linux-gnu, or any
# GCC >= 12 built with riscv64-linux-gnu target).  The cross prefix is
# overridable: make ARCH=riscv CROSS=riscv64-unknown-elf-
#
# Output: build/opencube-riscv.elf  (ET_EXEC RISC-V)
# Run:    qemu-system-riscv64 -machine virt -kernel build/opencube-riscv.elf

CROSS   ?= riscv64-linux-gnu-
XCC      := $(CROSS)gcc
XLD      := $(CROSS)ld
XOBJDUMP := $(CROSS)objdump
XREADELF := $(CROSS)readelf

XARCH_DIR := $(OC_ROOT)/arch/riscv
XBUILD    := $(BUILD)/arch-riscv
XELF      := $(BUILD)/opencube-riscv.elf

# -march=rv64imac_zicsr (no FP: M-mode has no enable bits set by a raw -kernel
# boot, and the FP registers are dead until mstatus.FS is programmed).
# -mabi=lp64 matches (integer calling convention, no FP args).
XCFLAGS := -ffreestanding -nostdlib -nostartfiles \
           -march=rv64imac_zicsr -mabi=lp64 \
           -mcmodel=medany \
           -fno-stack-protector -fno-pie -fno-pic \
           -Wall -Wextra -Werror -O2 -g -std=gnu11 \
           -I$(XARCH_DIR) \
           -DOC_RELEASE_VERSION=\"$(OC_RELEASE_VERSION)\" \
           -DOC_KERNEL_BUILD=$(OC_KERNEL_BUILD)

XOBJ := $(XBUILD)/boot.o $(XBUILD)/kernel_main.o

.PHONY: all kernel run-bios
all: $(XELF)
kernel: $(XELF)

$(XBUILD):
	mkdir -p $(XBUILD)

$(XBUILD)/%.o: $(XARCH_DIR)/%.S | $(XBUILD)
	$(XCC) $(XCFLAGS) -c $< -o $@

$(XBUILD)/%.o: $(XARCH_DIR)/%.c | $(XBUILD)
	$(XCC) $(XCFLAGS) -c $< -o $@

$(XELF): $(XOBJ) $(XARCH_DIR)/linker.ld | $(XBUILD)
	$(XCC) $(XCFLAGS) -T $(XARCH_DIR)/linker.ld \
	  -Wl,--build-id=none -Wl,--hash-style=sysv -o $@ $(XOBJ)
	$(XREADELF) -h $@ | grep -E "Class|Machine|Entry"
	@echo "riscv cross-compile: $@ ($$(stat -c%s $@) bytes)"

# `make run-bios ARCH=riscv` boots it under QEMU (raw M-mode -kernel
# boot, the riscv equivalent of a BIOS-less raw boot).
QEMU_RISCV64 ?= qemu-system-riscv64
run-bios: $(XELF)
	$(QEMU_RISCV64) -machine virt -smp 1 -m 128M -bios none \
	  -nographic -kernel $(XELF)

clean-arch:
	rm -rf $(XBUILD) $(XELF)
