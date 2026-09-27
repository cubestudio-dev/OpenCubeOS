# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS — WP-01
# File: Makefile
#
# Targets:
#   make            - build the kernel ELF (build/opencube.elf)
#   make iso        - build bootable ISO (build/opencube.iso)  [BIOS + UEFI]
#   make run-bios   - boot the ISO in QEMU (BIOS/SeaBIOS)
#   make run-uefi   - boot the ISO in QEMU (UEFI/OVMF)
#   make shot-bios  - run-bios and capture a screenshot
#   make shot-uefi  - run-uefi and capture a screenshot
#   make clean      - remove build artifacts
#   make dist       - build a source zip + ISO into dist/
#
# Toolchain: set OC_TOOLS env var to your toolchain root (e.g. /usr for
# system-installed, or a custom path if you extracted packages manually).
# OVMF_FD / QEMU_DATADIR / SEABIOS_DIR default to standard system paths.

OC_ROOT   := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
BUILD     := $(OC_ROOT)/build
ISO_DIR   := $(OC_ROOT)/iso
DIST      := $(OC_ROOT)/dist

# Tools (resolved via PATH)
CC        ?= gcc
LD        ?= ld
NASM      ?= nasm
QEMU      ?= qemu-system-x86_64
# Override these via env if your tools are in a non-standard location:
#   export OC_TOOLS=/path/to/toolchain
#   export OVMF_FD=/path/to/OVMF.fd
OC_TOOLS  ?= /usr
OVMF_FD   ?= $(OC_TOOLS)/share/ovmf/OVMF.fd
QEMU_DATADIR ?= $(OC_TOOLS)/share/qemu
SEABIOS_DIR  ?= $(OC_TOOLS)/share/seabios

# Compiler / linker flags — freestanding, no redzone, no PIE, no stack protector.
CFLAGS    := -ffreestanding -fno-stack-protector -fno-pie -fno-pic \
	     -mno-red-zone -mno-sse -mno-mmx -mno-3dnow -mcmodel=kernel \
	     -fno-asynchronous-unwind-tables -Wall -Wextra -Werror \
	     -O2 -g -std=gnu11 -I$(OC_ROOT)/kernel \
	     -MMD -MP
ASFLAGS   := -f elf64 -F dwarf -g
LDFLAGS   := -n -nostdlib -T $(OC_ROOT)/linker.ld -z max-page-size=0x1000 -z noexecstack

