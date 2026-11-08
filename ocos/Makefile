# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS NAK - ocos main system Makefile (WP-01-tree)
#
# Targets:
#   all     - kernel.elf + opencubenak-WP-01-tree.iso (default)
#   kernel  - kernel.elf only
#   iso     - opencubenak-WP-01-tree.iso only
#   run     - boot the ISO in QEMU with the serial console on stdio
#   clean   - remove every build product (the whole build/ tree)
#
# Source discovery is a pure filesystem scan (recursive wildcard over
# kernel/, drivers/, services/, apps/ and l1apps/): a new .c/.S file
# anywhere in those trees is picked up on the next make, no list to
# maintain, no Python, plain GNU make only. interface/ holds service
# API headers only (svc_*_api.h) and is never scanned for build
# inputs; it is reachable from includes via the -I . below.
#
# File naming everywhere follows [class]_[category]_[role]:
#   core_*  kernel core, drv_* drivers, svc_* services.
#
# Reproducible builds: SOURCE_DATE_EPOCH is honoured when set (the
# kernel "Build:" banner is derived from it); when unset it falls back
# to the current time.

CC            := gcc
LD            := ld
GRUB_MKRESCUE := grub-mkrescue
QEMU          := qemu-system-x86_64

# User-local toolchains (tools/install-toolchain.sh) install GRUB
# modules and QEMU firmware under a prefix instead of /usr; the
# compiled-in defaults do not exist there, so pass them explicitly.
GRUB_LIBDIR := $(if $(GRUB_PREFIX),$(GRUB_PREFIX)/usr/lib/grub/i386-pc,/usr/lib/grub/i386-pc)
QEMU_FWARGS  := $(if $(QEMU_PREFIX),-L $(QEMU_PREFIX)/usr/share/qemu -L $(QEMU_PREFIX)/usr/share/seabios,)

# --- reproducible build stamp -------------------------------------------
# SOURCE_DATE_EPOCH (env or make override) drives the kernel banner
# (CORE_MAIN_BUILD_TIMESTAMP, consumed by kernel/core_main_entry.c);
# without it the current time is used, so normal builds stay truthful.
# The stamp is UTC+8 (Asia/Shanghai): 28800 seconds are added to the
# epoch BEFORE the UTC breakdown, so the date rolls over correctly
# across midnight, and the suffix states the offset explicitly.
SOURCE_DATE_EPOCH ?= $(shell date +%s)
BUILD_TIMESTAMP := $(shell date -u -d "@$$(( $(SOURCE_DATE_EPOCH) + 28800 ))" '+%Y-%m-%d %H:%M:%S +08:00' 2>/dev/null || echo unknown)

# --- layout ------------------------------------------------------------------
BUILD_DIR := build
KERNEL    := $(BUILD_DIR)/kernel.elf
ISO       := $(BUILD_DIR)/opencubenak-WP-01-tree.iso
ISO_ROOT  := $(BUILD_DIR)/iso
GRUB_CFG  := grub.cfg

# --- flags ----------------------------------------------------------------------
KERNEL_CFLAGS := -std=c11 -O2 -g -Wall -Wextra -Werror \
                 -ffreestanding -nostdlib -fno-builtin \
                 -fno-pie -fno-pic -fno-stack-protector \
                 -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
                 -I . \
                 -MMD -MP \
                 -DCORE_MAIN_BUILD_TIMESTAMP="\"$(BUILD_TIMESTAMP)\""

KERNEL_ASFLAGS := -Wall -Wextra -Werror -D__ASSEMBLY__ \
                  -ffreestanding -nostdlib \
                  -fno-pie -fno-pic -fno-stack-protector \
                  -mno-red-zone -mno-mmx -mno-sse -mno-sse2

KERNEL_LDFLAGS := -z max-page-size=0x1000 -z noexecstack --build-id=none

# --- source discovery (recursive filesystem scan) ------------------------
# rwildcard DIR PATTERN - no hand-maintained file lists anywhere.
rwildcard = $(wildcard $1$2) $(foreach d,$(wildcard $1*),$(call rwildcard,$d/,$2))

KERNEL_DIRS  := kernel drivers services apps l1apps
KERNEL_C_SRCS := $(foreach d,$(KERNEL_DIRS),$(call rwildcard,$d/,*.c))
KERNEL_S_SRCS := $(foreach d,$(KERNEL_DIRS),$(call rwildcard,$d/,*.S))
KERNEL_OBJS   := $(sort $(patsubst %.c,$(BUILD_DIR)/%.o,$(KERNEL_C_SRCS)) \
                        $(patsubst %.S,$(BUILD_DIR)/%.o,$(KERNEL_S_SRCS)))

# --- rules -----------------------------------------------------------------------
.PHONY: all kernel iso run clean

all: $(KERNEL) $(ISO)

kernel: $(KERNEL)

iso: $(ISO)

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(KERNEL_ASFLAGS) -c $< -o $@

$(KERNEL): $(KERNEL_OBJS) kernel/linker.ld
	$(LD) $(KERNEL_LDFLAGS) -T kernel/linker.ld -o $@ $(KERNEL_OBJS)

$(ISO): $(KERNEL) $(GRUB_CFG)
	@mkdir -p $(ISO_ROOT)/boot/grub
	cp $(KERNEL) $(ISO_ROOT)/boot/kernel.elf
	cp $(GRUB_CFG) $(ISO_ROOT)/boot/grub/grub.cfg
	$(GRUB_MKRESCUE) -d $(GRUB_LIBDIR) -o $@ $(ISO_ROOT)

run: all
	$(QEMU) $(QEMU_FWARGS) -m 128M -cdrom $(ISO) \
	        -serial stdio -display none -monitor none -no-reboot

clean:
	rm -rf $(BUILD_DIR)

-include $(KERNEL_OBJS:.o=.d)

.DELETE_ON_ERROR:
MAKEFLAGS += --warn-undefined-variables
