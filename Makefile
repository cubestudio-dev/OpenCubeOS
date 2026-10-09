# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS (current release: WP-10-project_restructure-fix1)
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
# OC_RELEASE_VERSION is baked into the kernel banner + uname + update
# check (WP-10u). Override for the end-to-end update test:
#   make OC_RELEASE_VERSION=WP-10c-test1
# FINDING #1 (HANDOVER 1.3, WP-10-AUDIT_P2-fix2): the version string is
# derived from git at build time so the banner/uname/update version
# always matches the actual tree: `git describe --tags` when HEAD is on
# (or after) a tag, literal "dev" fallback when git is unavailable or
# the tree has no tags. The ?= override still wins for the E2E update
# test. Release builds: tag BEFORE the final build (git tag
# WP-10-AUDIT_P2-fix2) so the baked string equals the Release tag.
OC_RELEASE_VERSION ?= $(shell git -C $(OC_ROOT) describe --tags 2>/dev/null || echo dev)

# ---- Source tree (WP-10-project_restructure-fix1 layout) ----
# One folder per module.  All include paths are exported to the compiler so
# every #include stays a plain basename include (multi -I strategy).
# NOTE: must be defined BEFORE CFLAGS because CFLAGS uses := (immediate).
KERNEL_DIRS := kernel kernel/arch/x86_64 kernel/core kernel/mem kernel/lib \
               kernel/crypto kernel/ota \
               drivers/block drivers/nic drivers/snd drivers/usb \
               drivers/input drivers/display drivers/pci \
               fs net shell l1
# NOTE: kernel/generated is deliberately NOT in KERNEL_DIRS: it only holds
# the stage-2 generated grub_boot_data.c, which is compiled separately
# (GRUB_DATA_OBJ) and linked in the second link stage only.
KERNEL_INC := $(foreach d,$(KERNEL_DIRS) boot tools,-I$(OC_ROOT)/$(d))

