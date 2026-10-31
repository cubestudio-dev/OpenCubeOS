# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS (current release: WP-10-project_restructure-fix1)
# File: Makefile
#
# Targets:
#   make            - build the kernel ELF (build/opencube.elf)
#   make all        - WP-13 full pipeline: kernel + ISO + SRC zip + OTA
#   make kernel     - kernel ELF only (the pre-WP-13 `make all` goal)
#   make iso        - build bootable ISO (build/opencube.iso)  [BIOS + UEFI]
#   make src-zip    - deterministic source zip (git archive; fixed order,
#                     fixed mtimes, fixed permissions)
#   make ota        - OTA update package (tools/make_update_pkg.sh)
#   make reproducible-check
#                   - WP-13: two clean builds, byte-compare the ELF
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

# ---- WP-13-build_system: target architecture dispatch ----------------
# ARCH selects the machine the build targets.  x86_64 (the default) is
# the full Open Cube OS kernel; arm64 / riscv build the per-architecture
# bring-up shells under arch/<name>/ (real bare-metal entry + UART +
# heartbeat; the full per-arch kernels are later work packages).  The
# x86_64 body of this Makefile is skipped entirely for cross builds.
ARCH ?= x86_64
ifeq ($(filter $(ARCH),x86_64 arm64 riscv),)
$(error unsupported ARCH '$(ARCH)' - supported: x86_64 arm64 riscv)
endif

ifneq ($(ARCH),x86_64)
# cross build: the per-arch build.mk replaces the whole x86_64 body below
include $(OC_ROOT)/src/L0/core/arch/$(ARCH)/build.mk
else

# ---- WP-13-build_system: multi-configuration (spec 5.7) ---------------
# The .config file is a make fragment (CONFIG_X=y|n) written by
# `make defconfig` or `make menuconfig`; command-line overrides win.
# 3.0.0-WP03: CONFIG_TUI and CONFIG_CHINESE are GONE - the TUI and the
# IME user plane are deleted, and the CJK display pipeline (font /
# encoding / render / console_cn) is always compiled in (display root
# service).  The surviving knobs are the hardware profile options
# (NVME / AHCI / VIRTIO_BLK); config_gen.h stubs their APIs when off.
-include $(OC_ROOT)/.config
CONFIG_NVME       ?= y
CONFIG_AHCI       ?= y
CONFIG_VIRTIO_BLK ?= y

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
# v1.0.0 (2026-10-24): the release version is now a fixed literal in the
# Makefile. It replaced the historical `git describe --tags` derivation
# (FINDING #1, HANDOVER 1.3, WP-10-AUDIT_P2-fix2) that served the
# internal engineering phase (WP-* tags). The ?= override still wins for
# the E2E update test. Engineering history remains in CHANGELOG and git.
# v2.0.0 (2026-10-25): the breaking ring-3 release - L0 restructured
# (core/drivers/services/boot/recovery), boots into the ring-3 service
# surface (ush + SYS_L1SVC), register-class interfaces removed.
OC_RELEASE_VERSION ?= 3.0.0-WP04
# BUG-0347 (RUNTIME): monotonic build id baked into the kernel and
# published by the server-side version.json - makes the update check a
# numeric comparison instead of a string equality (see ota_update.h).
# Base: "git rev-list --count HEAD" of the built tree; override for tests:
#   make OC_KERNEL_BUILD=42
# v1.0.0 (2026-10-24): +21 alignment offset. The published build ids
# were baked from an engineering tree whose git history ran 21 commits
# longer than the public repo main (WP-14-docs_release shipped build
# 274 at public count 253). The OTA update check compares build ids
# numerically (BUG-0347), so the public sequence must continue above
# the already-published 274. The offset keeps one-build-per-commit
# monotonicity: the v1.0.0 tag sits at public count 257 -> build 278.
OC_KERNEL_BUILD_OFFSET := 21
OC_KERNEL_BUILD ?= $(shell expr $$(git -C $(OC_ROOT) rev-list --count HEAD 2>/dev/null || echo 0) + $(OC_KERNEL_BUILD_OFFSET))

