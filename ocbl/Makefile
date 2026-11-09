# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS NAK - ocbl Makefile (WP-02-ocbl)
#
# The handwritten bootloader. Two independent firmware paths, one
# shared 64-bit ELF64 loader:
#
#   all      - ocbl-bios.bin + ocbl-uefi.efi + the bootable
#              opencubenak-WP-02-ocbl.iso (default)
#   bios     - BIOS path: 512-byte MBR (stage 1) + 32-bit trampoline
#              (stage 2a) + 64-bit C payload (stage 2b), with the
#              kernel.elf appended at LBA 64 and its sector count
#              patched into stage 2a at build time
#   uefi     - UEFI path: native PE32+ application built by
#              x86_64-w64-mingw32-gcc (-Wl,--subsystem,10)
#   iso      - opencubenak-WP-02-ocbl.iso, THE user-facing
#              deliverable: a dual El Torito image that boots on
#              BIOS (no-emulation boot image, everything preloaded
#              at 0x7C00) and on UEFI (FAT volume with
#              \EFI\BOOT\BOOTX64.EFI) from the same medium,
#              assembled by xorriso in mkisofs mode - no external
#              bootloader involved anywhere
#   run-bios    - boot ocbl-bios.bin as a raw disk under SeaBIOS
#   run-uefi    - boot ocbl-uefi.efi under OVMF (pflash) with the ESP
#                 tree served by QEMU's virtual FAT drive
#   run-iso-bios - boot the ISO under SeaBIOS (-cdrom)
#   run-iso-uefi - boot the ISO under OVMF (pflash + -cdrom)
#   test        - run all four paths and print their serial logs
#   clean       - remove the whole build/ tree
#
# BIOS disk layout (LBA), also documented in
# boot/core_boot_bios_layout.h:
#   0        stage 1 (boots at 0x7C00)
#   1..31    stage 2a, loaded to 0x7E00 (16-bit/32-bit trampoline)
#   32..63   stage 2b, loaded to 0x10000 (64-bit entry + C payload)
#   64..     kernel.elf, streamed to 0x20000 by stage 2a (INT 13h
#            AH=42h in 64-sector batches; the block count is patched
#            into the ocbl_kernel_sectors word of stage 2a)
#
# Naming everywhere follows [class]_[category]_[role].

SHELL := /bin/bash

CC       := gcc
UEFI_CC  := x86_64-w64-mingw32-gcc
LD       := ld
OBJCOPY  := objcopy
NM       := nm
QEMU     := qemu-system-x86_64

BUILD_DIR := build
KERNEL_ELF ?= ../ocos/build/kernel.elf

BIOS_BIN := $(BUILD_DIR)/ocbl-bios.bin
UEFI_EFI := $(BUILD_DIR)/ocbl-uefi.efi

# The user-facing deliverable (WP-02 acceptance): one bootable ISO,
# dual El Torito, kernel.elf inside.
BIOS_CD_BIN := $(BUILD_DIR)/ocbl-bios-cd.bin
ESP_IMG     := $(BUILD_DIR)/efi.img
ISO         := $(BUILD_DIR)/opencubenak-WP-02-ocbl.iso
ISO_ROOT    := $(BUILD_DIR)/iso-root

STAGE1  := $(BUILD_DIR)/core_boot_bios_stage1
STAGE2A := $(BUILD_DIR)/core_boot_bios_stage2a
STAGE2B := $(BUILD_DIR)/core_boot_bios_stage2b
ESP_DIR := $(BUILD_DIR)/esp

# El Torito no-emulation flavor of the BIOS path: the same three
# stages built with -DOCBL_BOOT_CD (see boot/core_boot_bios_layout.h
# for what the switch means). Separate object names keep both
# flavors side by side in build/.
STAGE1_CD  := $(BUILD_DIR)/core_boot_bios_stage1_cd
STAGE2A_CD := $(BUILD_DIR)/core_boot_bios_stage2a_cd
STAGE2B_CD := $(BUILD_DIR)/core_boot_bios_stage2b_cd