# ---- Embedded user programs (embed chain, HANDOVER 2.3 gap) ----
# kernel/core/userprogs_data.h is included by kernel/main.c (the `run`
# command executes these programs) but was maintained by MANUAL tool runs:
# the Makefile had ZERO rules touching it, so a clean rebuild silently
# shipped stale embedded programs (the fix1 int3_user incident class).
# Two guards now exist:
#   make userprogs        - regenerate the header from userprogs/ sources
#                           (asm via tools/embed_userprog.py, C via
#                           tools/build_c_userprog.py; needs nasm+gcc)
#   make userprogs-check  - fail when any source is >2 s newer than the
#                           header (2 s slack absorbs fresh-clone mtime
#                           jitter); wired into `make dist` so the
#                           release path can never ship stale programs
EMBED_PY        := tools/embed_userprog.py
BUILD_C_PY      := tools/build_c_userprog.py
BUILD_SOLIB_PY  := tools/build_solib.py
# The embed chain covers every embedded image: asm progs, C progs, the six
# dynamic-link programs (pie / pie-foo recipes), libfoo.c (solib leg) and
# libs/ld_so.c (ld.so itself, ld_so.ld @ 0x10000000).
USERPROG_SRCS   := $(wildcard $(OC_ROOT)/userprogs/*.asm) $(wildcard $(OC_ROOT)/userprogs/*.c) $(OC_ROOT)/libs/ld_so.c
USERPROGS_HDR   := kernel/core/userprogs_data.h

CFLAGS    := -ffreestanding -fno-stack-protector -fno-pie -fno-pic \
             -mno-red-zone -mno-sse -mno-mmx -mno-3dnow -mcmodel=kernel \
             -fno-asynchronous-unwind-tables -Wall -Wextra -Werror \
             -O2 -g -std=gnu11 $(KERNEL_INC) \
             -DOC_RELEASE_VERSION=\"$(OC_RELEASE_VERSION)\" \
             -MMD -MP
ASFLAGS   := -f elf64 -F dwarf -g
LDFLAGS   := -n -nostdlib -T $(OC_ROOT)/linker.ld -z max-page-size=0x1000 -z noexecstack

# Sources (recursive over KERNEL_DIRS)
# boot/grub_boot_stub.c is compiled separately (stage-1 link only).
KERNEL_C    := $(foreach d,$(KERNEL_DIRS),$(wildcard $(OC_ROOT)/$(d)/*.c))
KERNEL_ASM_B := $(wildcard $(OC_ROOT)/boot/*.S)
KERNEL_ASM_K := $(wildcard $(OC_ROOT)/kernel/arch/x86_64/*.S)
KERNEL_OBJ  := $(patsubst $(OC_ROOT)/%.c,$(BUILD)/%.o,$(KERNEL_C)) \
               $(patsubst $(OC_ROOT)/boot/%.S,$(BUILD)/boot_%.o,$(KERNEL_ASM_B)) \
               $(patsubst $(OC_ROOT)/kernel/%.S,$(BUILD)/%.o,$(KERNEL_ASM_K))

KERNEL     := $(BUILD)/opencube.elf
KERNEL_ISO := $(BUILD)/opencube.iso

# WP-10d-pre (rule-9 self-sufficiency): two-stage link.  Stage 1 links
# the kernel without the embedded boot data; tools/embed_grub.py then
# generates kernel/generated/grub_boot_data.c from the GRUB i386-pc
# boot/core images (grub-mkimage) + the stage-1 ELF; stage 2 links the
# final kernel with that data.  The in-system abdisk / install /
# grub-install commands use it to make a disk bootable from the shell.
GEN_DIR     := $(OC_ROOT)/kernel/generated
GRUB_DATA_C := $(GEN_DIR)/grub_boot_data.c
GRUB_DATA_OBJ := $(BUILD)/grub_boot_data.o
STUB_OBJ    := $(BUILD)/grub_boot_stub.o
BASE_ELF    := $(BUILD)/opencube_base.elf

# Default goal
.DEFAULT_GOAL := all
.PHONY: all iso run-bios run-uefi run-bios-gui run-uefi-gui run-bios-persist run-uefi-persist shot-bios shot-uefi clean dist userprogs userprogs-check

all: $(KERNEL)

# ---- Build rules ----

$(BUILD):
	mkdir -p $(BUILD)

# C source → object file (with header dependency tracking, BUG-025 FIX)
$(BUILD)/%.o: $(OC_ROOT)/%.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Assembly source (boot/) → object file
$(BUILD)/boot_%.o: $(OC_ROOT)/boot/%.S | $(BUILD)
	$(NASM) $(ASFLAGS) $< -o $@

# Assembly source (kernel/arch/x86_64/) → object file
$(BUILD)/%.o: $(OC_ROOT)/kernel/%.S | $(BUILD)
	@mkdir -p $(dir $@)
	$(NASM) $(ASFLAGS) $< -o $@

# Link the kernel ELF in two stages (see WP-10d-pre note above), then
# strip debug info (removes build-machine paths)
$(STUB_OBJ): $(OC_ROOT)/boot/grub_boot_stub.c $(OC_ROOT)/boot/grub_boot_data.h | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BASE_ELF): $(KERNEL_OBJ) $(STUB_OBJ) $(OC_ROOT)/linker.ld | $(BUILD)
	$(LD) $(LDFLAGS) -o $@ $(KERNEL_OBJ) $(STUB_OBJ)
	strip --strip-debug $@

$(GRUB_DATA_C): $(OC_ROOT)/tools/embed_grub.py $(BASE_ELF) \
		$(OC_ROOT)/boot/grub_boot_data.h | $(BUILD)
	python3 $(OC_ROOT)/tools/embed_grub.py "$(OC_TOOLS)" $(BASE_ELF) $@

$(GRUB_DATA_OBJ): $(GRUB_DATA_C) $(OC_ROOT)/boot/grub_boot_data.h | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL): $(BASE_ELF) $(GRUB_DATA_OBJ) $(OC_ROOT)/linker.ld | $(BUILD)
	$(LD) $(LDFLAGS) -o $@ $(KERNEL_OBJ) $(GRUB_DATA_OBJ)
	strip --strip-debug $@

# BUG-025 FIX: Header dependency tracking (-MMD -MP generates .d files)
-include $(KERNEL_OBJ:.o=.d)

# ---- ISO build ----
# Uses tools/build_iso.sh which calls grub-mkimage + xorriso directly.
$(KERNEL_ISO): $(KERNEL) $(OC_ROOT)/grub.cfg $(OC_ROOT)/tools/build_iso.sh
	mkdir -p $(BUILD)
	bash $(OC_ROOT)/tools/build_iso.sh

iso: $(KERNEL_ISO)

# ---- /etc config disk (WP-09-fix5) ----
# FAT32 volume mounted at /etc by the kernel; holds /etc/opencube.conf so
# configuration edits survive reboots.  Built from etc/opencube.conf.
ETC_IMG := $(BUILD)/etc.img

$(ETC_IMG): $(OC_ROOT)/etc/opencube.conf | $(BUILD)
	rm -f $(ETC_IMG)
	truncate -s 32M $(ETC_IMG)
	printf 'drive e: file="%s"\n' "$(abspath $(ETC_IMG))" > $(BUILD)/mtoolsrc
	PATH=$(OC_TOOLS)/usr/bin:$$PATH MTOOLSRC=$(BUILD)/mtoolsrc mformat -F -v OCS_ETC e:
	PATH=$(OC_TOOLS)/usr/bin:$$PATH MTOOLSRC=$(BUILD)/mtoolsrc mcopy $(OC_ROOT)/etc/opencube.conf e:opencube.conf

# ---- QEMU runs ----

# BIOS boot: SeaBIOS loads GRUB from El Torito, GRUB loads kernel.
run-bios: $(KERNEL_ISO) $(ETC_IMG)
	$(QEMU) -L $(QEMU_DATADIR) -L $(SEABIOS_DIR) \
          -m 256M -cdrom $(KERNEL_ISO) -boot d \
          -drive if=ide,format=raw,file=$(ETC_IMG) \
          -no-reboot -display none -serial stdio -monitor none -vga std -snapshot

# UEFI boot: OVMF loads GRUB EFI from El Torito, GRUB loads kernel.
run-uefi: $(KERNEL_ISO) $(ETC_IMG)
	cp $(OVMF_FD) $(BUILD)/OVMF_WORK.fd
	$(QEMU) -L $(QEMU_DATADIR) -L $(SEABIOS_DIR) \
          -m 256M -cdrom $(KERNEL_ISO) -boot d \
          -drive if=ide,format=raw,file=$(ETC_IMG) \
          -no-reboot -display none -serial stdio -monitor none -vga std -snapshot \
          -drive if=pflash,format=raw,file=$(BUILD)/OVMF_WORK.fd

# ---- GUI runs (keyboard input via the QEMU window) ----
# The -display none targets above are headless: there is no graphical window,
# so the PS/2 keyboard has nowhere to send scancodes and ONLY serial input
# works.  Use these GUI targets when you want to type on the keyboard:
# a GTK window opens showing the 800x600 framebuffer and captures keystrokes
# (IRQ1 -> scancode -> queue).  Serial stays on stdio so you can still see
# output and type there too; both feeds share the same input queue.
run-bios-gui: $(KERNEL_ISO) $(ETC_IMG)
	$(QEMU) -L $(QEMU_DATADIR) -L $(SEABIOS_DIR) \
          -m 256M -cdrom $(KERNEL_ISO) -boot d \
          -drive if=ide,format=raw,file=$(ETC_IMG) \
          -no-reboot -display gtk -serial stdio -monitor none -vga std -snapshot

run-uefi-gui: $(KERNEL_ISO) $(ETC_IMG)
	cp $(OVMF_FD) $(BUILD)/OVMF_WORK.fd
	$(QEMU) -L $(QEMU_DATADIR) -L $(SEABIOS_DIR) \
          -m 256M -cdrom $(KERNEL_ISO) -boot d \
          -drive if=ide,format=raw,file=$(ETC_IMG) \
          -no-reboot -display gtk -serial stdio -monitor none -vga std -snapshot \
          -drive if=pflash,format=raw,file=$(BUILD)/OVMF_WORK.fd

# Screenshots: use VNC + monitor screendump, convert PPM→PNG via PIL.
shot-bios: $(KERNEL_ISO)
	python3 $(OC_ROOT)/tools/qemu_shot_vnc.py $(KERNEL_ISO) $(BUILD)/shot-bios.png

shot-uefi: $(KERNEL_ISO)
	cp $(OVMF_FD) $(BUILD)/OVMF_WORK.fd
	python3 $(OC_ROOT)/tools/qemu_shot_vnc.py $(KERNEL_ISO) $(BUILD)/shot-uefi.png uefi

# WP-09-fix5: persistent runs (-snapshot OFF) — edits to /etc/opencube.conf
# are written back to build/etc.img and survive a reboot.
run-bios-persist: $(KERNEL_ISO) $(ETC_IMG)
	$(QEMU) -L $(QEMU_DATADIR) -L $(SEABIOS_DIR) \
          -m 256M -cdrom $(KERNEL_ISO) -boot d \
          -drive if=ide,format=raw,file=$(ETC_IMG) \
          -no-reboot -display none -serial stdio -monitor none -vga std

run-uefi-persist: $(KERNEL_ISO) $(ETC_IMG)
	cp $(OVMF_FD) $(BUILD)/OVMF_WORK.fd
	$(QEMU) -L $(QEMU_DATADIR) -L $(SEABIOS_DIR) \
          -m 256M -cdrom $(KERNEL_ISO) -boot d \
          -drive if=ide,format=raw,file=$(ETC_IMG) \
          -no-reboot -display none -serial stdio -monitor none -vga std \
          -drive if=pflash,format=raw,file=$(BUILD)/OVMF_WORK.fd

clean:
	rm -rf $(BUILD) $(DIST) $(ISO_DIR)/boot/opencube.elf $(GEN_DIR)/grub_boot_data.c

# ---- Embedded-user-program targets (embed chain) ----
# Regenerate the embedded program header from sources. asm programs go
# through tools/embed_userprog.py <name> <path>; C programs through
# tools/build_c_userprog.py <name> <path>. nasm/gcc must be on PATH.
userprogs:
	@set -e; for src in $(USERPROG_SRCS); do \
           base=$$(basename "$$src"); name=$${base%.*}; \
           case "$$src" in \
             *.asm) echo "userprogs: embed $$name (asm)"; python3 $(OC_ROOT)/$(EMBED_PY) "$$name" "$$src";; \
             */libfoo.c) echo "userprogs: embed $$name (solib)"; python3 $(OC_ROOT)/$(BUILD_SOLIB_PY) "$$src";; \
             */ld_so.c) echo "userprogs: embed $$name (c, ld_so.ld)"; \
		python3 $(OC_ROOT)/$(BUILD_C_PY) "$$name" "$$src" $(OC_ROOT)/libs/ld_so.ld;; \
             */dyn_hello.c|*/pie_test.c) echo "userprogs: embed $$name (c, pie)"; \
		python3 $(OC_ROOT)/$(BUILD_C_PY) "$$name" "$$src" pie;; \
             */so_test.c|*/dlsym_test.c|*/reloc_test.c|*/main_dyn.c) echo "userprogs: embed $$name (c, pie-foo)"; \
		python3 $(OC_ROOT)/$(BUILD_C_PY) "$$name" "$$src" pie-foo;; \
             *.c)   echo "userprogs: embed $$name (c)";   python3 $(OC_ROOT)/$(BUILD_C_PY) "$$name" "$$src";; \
           esac || exit 1; \
         done; echo "userprogs: regenerated $(USERPROGS_HDR)"