# ---- Source tree (3.0.0-WP03 G2 layout: the src/L0 service kernel) ----
# One folder per module.  All include paths are exported to the compiler so
# every #include stays a plain basename include (multi -I strategy).
# NOTE: must be defined BEFORE CFLAGS because CFLAGS uses := (immediate).
#   src/L0/boot/     - the boot composition root: boot.S / long_mode /
#                      multiboot2 / grub_boot_stub + kmain.c (ex main.c),
#                      the L1 launcher and the boot self-tests.  May call
#                      into every layer (it is the top of the dependency
#                      chain).
#   src/L0/core/     - kernel mechanisms (sched/sync/timer/usermode/
#                      registry/kcap/crash + mem/ + lib/ + generated/) and
#                      core/arch/{x86_64,arm64,riscv}.  Depends on nothing
#                      above it.
#   src/L0/drivers/  - hardware drivers, one folder per family.
#   src/L0/services/ - root services, folder == interface facet:
#                      fs block net(audio-merged nic) audio font encoding
#                      display loader selfhost | sysapi ipc klog health
#                      crash (+time power crypto camera usb bodies)
#   src/L1/          - ring-3 user applications: ush (the boot shell, the
#                      only OC_L1_APP_DECL application) + the embedded
#                      test programs (built by `make l1apps`)
# 3.0.0-WP03 G1 deletions (still true): recovery/ (the oc> environment
# shell), tui/ (the TUI framework + desktop), the IME (pinyin engine) -
# the whole L0 user plane is gone.  G2 dissolved the old chinese/ and
# services/l1/ grab-bags into font / encoding / display + sysapi / ipc /
# loader + boot.
KERNEL_DIRS := src/L0/core src/L0/core/arch/x86_64 src/L0/core/mem \
               src/L0/core/lib src/L0/core/console \
               src/L0/services/crypto src/L0/services/selfhost \
               src/L0/services/health src/L0/services/klog \
               src/L0/services/crash src/L0/services/power \
               src/L0/services/time src/L0/services/fs \
               src/L0/services/net src/L0/services/sysapi \
               src/L0/services/block src/L0/services/audio \
               src/L0/services/display src/L0/services/font \
               src/L0/services/encoding src/L0/services/camera \
               src/L0/services/usb src/L0/services/ipc \
               src/L0/services/loader src/L0/services/ocembed \
               src/L0/services/watchdog src/L0/services/l1 \
               src/L0/services/diag \
               src/L0/services/tui \
               src/L0/drivers/block src/L0/drivers/nic src/L0/drivers/snd \
               src/L0/drivers/usb src/L0/drivers/input \
               src/L0/drivers/pci \
               src/L0/drivers/acpi src/L0/drivers/i2c \
               src/L0/drivers/time src/L0/drivers/wireless \
               src/L0/drivers/bluetooth src/L0/drivers/sd \
               src/L0/drivers/camera src/L0/drivers/misc \
               src/L0/drivers/scsi \
               src/L0/boot
# NOTE: core/generated is deliberately NOT in KERNEL_DIRS: it only holds
# the stage-2 generated grub_boot_data.c, which is compiled separately
# (GRUB_DATA_OBJ) and linked in the second link stage only.  Its -I path
# IS exported (WP-10-tui-qa): version_gen.h lives there and is included
# by the objects that embed the release version.
KERNEL_INC := $(foreach d,$(KERNEL_DIRS) tools,-I$(OC_ROOT)/$(d)) \
              -I$(OC_ROOT)/src/L0/core/generated -I$(OC_ROOT)/src/L1