STAGE2A_LOAD := 0x7E00
PATCH_SYM    := ocbl_kernel_sectors
CD_DEFINES   := -DOCBL_BOOT_CD

# --- flags -------------------------------------------------------------------
WARN := -Wall -Wextra -Werror

# BIOS 16-bit assembly (stage 1, stage 2a)
BIOS_AS16_FLAGS := $(WARN) -ffreestanding -nostdlib -fno-pie -fno-pic \
	           -m16 -I .

# BIOS 64-bit C + assembly (stage 2b + payload)
BIOS_C64_FLAGS := -std=c11 -O2 -g $(WARN) \
	          -ffreestanding -nostdlib -fno-builtin \
	          -fno-pie -fno-pic -fno-stack-protector \
	          -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
	          -I .
BIOS_AS64_FLAGS := $(WARN) -ffreestanding -nostdlib \
	           -fno-pie -fno-pic -fno-stack-protector \
	           -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -I .

BIOS_LDFLAGS := -z noexecstack --build-id=none

# UEFI (mingw-w64, native PE32+)
UEFI_CFLAGS := -std=c11 -O2 -g $(WARN) \
	       -ffreestanding -nostdlib -fno-builtin \
	       -fno-stack-protector \
	       -fno-asynchronous-unwind-tables \
	       -fno-strict-aliasing \
	       -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
	       -fshort-wchar \
	       -I .
UEFI_LDFLAGS := -Wl,--subsystem,10 -e efi_main

UEFI_SRCS := boot/core_boot_uefi.c \
	     boot/core_boot_elf_loader.c \
	     drivers/drv_serial_16550.c \
	     drivers/drv_video_uefi_gop.c

BIOS64_SRCS := boot/core_boot_bios_main.c \
	       boot/core_boot_elf_loader.c \
	       drivers/drv_serial_16550.c \
	       drivers/drv_video_vga.c

QEMU_FWARGS := $(if $(QEMU_PREFIX),-L $(QEMU_PREFIX)/usr/share/qemu -L $(QEMU_PREFIX)/usr/share/seabios,)
OVMF_CODE   := $(if $(OVMF_PREFIX),$(OVMF_PREFIX)/OVMF_CODE_4M.fd,/usr/share/OVMF/OVMF_CODE_4M.fd)
OVMF_VARS   := $(if $(OVMF_PREFIX),$(OVMF_PREFIX)/OVMF_VARS_4M.fd,/usr/share/OVMF/OVMF_VARS_4M.fd)

.PHONY: all bios uefi iso run-bios run-uefi run-iso-bios run-iso-uefi test clean FORCE
.DELETE_ON_ERROR:

all: bios uefi iso

bios: $(BIOS_BIN)

uefi: $(UEFI_EFI)

iso: $(ISO)

FORCE:

# --- stage 1: MBR boot sector -------------------------------------------------
$(STAGE1).o: boot/core_boot_bios_stage1.S
	@mkdir -p $(BUILD_DIR)
	$(CC) $(BIOS_AS16_FLAGS) -c $< -o $@

$(STAGE1).bin: $(STAGE1).o
	$(LD) -m elf_i386 -Ttext 0x7C00 -e _start $(BIOS_LDFLAGS) -o $(STAGE1).elf $<
	$(OBJCOPY) -O binary $(STAGE1).elf $(STAGE1).bin
	@test $$(stat -c%s $(STAGE1).bin) -eq 512 || \
	 { echo "ocbl: stage1 must be exactly 512 bytes"; exit 1; }

# --- stage 2a: 16/32-bit trampoline -------------------------------------------
$(STAGE2A).o: boot/core_boot_bios_stage2.S boot/core_boot_bios_layout.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(BIOS_AS16_FLAGS) -c $< -o $@

$(STAGE2A).bin: $(STAGE2A).o
	$(LD) -m elf_i386 -Ttext 0x7E00 -e _start $(BIOS_LDFLAGS) -o $(STAGE2A).elf $<
	$(OBJCOPY) -O binary $(STAGE2A).elf $(STAGE2A).bin
	@test $$(stat -c%s $(STAGE2A).bin) -le $$(( 31*512 )) || \
	 { echo "ocbl: stage2a exceeds 31 sectors"; exit 1; }