# Staleness gate: fail when any userprog source is more than 2 seconds
# newer than the embedded header (2 s slack absorbs fresh-clone mtime
# jitter, where all checkout files share one timestamp). Wired into
# `make dist` so the RELEASE path can never ship stale programs.
userprogs-check:
	@newest_src=$$(ls -t $(USERPROG_SRCS) 2>/dev/null | head -1); \
         if [ -n "$$newest_src" ] && [ -f "$(OC_ROOT)/$(USERPROGS_HDR)" ]; then \
           s=$$(stat -c %Y "$$newest_src"); h=$$(stat -c %Y "$(OC_ROOT)/$(USERPROGS_HDR)"); \
           if [ "$$s" -gt $$((h + 2)) ]; then \
             echo "ERROR: $$newest_src is newer than $(USERPROGS_HDR)."; \
             echo "The embedded user programs are STALE (fix1 int3_user incident class)."; \
             echo "Fix: make userprogs   (needs nasm + gcc on PATH)"; \
             exit 1; \
           fi; \
         fi; echo "userprogs-check: OK"

# ---- Distribution: source zip + ISO ----
dist: userprogs-check $(KERNEL_ISO)
	mkdir -p $(DIST)
	TIMESTAMP=$$(date +%Y%m%d-%H%M%S); \
	SRCZIP=$(DIST)/OpenCubeOS-src-$(OC_RELEASE_VERSION)-$$TIMESTAMP.zip; \
	ISOCOPY=$(DIST)/OpenCubeOS-$(OC_RELEASE_VERSION)-$$TIMESTAMP.iso; \
	(cd $(OC_ROOT) && zip -qr $$SRCZIP . -x "build/*" "dist/*" ".git/*" "tools/push_to_git.py" "releases/*" "website/rw*.sh" "website/build-local.sh" "website/public/downloads/*" "website/out/*" "website/.next/*" "website/node_modules/*" "kernel/generated/*"); \
	cp $(KERNEL_ISO) $$ISOCOPY; \
	echo "SRC: $$SRCZIP"; \
	echo "ISO: $$ISOCOPY"; \
	sha256sum $$ISOCOPY | tee $$ISOCOPY.sha256