# ---- Embedded user programs (embed chain, HANDOVER 2.3 gap) ----
# src/L0/core/userprogs_data.h is included by core_usermode.c (the
# ring-3 images are mapped from these arrays) but was maintained by
# MANUAL tool runs:
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
# 3.0.0-WP04: folder apps join the staleness gate - a newer source
# under any app folder (or the shared header library) must refresh the
# generated header exactly like a legacy flat program does.
USERPROG_SRCS   := $(wildcard $(OC_ROOT)/src/L1/*.asm) $(wildcard $(OC_ROOT)/src/L1/*.c) $(OC_ROOT)/src/L1/ld_so.c \
                   $(wildcard $(OC_ROOT)/src/L1/*/*.c) $(wildcard $(OC_ROOT)/src/L1/*/*.asm) $(wildcard $(OC_ROOT)/src/L1/_shared/*.h)
USERPROGS_HDR   := src/L0/core/userprogs_data.h

CFLAGS    := -ffreestanding -fno-stack-protector -fno-pie -fno-pic \
             -mno-red-zone -mno-sse -mno-mmx -mno-3dnow -mcmodel=kernel \
             -fno-asynchronous-unwind-tables -Wall -Wextra -Werror \
             -O2 -g -std=gnu11 $(KERNEL_INC) \
             -DOC_RELEASE_VERSION=\"$(OC_RELEASE_VERSION)\" \
             -DOC_KERNEL_BUILD=$(OC_KERNEL_BUILD) \
             -MMD -MP
ASFLAGS   := -f elf64 -F dwarf -g

# ---- WP-13-build_system: content-addressed compile cache --------------
# OC_CC wraps every C compile through tools/oc_cc.sh: cache key = compiler
# identity + flags + source hash + header-content hashes (from -MM).  Hits
# restore the .o AND .d; misses compile for real and store both.  Disable
# with OC_CACHE_OFF=1; relocate with OC_CACHE_DIR=...; stats via
# `make cache-stats`.  Hits are byte-identical to fresh compiles, so the
# cache is safe for reproducible-check.
OC_CC := bash $(OC_ROOT)/tools/oc_cc.sh $(CC)
LDFLAGS   := -n -nostdlib -T $(OC_ROOT)/src/L0/core/arch/x86_64/linker.ld -z max-page-size=0x1000 -z noexecstack

# Sources (recursive over KERNEL_DIRS)
# src/L0/boot/grub_boot_stub.c is compiled separately (stage-1 link
# only); filter it out of the kernel-proper object list.
KERNEL_C    := $(foreach d,$(KERNEL_DIRS),$(wildcard $(OC_ROOT)/$(d)/*.c))
KERNEL_C    := $(filter-out %grub_boot_stub.c,$(KERNEL_C))
KERNEL_ASM_B := $(wildcard $(OC_ROOT)/src/L0/boot/*.S)
KERNEL_ASM_K := $(wildcard $(OC_ROOT)/src/L0/core/arch/x86_64/*.S)
# WP-13 multi-config source filtering: individual block drivers for
# the hardware profile options (self-registering drivers; the init
# calls in main.c are macro-stubbed by config_gen.h).  3.0.0-WP03: the
# TUI/chinese dir-drops above are gone (dirs deleted / always-on).
ifeq ($(CONFIG_NVME),n)
KERNEL_C    := $(filter-out %driver_block_nvme.c,$(KERNEL_C))
endif
ifeq ($(CONFIG_AHCI),n)
KERNEL_C    := $(filter-out %driver_block_ahci.c,$(KERNEL_C))
endif
ifeq ($(CONFIG_VIRTIO_BLK),n)
KERNEL_C    := $(filter-out %driver_block_virtio_blk.c,$(KERNEL_C))
endif

KERNEL_OBJ  := $(patsubst $(OC_ROOT)/%.c,$(BUILD)/%.o,$(KERNEL_C)) \
               $(patsubst $(OC_ROOT)/src/L0/boot/%.S,$(BUILD)/boot_%.o,$(KERNEL_ASM_B)) \
               $(patsubst $(OC_ROOT)/src/L0/core/arch/%.S,$(BUILD)/core/arch/%.o,$(KERNEL_ASM_K))

KERNEL     := $(BUILD)/opencube.elf
KERNEL_ISO := $(BUILD)/opencube.iso

# WP-10d-pre (rule-9 self-sufficiency): two-stage link.  Stage 1 links
# the kernel without the embedded boot data; tools/embed_grub.py then
# generates kernel/generated/grub_boot_data.c from the GRUB i386-pc
# boot/core images (grub-mkimage) + the stage-1 ELF; stage 2 links the
# final kernel with that data.  The in-system abdisk / install /
# grub-install commands use it to make a disk bootable from the shell.
GEN_DIR     := $(OC_ROOT)/src/L0/core/generated
GRUB_DATA_C := $(GEN_DIR)/grub_boot_data.c
GRUB_DATA_OBJ := $(BUILD)/grub_boot_data.o
STUB_OBJ    := $(BUILD)/grub_boot_stub.o
BASE_ELF    := $(BUILD)/opencube_base.elf

# ---- WP-10-tui-qa (build integrity): version recompile trigger --------
# OC_RELEASE_VERSION / OC_KERNEL_BUILD are -D flags, and -D flags are NOT
# part of the -MMD .d dependency graph.  Incremental builds therefore kept
# the banner/uname/update-check string baked into the LAST compile of
# main.o / shell_lang.o / ota_update.o while the linked code was new
# (observed live: an ISO booted a "-ga4d5342" banner over -ga29ac53 code).
# version_gen.h regenerates whenever the version changes (cmp-guarded, so
# unchanged versions do not thrash mtimes); the three objects include it,
# their .d files pick it up, and the next make recompiles exactly them.
VERSION_GEN := $(GEN_DIR)/version_gen.h
$(VERSION_GEN): FORCE
	@mkdir -p $(GEN_DIR)
	@{ echo '/* Generated by the Makefile - build stamp + recompile trigger'; \
           echo ' * for the release version (WP-10-tui-qa; see Makefile note).'; \
           echo ' * The -D OC_RELEASE_VERSION / OC_KERNEL_BUILD flags remain the'; \
           echo ' * authoritative value source; this header only makes their'; \
           echo ' * change visible to the dependency graph. */'; \
           echo '#define OC_VERSION_STAMP "$(OC_RELEASE_VERSION) build $(OC_KERNEL_BUILD)"'; \
         } > $(VERSION_GEN).tmp
	@if ! cmp -s $(VERSION_GEN).tmp $(VERSION_GEN); then \
           mv -f $(VERSION_GEN).tmp $(VERSION_GEN); \
           echo "make: regenerated $$@ ($(OC_RELEASE_VERSION))"; \
         else rm -f $(VERSION_GEN).tmp; fi