# --- stage 2b + 64-bit C payload ----------------------------------------------
$(STAGE2B).o: boot/core_boot_bios_stage2b.S boot/core_boot_bios_layout.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(BIOS_AS64_FLAGS) -c $< -o $@

$(BUILD_DIR)/payload/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(BIOS_C64_FLAGS) -c $< -o $@

BIOS64_OBJS := $(addprefix $(BUILD_DIR)/payload/,$(BIOS64_SRCS:.c=.o))

$(STAGE2B).bin: $(STAGE2B).o $(BIOS64_OBJS)
	$(LD) -m elf_x86_64 -Ttext 0xBC00 \
	      -e core_boot_bios_stage2b_entry $(BIOS_LDFLAGS) \
	      -o $(STAGE2B).elf $(STAGE2B).o $(BIOS64_OBJS)
	$(OBJCOPY) -O binary -j .text -j .rodata -j .data \
	    $(STAGE2B).elf $(STAGE2B).bin
	@test $$(stat -c%s $(STAGE2B).bin) -le $$(( 32*512 )) || \
	 { echo "ocbl: stage2b exceeds 32 sectors"; exit 1; }

# --- assemble + patch the BIOS disk image --------------------------------------
# The kernel sector count is patched into the $(PATCH_SYM) word of
# stage 2a: file offset = symbol VMA - $(STAGE2A_LOAD) (link base).
$(BIOS_BIN): $(STAGE1).bin $(STAGE2A).bin $(STAGE2B).bin $(KERNEL_ELF)
	@mkdir -p $(BUILD_DIR)
	rm -f $@
	cat $(STAGE1).bin $(STAGE2A).bin > $@
	truncate -s 16384 $@
	cat $(STAGE2B).bin >> $@
	truncate -s 32768 $@
	cat $(KERNEL_ELF) >> $@
	truncate -s %512 $@
	sectors=$$(( ($$(stat -c%s $(KERNEL_ELF)) + 511) / 512 )); \
	test $$sectors -le 512 || \
	  { echo "ocbl: kernel.elf too large for the BIOS image ($$sectors sectors > 512)"; exit 1; }; \
	sym=$$($(NM) $(STAGE2A).elf | awk '/ $(PATCH_SYM)$$/ {print $$1}'); \
	off=$$(( 0x$${sym} - $(STAGE2A_LOAD) + 512 )); \
	hex=$$(printf '%02x%02x%02x%02x' \
	      $$(( sectors & 255 )) $$(( (sectors >> 8) & 255 )) \
	      $$(( (sectors >> 16) & 255 )) $$(( (sectors >> 24) & 255 ))); \
	esc=$$(echo $$hex | sed 's/../\\x&/g'); \
	printf "$$esc" | dd of=$@ bs=1 seek=$$off conv=notrunc status=none; \
	echo "ocbl: BIOS image ready, kernel = $$sectors sectors, patched at stage2a offset $$off"

# --- El Torito no-emulation flavor: the BIOS boot image of the ISO -----------
# Same three stages, built with -DOCBL_BOOT_CD: the firmware loads
# the whole image (including the kernel at file offset 32 KiB)
# contiguously at 0x7C00 before entering stage 1, so stage 1 makes
# no INT 13h call and stage 2a skips its kernel read loop.

$(STAGE1_CD).o: boot/core_boot_bios_stage1.S
	@mkdir -p $(BUILD_DIR)
	$(CC) $(BIOS_AS16_FLAGS) $(CD_DEFINES) -c $< -o $@

$(STAGE1_CD).bin: $(STAGE1_CD).o
	$(LD) -m elf_i386 -Ttext 0x7C00 -e _start $(BIOS_LDFLAGS) -o $(STAGE1_CD).elf $<
	$(OBJCOPY) -O binary $(STAGE1_CD).elf $(STAGE1_CD).bin
	@test $$(stat -c%s $(STAGE1_CD).bin) -eq 512 || \
	 { echo "ocbl: CD stage1 must be exactly 512 bytes"; exit 1; }