# Sources
KERNEL_C    := $(wildcard $(OC_ROOT)/kernel/*.c)
KERNEL_ASM_B := $(wildcard $(OC_ROOT)/boot/*.S)
KERNEL_ASM_K := $(wildcard $(OC_ROOT)/kernel/*.S)
KERNEL_OBJ  := $(patsubst $(OC_ROOT)/kernel/%.c,$(BUILD)/%.o,$(KERNEL_C)) \
	       $(patsubst $(OC_ROOT)/boot/%.S,$(BUILD)/boot_%.o,$(KERNEL_ASM_B)) \
	       $(patsubst $(OC_ROOT)/kernel/%.S,$(BUILD)/%.o,$(KERNEL_ASM_K))

KERNEL     := $(BUILD)/opencube.elf
KERNEL_ISO := $(BUILD)/opencube.iso

# Default goal
.DEFAULT_GOAL := all
.PHONY: all iso run-bios run-uefi run-bios-gui run-uefi-gui shot-bios shot-uefi clean dist

all: $(KERNEL)

# ---- Build rules ----

$(BUILD):
	mkdir -p $(BUILD)

# C source → object file (with header dependency tracking, BUG-025 FIX)
$(BUILD)/%.o: $(OC_ROOT)/kernel/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

# Assembly source (boot/) → object file
$(BUILD)/boot_%.o: $(OC_ROOT)/boot/%.S | $(BUILD)
	$(NASM) $(ASFLAGS) $< -o $@

# Assembly source (kernel/) → object file
$(BUILD)/%.o: $(OC_ROOT)/kernel/%.S | $(BUILD)
	$(NASM) $(ASFLAGS) $< -o $@

# Link the kernel ELF, then strip debug info (removes build-machine paths)
$(KERNEL): $(KERNEL_OBJ) $(OC_ROOT)/linker.ld | $(BUILD)
	$(LD) $(LDFLAGS) -o $@ $(KERNEL_OBJ)
	strip --strip-debug $@

# BUG-025 FIX: Header dependency tracking (-MMD -MP generates .d files)
-include $(KERNEL_OBJ:.o=.d)

# ---- ISO build ----
# Uses tools/build_iso.sh which calls grub-mkimage + xorriso directly.
$(KERNEL_ISO): $(KERNEL) $(OC_ROOT)/grub.cfg $(OC_ROOT)/tools/build_iso.sh
	mkdir -p $(BUILD)
	bash $(OC_ROOT)/tools/build_iso.sh

iso: $(KERNEL_ISO)

# ---- QEMU runs ----

# BIOS boot: SeaBIOS loads GRUB from El Torito, GRUB loads kernel.
run-bios: $(KERNEL_ISO)
	$(QEMU) -L $(QEMU_DATADIR) -L $(SEABIOS_DIR) \
	  -m 256M -cdrom $(KERNEL_ISO) -boot d \
	  -no-reboot -display none -serial stdio -monitor none -vga std -snapshot

# UEFI boot: OVMF loads GRUB EFI from El Torito, GRUB loads kernel.
run-uefi: $(KERNEL_ISO)
	cp $(OVMF_FD) $(BUILD)/OVMF_WORK.fd
	$(QEMU) -L $(QEMU_DATADIR) -L $(SEABIOS_DIR) \
	  -m 256M -cdrom $(KERNEL_ISO) -boot d \
	  -no-reboot -display none -serial stdio -monitor none -vga std -snapshot \
	  -drive if=pflash,format=raw,file=$(BUILD)/OVMF_WORK.fd

# ---- GUI runs (keyboard input via the QEMU window) ----
# The -display none targets above are headless: there is no graphical window,
# so the PS/2 keyboard has nowhere to send scancodes and ONLY serial input
# works.  Use these GUI targets when you want to type on the keyboard:
# a GTK window opens showing the 800x600 framebuffer and captures keystrokes
# (IRQ1 -> scancode -> queue).  Serial stays on stdio so you can still see
# output and type there too; both feeds share the same input queue.
run-bios-gui: $(KERNEL_ISO)
	$(QEMU) -L $(QEMU_DATADIR) -L $(SEABIOS_DIR) \
	  -m 256M -cdrom $(KERNEL_ISO) -boot d \
	  -no-reboot -display gtk -serial stdio -monitor none -vga std -snapshot

run-uefi-gui: $(KERNEL_ISO)
	cp $(OVMF_FD) $(BUILD)/OVMF_WORK.fd
	$(QEMU) -L $(QEMU_DATADIR) -L $(SEABIOS_DIR) \
	  -m 256M -cdrom $(KERNEL_ISO) -boot d \
	  -no-reboot -display gtk -serial stdio -monitor none -vga std -snapshot \
	  -drive if=pflash,format=raw,file=$(BUILD)/OVMF_WORK.fd

# Screenshots: use VNC + monitor screendump, convert PPM→PNG via PIL.
shot-bios: $(KERNEL_ISO)
	python3 $(OC_ROOT)/tools/qemu_shot_vnc.py $(KERNEL_ISO) $(BUILD)/shot-bios.png

shot-uefi: $(KERNEL_ISO)
	cp $(OVMF_FD) $(BUILD)/OVMF_WORK.fd
	python3 $(OC_ROOT)/tools/qemu_shot_vnc.py $(KERNEL_ISO) $(BUILD)/shot-uefi.png uefi

clean:
	rm -rf $(BUILD) $(DIST) $(ISO_DIR)/boot/opencube.elf

# ---- Distribution: source zip + ISO ----
dist: $(KERNEL_ISO)
	mkdir -p $(DIST)
	TIMESTAMP=$$(date +%Y%m%d-%H%M%S); \
	SRCZIP=$(DIST)/OpenCubeOS-src-WP08-$$TIMESTAMP.zip; \
	ISOCOPY=$(DIST)/OpenCubeOS-WP08-$$TIMESTAMP.iso; \
	(cd $(OC_ROOT) && zip -qr $$SRCZIP . -x "build/*" "dist/*" ".git/*" "tools/push_to_git.py" "releases/*"); \
	cp $(KERNEL_ISO) $$ISOCOPY; \
	echo "SRC: $$SRCZIP"; \
	echo "ISO: $$ISOCOPY"; \
	sha256sum $$ISOCOPY | tee $$ISOCOPY.sha256