FORCE:
.PHONY: FORCE

# ---- WP-13 multi-config: generated macro header ----------------------
# OC_CONFIG_* + optional-module stubs; cmp-guarded so unchanged values
# do not thrash mtimes (includers' .d files trigger the recompiles).
CONFIG_GEN := $(GEN_DIR)/config_gen.h
$(CONFIG_GEN): FORCE
	@mkdir -p $(GEN_DIR)
	@{ echo '/* Generated by the Makefile - WP-13 multi-configuration.'; \
           echo ' * Values come from .config (make defconfig / menuconfig) or'; \
           echo ' * command-line CONFIG_X=n overrides. Do not edit. */'; \
           echo '#ifndef OC_CONFIG_GEN_H'; \
           echo '#define OC_CONFIG_GEN_H'; \
           echo '#define OC_CONFIG_NVME        $(if $(filter n,$(CONFIG_NVME)),0,1)'; \
           echo '#define OC_CONFIG_AHCI        $(if $(filter n,$(CONFIG_AHCI)),0,1)'; \
           echo '#define OC_CONFIG_VIRTIO_BLK  $(if $(filter n,$(CONFIG_VIRTIO_BLK)),0,1)'; \
           echo '#if !OC_CONFIG_NVME'; \
           echo '#define driver_block_nvme_init(a)     ((void)(a))'; \
           echo '#endif'; \
           echo '#if !OC_CONFIG_AHCI'; \
           echo '#define driver_block_ahci_init(a)     ((void)(a))'; \
           echo '#endif'; \
           echo '#if !OC_CONFIG_VIRTIO_BLK'; \
           echo '#define driver_block_virtio_blk_init() ((void)0)'; \
           echo '#endif'; \
           echo '#endif /* OC_CONFIG_GEN_H */'; \
         } > $(CONFIG_GEN).tmp
	@if ! cmp -s $(CONFIG_GEN).tmp $(CONFIG_GEN); then \
           mv -f $(CONFIG_GEN).tmp $(CONFIG_GEN); \
           echo "make: regenerated $$@ (NVME=$(CONFIG_NVME) AHCI=$(CONFIG_AHCI) VIRTIO_BLK=$(CONFIG_VIRTIO_BLK))"; \
         else rm -f $(CONFIG_GEN).tmp; fi
# the header must exist before ANY object compiles (order-only: it does
# not itself trigger rebuilds - the .d files do that)
$(KERNEL_OBJ) $(STUB_OBJ) $(GRUB_DATA_OBJ): | $(VERSION_GEN) $(CONFIG_GEN)