$(STAGE2A_CD).o: boot/core_boot_bios_stage2.S boot/core_boot_bios_layout.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(BIOS_AS16_FLAGS) $(CD_DEFINES) -c $< -o $@

$(STAGE2A_CD).bin: $(STAGE2A_CD).o
	$(LD) -m elf_i386 -Ttext 0x7E00 -e _start $(BIOS_LDFLAGS) -o $(STAGE2A_CD).elf $<
	$(OBJCOPY) -O binary $(STAGE2A_CD).elf $(STAGE2A_CD).bin
	@test $$(stat -c%s $(STAGE2A_CD).bin) -le $$(( 31*512 )) || \
	 { echo "ocbl: CD stage2a exceeds 31 sectors"; exit 1; }

$(STAGE2B_CD).o: boot/core_boot_bios_stage2b.S boot/core_boot_bios_layout.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(BIOS_AS64_FLAGS) $(CD_DEFINES) -c $< -o $@

$(BUILD_DIR)/payload_cd/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(BIOS_C64_FLAGS) $(CD_DEFINES) -c $< -o $@

BIOS64_CD_OBJS := $(addprefix $(BUILD_DIR)/payload_cd/,$(BIOS64_SRCS:.c=.o))

$(STAGE2B_CD).bin: $(STAGE2B_CD).o $(BIOS64_CD_OBJS)
	$(LD) -m elf_x86_64 -Ttext 0xBC00 \
	      -e core_boot_bios_stage2b_entry $(BIOS_LDFLAGS) \
	      -o $(STAGE2B_CD).elf $(STAGE2B_CD).o $(BIOS64_CD_OBJS)
	$(OBJCOPY) -O binary -j .text -j .rodata -j .data \
	    $(STAGE2B_CD).elf $(STAGE2B_CD).bin
	@test $$(stat -c%s $(STAGE2B_CD).bin) -le $$(( 32*512 )) || \
	 { echo "ocbl: CD stage2b exceeds 32 sectors"; exit 1; }

# Assemble the El Torito BIOS image. Identical byte layout to
# ocbl-bios.bin, but ocbl_kernel_sectors keeps its link-time zero
# (the read loop is skipped) and the kernel load base is the
# in-image address 0x7C00 + 0x8000 = 0xFC00 - both asserted below.
# The image is padded to a multiple of 2048 (the El Torito sector
# size the firmware counts in).
$(BIOS_CD_BIN): $(STAGE1_CD).bin $(STAGE2A_CD).bin $(STAGE2B_CD).bin $(KERNEL_ELF)
	@mkdir -p $(BUILD_DIR)
	rm -f $@
	cat $(STAGE1_CD).bin $(STAGE2A_CD).bin > $@
	truncate -s 16384 $@
	cat $(STAGE2B_CD).bin >> $@
	truncate -s 32768 $@
	cat $(KERNEL_ELF) >> $@
	truncate -s %2048 $@
	sect=$$(( ($$(stat -c%s $(KERNEL_ELF)) + 511) / 512 )); \
	test $$sect -le 512 || \
	  { echo "ocbl: kernel.elf too large for the CD image ($$sect sectors > 512)"; exit 1; }; \
	sym=$$($(NM) $(STAGE2A_CD).elf | awk '/ $(PATCH_SYM)$$/ {print $$1}'); \
	off=$$(( 0x$${sym} - $(STAGE2A_LOAD) + 512 )); \
	word=$$(od -An -tu4 -j $$off -N 4 $@ | tr -d ' '); \
	test "$$word" = "0" || \
	  { echo "ocbl: CD flavor must keep $(PATCH_SYM) at 0 (got $$word)"; exit 1; }; \
	sym=$$($(NM) $(STAGE2A_CD).elf | awk '/ ocbl_kernel_load_base$$/ {print $$1}'); \
	off=$$(( 0x$${sym} - $(STAGE2A_LOAD) + 512 )); \
	word=$$(od -An -tu4 -j $$off -N 4 $@ | tr -d ' '); \
	test "$$word" = "64512" || \
	  { echo "ocbl: CD flavor kernel load base must be 0xFC00 (got $$word)"; exit 1; }
	@echo "ocbl: El Torito BIOS image ready (kernel embedded, no INT 13h)"

