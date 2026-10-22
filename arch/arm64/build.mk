# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - WP-13-build_system
# File: arch/arm64/build.mk
# Purpose: cross-compile rules for the ARM64 (aarch64) bring-up image.
#          Included by the top-level Makefile when ARCH=arm64.
#
# Toolchain: aarch64-linux-gnu-gcc (Debian gcc-aarch64-linux-gnu, or any
# GCC >= 12 built with aarch64-linux-gnu target).  The cross prefix is
# overridable: make ARCH=arm64 CROSS=aarch64-none-elf-
#
# Output: build/opencube-arm64.elf  (ETT_EXEC AARCH64)
# Run:    qemu-system-aarch64 -machine virt -cpu cortex-a57 \
#           -nographic -kernel build/opencube-arm64.elf

CROSS   ?= aarch64-linux-gnu-
XCC      := $(CROSS)gcc
XLD      := $(CROSS)ld
XOBJDUMP := $(CROSS)objdump
XREADELF := $(CROSS)readelf

XARCH_DIR := $(OC_ROOT)/arch/arm64
XBUILD    := $(BUILD)/arch-arm64
XELF      := $(BUILD)/opencube-arm64.elf

XCFLAGS := -ffreestanding -nostdlib -nostartfiles \
           -mgeneral-regs-only -fno-stack-protector -fno-pie -fno-pic \
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
	@echo "arm64 cross-compile: $@ ($$(stat -c%s $@) bytes)"

# `make run-bios ARCH=arm64` boots it under QEMU (BIOS = direct -kernel
# load on virt; there is no firmware, which is the arm64 equivalent of a
# BIOS-less raw boot).
QEMU_ARM64 ?= qemu-system-aarch64
run-bios: $(XELF)
	$(QEMU_ARM64) -machine virt -cpu cortex-a57 -smp 1 -m 128M \
	  -nographic -kernel $(XELF)

clean-arch:
	rm -rf $(XBUILD) $(XELF)