# Default goal: bare `make` still builds the kernel ELF (pre-WP-13
# behaviour); the WP-13 full pipeline is `make all` (identical to
# ./build.sh all and .\\build.ps1).
.DEFAULT_GOAL := kernel
.PHONY: all kernel iso src-zip ota reproducible-check run-bios run-uefi run-bios-gui run-uefi-gui run-bios-persist run-uefi-persist shot-bios shot-uefi clean dist userprogs userprogs-check

# ---- WP-13-build_system: unified full pipeline ----
# kernel -> hybrid BIOS+UEFI ISO -> deterministic SRC zip -> OTA package.
# Same entry point as tools/build.sh and build.ps1.
all: $(KERNEL_ISO) src-zip ota
	@echo "make all: full pipeline done"
	@echo "  kernel : $(KERNEL)"
	@echo "  ISO    : $(KERNEL_ISO)"
	@echo "  SRC    : $(SRC_ZIP)"
	@echo "  OTA    : $(OTA_PKG)"

# pre-WP-13 `make all` semantics (kernel ELF only).
kernel: $(KERNEL)

# ---- Build rules ----

$(BUILD):
	mkdir -p $(BUILD)

# WP-10-wireless_bluetooth: the wireless/sd driver set compiles at -Os so
# the kernel image stays inside the 0x400000 identity window (the linker
# ASSERT is byte-exact; measured over-run was 8 KiB at -O2).  Same
# per-file-override pattern as the AML -O0 rule below.
$(BUILD)/src/L0/drivers/wireless/driver_wireless.o: $(OC_ROOT)/src/L0/drivers/wireless/driver_wireless.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(OC_CC) $(filter-out -O2,$(CFLAGS)) -Os -c $< -o $@

$(BUILD)/src/L0/drivers/wireless/driver_iwlwifi.o: $(OC_ROOT)/src/L0/drivers/wireless/driver_iwlwifi.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(OC_CC) $(filter-out -O2,$(CFLAGS)) -Os -c $< -o $@

$(BUILD)/src/L0/drivers/wireless/driver_rtlwifi.o: $(OC_ROOT)/src/L0/drivers/wireless/driver_rtlwifi.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(OC_CC) $(filter-out -O2,$(CFLAGS)) -Os -c $< -o $@

$(BUILD)/src/L0/drivers/wireless/driver_ath.o: $(OC_ROOT)/src/L0/drivers/wireless/driver_ath.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(OC_CC) $(filter-out -O2,$(CFLAGS)) -Os -c $< -o $@

$(BUILD)/src/L0/drivers/wireless/driver_brcm.o: $(OC_ROOT)/src/L0/drivers/wireless/driver_brcm.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(OC_CC) $(filter-out -O2,$(CFLAGS)) -Os -c $< -o $@

$(BUILD)/src/L0/drivers/sd/driver_sdhci.o: $(OC_ROOT)/src/L0/drivers/sd/driver_sdhci.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(OC_CC) $(filter-out -O2,$(CFLAGS)) -Os -c $< -o $@


# C source → object file (with header dependency tracking, BUG-025 FIX)
# WP-10-camera_misc: the camera (JPEG decoder + UVC parser) set compiles
# at -Os for the same codegen-sensitivity reason as the wireless set
# above: -O2 miscompiles a working path (segfault pattern caught by the
# 4:4:4 fixture on the host build), -Os is clean and the decoder is
# not performance-critical (one frame at a time).
$(BUILD)/src/L0/drivers/camera/lib_mjpeg.o: $(OC_ROOT)/src/L0/drivers/camera/lib_mjpeg.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(OC_CC) $(filter-out -O2,$(CFLAGS)) -Os -c $< -o $@

$(BUILD)/src/L0/drivers/camera/driver_camera.o: $(OC_ROOT)/src/L0/drivers/camera/driver_camera.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(OC_CC) $(filter-out -O2,$(CFLAGS)) -Os -c $< -o $@

$(BUILD)/src/L0/drivers/camera/driver_camera_uvc.o: $(OC_ROOT)/src/L0/drivers/camera/driver_camera_uvc.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(OC_CC) $(filter-out -O2,$(CFLAGS)) -Os -c $< -o $@