# --- UEFI: native PE32+ ----------------------------------------------------------
$(UEFI_EFI): $(UEFI_SRCS) boot/core_boot_uefi.h boot/core_boot_elf_loader.h
	@mkdir -p $(BUILD_DIR)
	$(UEFI_CC) $(UEFI_CFLAGS) $(UEFI_SRCS) -o $@ $(UEFI_LDFLAGS)

# --- UEFI FAT image: the El Torito UEFI boot volume ----------------------------
# A 1 MiB FAT12 volume (16 heads x 32 sectors x 4 tracks, 2048-byte
# aligned as El Torito requires) exposing \EFI\BOOT\BOOTX64.EFI -
# what the firmware launches - and \boot\kernel.elf - what it loads.
# Assembled with mtools, no root needed.
$(ESP_IMG): $(UEFI_EFI) $(KERNEL_ELF)
	rm -f $@
	mformat -C -i $@ -h 16 -s 32 -t 4 ::
	mmd -i $@ ::/EFI ::/EFI/BOOT ::/boot
	mcopy -i $@ $(UEFI_EFI) ::/EFI/BOOT/BOOTX64.EFI
	mcopy -i $@ $(KERNEL_ELF) ::/boot/kernel.elf
	@mdir -/ -b -i $@ :: | sed 's|^|ocbl: efi.img: |'

# --- THE deliverable: the dual El Torito bootable ISO ---------------------------
# opencubenak-WP-02-ocbl.iso boots on BOTH firmwares from the same
# medium:
#   - BIOS (SeaBIOS): El Torito catalog entry 1, no-emulation image
#     boot/ocbl-bios-cd.bin - the firmware loads the whole image at
#     0x7C00 and enters stage 1 (see the OCBL_BOOT_CD flavor);
#   - UEFI (OVMF/real firmware): El Torito catalog entry 2, the FAT
#     volume efi.img carrying \EFI\BOOT\BOOTX64.EFI; it is appended
#     as MBR partition 2 (type 0xEF) as well, so the same ISO is
#     also USB-stick bootable on UEFI.
# /boot/kernel.elf is additionally visible as a plain ISO file -
# the very bytes both boot paths load. Assembled by xorriso in its
# mkisofs compatibility mode; no external bootloader involved.
$(ISO): $(BIOS_CD_BIN) $(ESP_IMG) $(KERNEL_ELF)
	rm -rf $(ISO_ROOT)
	mkdir -p $(ISO_ROOT)/boot
	cp $(BIOS_CD_BIN) $(ISO_ROOT)/boot/ocbl-bios-cd.bin
	cp $(KERNEL_ELF) $(ISO_ROOT)/boot/kernel.elf
	cp $(ESP_IMG) $(ISO_ROOT)/efi.img
	xorriso -as mkisofs -quiet \
	    -R -J -joliet-long -V OPENCUBENAK \
	    -b boot/ocbl-bios-cd.bin -no-emul-boot \
	    -eltorito-alt-boot -e efi.img -no-emul-boot \
	    -append_partition 2 0xef $(ESP_IMG) \
	    -o $@ $(ISO_ROOT)
	@echo "ocbl: ISO ready: $@ ($$(stat -c%s $@) bytes, dual El Torito)"

# --- kernel dependency (built by ocos) --------------------------------------------
$(KERNEL_ELF): FORCE
	$(MAKE) -C ../ocos kernel

# --- run / test -------------------------------------------------------------------
run-bios: bios
	$(QEMU) $(QEMU_FWARGS) -m 256M \
	        -drive format=raw,file=$(BIOS_BIN),if=ide \
	        -serial stdio -display none -monitor none -no-reboot

run-uefi: uefi
	@mkdir -p $(ESP_DIR)/EFI/BOOT $(ESP_DIR)/boot
	cp $(UEFI_EFI) $(ESP_DIR)/EFI/BOOT/BOOTX64.EFI
	cp $(KERNEL_ELF) $(ESP_DIR)/boot/kernel.elf
	cp $(OVMF_VARS) $(BUILD_DIR)/OVMF_VARS.fd
	$(QEMU) $(QEMU_FWARGS) -m 256M \
	        -pflash $(OVMF_CODE) -pflash $(BUILD_DIR)/OVMF_VARS.fd \
	        -drive format=raw,file=fat:rw:$(ESP_DIR),if=ide \
	        -serial stdio -display none -monitor none -no-reboot

run-iso-bios: iso
	$(QEMU) $(QEMU_FWARGS) -m 256M -cdrom $(ISO) \
	        -serial stdio -display none -monitor none -no-reboot

run-iso-uefi: iso
	cp $(OVMF_VARS) $(BUILD_DIR)/OVMF_VARS_iso.fd
	$(QEMU) $(QEMU_FWARGS) -m 256M \
	        -pflash $(OVMF_CODE) -pflash $(BUILD_DIR)/OVMF_VARS_iso.fd \
	        -cdrom $(ISO) \
	        -serial stdio -display none -monitor none -no-reboot

test: bios uefi iso
	@echo "=== BIOS serial (raw disk, SeaBIOS) ==="
	@timeout 20 $(QEMU) $(QEMU_FWARGS) -m 256M \
	        -drive format=raw,file=$(BIOS_BIN),if=ide \
	        -serial file:$(BUILD_DIR)/serial-bios.log -display none -monitor none -no-reboot >/dev/null 2>&1 || true
	@cat $(BUILD_DIR)/serial-bios.log
	@echo "=== BIOS serial (ISO, SeaBIOS) ==="
	@timeout 20 $(QEMU) $(QEMU_FWARGS) -m 256M -cdrom $(ISO) \
	        -serial file:$(BUILD_DIR)/serial-iso-bios.log -display none -monitor none -no-reboot >/dev/null 2>&1 || true
	@cat $(BUILD_DIR)/serial-iso-bios.log
	@echo "=== UEFI serial (virtual FAT ESP, OVMF) ==="
	@mkdir -p $(ESP_DIR)/EFI/BOOT $(ESP_DIR)/boot
	@cp $(UEFI_EFI) $(ESP_DIR)/EFI/BOOT/BOOTX64.EFI
	@cp $(KERNEL_ELF) $(ESP_DIR)/boot/kernel.elf
	@cp $(OVMF_VARS) $(BUILD_DIR)/OVMF_VARS.fd
	@timeout 60 $(QEMU) $(QEMU_FWARGS) -m 256M \
	        -pflash $(OVMF_CODE) -pflash $(BUILD_DIR)/OVMF_VARS.fd \
	        -drive format=raw,file=fat:rw:$(ESP_DIR),if=ide \
	        -serial file:$(BUILD_DIR)/serial-uefi.log -display none -monitor none -no-reboot >/dev/null 2>&1 || true
	@cat $(BUILD_DIR)/serial-uefi.log
	@echo "=== UEFI serial (ISO, OVMF) ==="
	@cp $(OVMF_VARS) $(BUILD_DIR)/OVMF_VARS_iso.fd
	@timeout 60 $(QEMU) $(QEMU_FWARGS) -m 256M \
	        -pflash $(OVMF_CODE) -pflash $(BUILD_DIR)/OVMF_VARS_iso.fd \
	        -cdrom $(ISO) \
	        -serial file:$(BUILD_DIR)/serial-iso-uefi.log -display none -monitor none -no-reboot >/dev/null 2>&1 || true
	@cat $(BUILD_DIR)/serial-iso-uefi.log

clean:
	rm -rf $(BUILD_DIR)