# WP-10-input_chipset: the AML interpreter is compiled at -O0. It runs
# once at boot; the -O2 codegen of this file interacted badly with the
# interpreter's pointer-heavy value model (boot crash in kfree with a
# wild pointer, codegen-sensitive across builds) while -O0 parses the
# same tables cleanly (verified against the live QEMU DSDT).
$(BUILD)/src/L0/drivers/acpi/driver_acpi_aml.o: $(OC_ROOT)/src/L0/drivers/acpi/driver_acpi_aml.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(OC_CC) $(filter-out -O2,$(CFLAGS)) -O0 -c $< -o $@

# WP-10-tui-native: the native desktop editor + file-manager upgrades
# compile at -Os so the kernel image stays inside the 0x400000 identity
# window (same per-file-override pattern as the wireless -Os rules; the
# -O2 codegen over-ran the linker ASSERT with the new native editor).
$(BUILD)/tui/tui_dt_editor.o: $(OC_ROOT)/tui/tui_dt_editor.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(OC_CC) $(filter-out -O2,$(CFLAGS)) -Os -c $< -o $@

$(BUILD)/tui/tui_dt_files.o: $(OC_ROOT)/tui/tui_dt_files.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(OC_CC) $(filter-out -O2,$(CFLAGS)) -Os -c $< -o $@

$(BUILD)/%.o: $(OC_ROOT)/%.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(OC_CC) $(CFLAGS) -c $< -o $@

# Assembly source (src/L0/boot/) → object file
$(BUILD)/boot_%.o: $(OC_ROOT)/src/L0/boot/%.S | $(BUILD)
	$(NASM) $(ASFLAGS) $< -o $@

# Assembly source (src/L0/core/arch/x86_64/) → object file
$(BUILD)/core/arch/%.o: $(OC_ROOT)/src/L0/core/arch/%.S | $(BUILD)
	@mkdir -p $(dir $@)
	$(NASM) $(ASFLAGS) $< -o $@

# Link the kernel ELF in two stages (see WP-10d-pre note above), then
# strip debug info (removes build-machine paths)
$(STUB_OBJ): $(OC_ROOT)/src/L0/boot/grub_boot_stub.c $(OC_ROOT)/src/L0/boot/grub_boot_data.h | $(BUILD)
	@mkdir -p $(dir $@)
	$(OC_CC) $(CFLAGS) -c $< -o $@

$(BASE_ELF): $(KERNEL_OBJ) $(STUB_OBJ) $(OC_ROOT)/src/L0/core/arch/x86_64/linker.ld | $(BUILD)
	$(LD) $(LDFLAGS) -o $@ $(KERNEL_OBJ) $(STUB_OBJ)
	strip --strip-debug $@

$(GRUB_DATA_C): $(OC_ROOT)/tools/embed_grub.py $(BASE_ELF) \
                $(OC_ROOT)/src/L0/boot/grub_boot_data.h | $(BUILD)
	python3 $(OC_ROOT)/tools/embed_grub.py "$(OC_TOOLS)" $(BASE_ELF) $@

$(GRUB_DATA_OBJ): $(GRUB_DATA_C) $(OC_ROOT)/src/L0/boot/grub_boot_data.h | $(BUILD)
	@mkdir -p $(dir $@)
	$(OC_CC) $(CFLAGS) -c $< -o $@

$(KERNEL): $(BASE_ELF) $(GRUB_DATA_OBJ) $(OC_ROOT)/src/L0/core/arch/x86_64/linker.ld | $(BUILD)
	$(LD) $(LDFLAGS) -o $@ $(KERNEL_OBJ) $(GRUB_DATA_OBJ)
	strip --strip-debug $@

# BUG-025 FIX: Header dependency tracking (-MMD -MP generates .d files)
-include $(KERNEL_OBJ:.o=.d)

# ---- ISO build (3.0.0-WP05 B5-7: the GRUB chain is GONE) ----
# tools/build_iso.sh now stages the DUAL-FIRMWARE OCBL pair (BIOS
# stage1+core + UEFI PE) and packs through the OCBL ISO packer - the
# same artifact boots under SeaBIOS and OVMF, and the OCBL scanner
# pulls OCOS (this ISO) / OCIRE / foreign loaders from the fleet.
$(KERNEL_ISO): $(KERNEL) $(BUILD)/ocbl/ocbl_bios.bin \
               $(BUILD)/ocbl/ocbl_x64.efi $(OC_ROOT)/tools/build_iso.sh
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

# ---- WP-13-build_system: deterministic source zip ---------------------
# git archive gives byte-stable output for a given commit: entries in
# tree order, mtimes = commit time, permissions from the git index, no
# uid/gid/extra fields.  Heavy non-source trees are excluded by pathspec
# (same exclusion set the release chain uses).  Two builds of the same
# commit produce identical zips.
SRC_ZIP := $(DIST)/OpenCubeOS-src-$(OC_RELEASE_VERSION).zip
space := $(empty) $(empty)
SRC_EXCLUDE := website/public/downloads website/out website/.next website/node_modules \
               build dist releases .zscripts public/deliverables evidence
src-zip:
	@mkdir -p $(DIST)
	@git -C $(OC_ROOT) ls-files \
           | grep -vE '^($(subst $(space),|,$(SRC_EXCLUDE)))(/|$$)' > $(DIST)/.srclist \
           || true
	@if [ ! -s $(DIST)/.srclist ]; then echo "src-zip: not a git checkout - use 'make dist'" >&2; exit 1; fi
	@git -C $(OC_ROOT) archive --format=zip -o $(SRC_ZIP) HEAD -- $$(cat $(DIST)/.srclist)
	@rm -f $(DIST)/.srclist
	@echo "src-zip: $(SRC_ZIP) ($$(stat -c%s $(SRC_ZIP)) bytes)"

# ---- WP-13-build_system: OTA update package ---------------------------
OTA_PKG := $(DIST)/OpenCubeOS-$(OC_RELEASE_VERSION)-update.tar.gz
# v2.0.0: REAL FIX for the 1.0.0 incremental-build trap - `make
# src-zip ota` used to re-ship a stale ISO next to a fresh OTA
# (mixed-kernel package set).  The OTA package now also depends
# on the ISO image, so any kernel change forces BOTH artifacts
# to rebuild together; a stale-ISO + fresh-OTA set is no longer
# producible with the documented targets.
ota: $(KERNEL) $(KERNEL_ISO)
	@mkdir -p $(DIST)
	bash $(OC_ROOT)/tools/make_update_pkg.sh "$(OC_RELEASE_VERSION)" "$(KERNEL)" "$(OTA_PKG)"
	@echo "ota: $(OTA_PKG)"

# ---- WP-13-build_system: reproducibility check ------------------------
# Two clean builds from the same tree; the kernel ELF must be
# byte-identical.  Determinism sources verified: no __DATE__/__TIME__/
# __TIMESTAMP__ macros in the tree, the OCBL ISO packer pins every
# timestamp/GUID/volume-serial (3.0.0-WP05: GRUB is gone, the kernel-
# internal installer payload from embed_grub.py is unchanged and its
# inputs are stable), gcc/ld are deterministic for identical
# inputs+flags, link order is fixed by the wildcard expansion (same
# filesystem), and strip --strip-debug removes the path-bearing
# sections.  SOURCE_DATE_EPOCH is honoured when the caller exports it
# (forwarded to sub-tools that embed timestamps); the ELF itself does
# not need it.
reproducible-check:
	@echo "== reproducible-check: build 1/2 =="
	@$(MAKE) --no-print-directory clean >/dev/null
	@$(MAKE) --no-print-directory kernel >/dev/null
	@sha256sum $(KERNEL)
	@cp $(KERNEL) /tmp/.oc_repro_1.elf
	@echo "== reproducible-check: build 2/2 (clean again) =="
	@$(MAKE) --no-print-directory clean >/dev/null
	@$(MAKE) --no-print-directory kernel >/dev/null
	@if cmp -s /tmp/.oc_repro_1.elf $(KERNEL); then \
           echo "reproducible-check: PASS - ELF byte-identical across two clean builds"; \
           sha256sum $(KERNEL); rm -f /tmp/.oc_repro_1.elf; \
         else \
           echo "reproducible-check: FAIL - ELF differs:"; \
           sha256sum /tmp/.oc_repro_1.elf $(KERNEL); \
           cmp /tmp/.oc_repro_1.elf $(KERNEL) | head -3; \
           rm -f /tmp/.oc_repro_1.elf; exit 1; \
         fi

# ---- WP-13-build_system: config management targets --------------------
.PHONY: defconfig menuconfig
defconfig:
	@{ echo '# Open Cube OS build configuration (WP-13) - defaults'; \
           echo '# Written by make defconfig; edit with make menuconfig'; \
           echo 'CONFIG_NVME=y'; \
           echo 'CONFIG_AHCI=y'; \
           echo 'CONFIG_VIRTIO_BLK=y'; \
         } > $(OC_ROOT)/.config
	@echo "defconfig: wrote $(OC_ROOT)/.config (all options y)"

menuconfig:
	bash $(OC_ROOT)/tools/oc_menuconfig.sh

# ---- WP-13-build_system: compile cache management ---------------------
OC_CACHE_DIR ?= $(HOME)/.cache/opencube-cc
.PHONY: cache-stats cache-clear
cache-stats:
	@if [ -f $(OC_CACHE_DIR)/STATS ]; then \
           h=$$(awk '/^HIT/{n++}END{print n+0}' $(OC_CACHE_DIR)/STATS); \
           m=$$(awk '/^MISS/{n++}END{print n+0}' $(OC_CACHE_DIR)/STATS); \
           t=$$((h+m)); \
           if [ $$t -gt 0 ]; then pct=$$((100*h/t)); else pct=0; fi; \
           echo "compile cache: $$h hits / $$m misses ($$pct% hit rate)"; \
           echo "entries: $$(ls $(OC_CACHE_DIR)/*.o 2>/dev/null | wc -l)"; \
         else echo "compile cache: no STATS yet (first build populates it)"; fi
cache-clear:
	rm -rf $(OC_CACHE_DIR)
	@echo "compile cache: cleared $(OC_CACHE_DIR)"

# ---- Host test suite (WP-10-tui: closed the 15-command drift gap) ----
# tools/run_host_tests.py extracts each tests/host_*.c build line, remaps
# the WP-10-project_restructure source/include moves, compiles and runs
# the harness. Exit code = number of failed tests. rsa/x509 compile but
# skip their run step (they need openssl-generated external fixtures).
.PHONY: test
test:
	python3 tools/run_host_tests.py

# ---- Embedded-user-program targets (embed chain) ----
# 3.0.0-WP04: full regeneration via tools/gen_userprogs.py - it
# discovers every src/L1/<name>/ app folder (one command = one app =
# one folder; name = folder) plus every legacy flat program with its
# v2.x build mode, builds them all, and emits userprogs_data.h with
# the OC_USERPROGS[] name registry (the execve surface - no name is
# hand-registered in the kernel). nasm/gcc must be on PATH.
userprogs:
	python3 $(OC_ROOT)/tools/gen_userprogs.py

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
	(cd $(OC_ROOT) && zip -qr $$SRCZIP . -x "build/*" "dist/*" ".git/*" "tools/push_to_git.py" "releases/*" "website/rw*.sh" "website/build-local.sh" "website/public/downloads/*" "website/out/*" "website/.next/*" "website/node_modules/*" "src/L0/core/generated/*"); \
	cp $(KERNEL_ISO) $$ISOCOPY; \
	echo "SRC: $$SRCZIP"; \
	echo "ISO: $$ISOCOPY"; \
	sha256sum $$ISOCOPY | tee $$ISOCOPY.sha256

# ---- 3.0.0-WP-01-ocire-core: OCIRE independent recovery kernel ----
include $(OC_ROOT)/ocire/ocire.mk

# ---- 3.0.0-WP-05-ocbl: OCBL boot loader (the GRUB replacement) ----
include $(OC_ROOT)/ocbl/ocbl.mk

endif # ARCH dispatch

# ---- WP-14-docs_release: documentation data generation ----
# generate_api_docs(): extracts the real command table (runtime capture),
# the L1 interface table (surface headers) and source statistics into
# docs/data/*.json.  HELP_CAPTURE points at a `help -a` runtime capture
# (see evidence/help_a_runtime.txt; regenerate with tools/gen_api_docs.py
# --qemu build/opencube.iso).
api-docs:
	python3 $(OC_ROOT)/tools/gen_api_docs.py --help-capture $(OC_ROOT)/evidence/help_a_runtime.txt
