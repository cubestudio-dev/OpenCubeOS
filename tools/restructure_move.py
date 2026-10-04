#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# WP-10-project_restructure-fix1: Phase 1 - move/rename files per the new
# layout spec (one folder per module, [category]_[specific] file names).
# Uses `git mv` so history follows the files.
import subprocess, sys, os

ROOT = "/home/z/my-project/oc-os"

# old (relative to ROOT) -> new (relative to ROOT)
MOVES = {
    # ---- boot ----
    "kernel/grub_boot_stub.c":  "boot/grub_boot_stub.c",
    "kernel/grub_boot_data.h":  "kernel/generated/grub_boot_data.h",
    # ---- kernel root ----
    "kernel/kmain.c":           "kernel/main.c",
    "kernel/userprogs_data.h":  "kernel/generated/userprogs_data.h",
    "kernel/solib_data.h":      "kernel/generated/solib_data.h",
    # ---- kernel/arch/x86_64 ----
    "kernel/idt.c":             "kernel/arch/x86_64/arch_idt.c",
    "kernel/idt.h":             "kernel/arch/x86_64/arch_idt.h",
    "kernel/idt_stub.S":        "kernel/arch/x86_64/arch_idt_stub.S",
    "kernel/idt_load.S":        "kernel/arch/x86_64/arch_idt_load.S",
    "kernel/exceptions.c":      "kernel/arch/x86_64/arch_exceptions.c",
    "kernel/exceptions.h":      "kernel/arch/x86_64/arch_exceptions.h",
    "kernel/irq.c":             "kernel/arch/x86_64/arch_irq.c",
    "kernel/irq.h":             "kernel/arch/x86_64/arch_irq.h",
    "kernel/pic.h":             "kernel/arch/x86_64/arch_pic.h",
    "kernel/context_switch.S":  "kernel/arch/x86_64/arch_context_switch.S",
    "kernel/enter_ring3_fork.S":"kernel/arch/x86_64/arch_enter_ring3_fork.S",
    "kernel/multiboot2.c":      "kernel/arch/x86_64/arch_multiboot2.c",
    "kernel/multiboot2.h":      "kernel/arch/x86_64/arch_multiboot2.h",
    # ---- kernel/core ----
    "kernel/sched.c":           "kernel/core/core_sched.c",
    "kernel/sched.h":           "kernel/core/core_sched.h",
    "kernel/sync.c":            "kernel/core/core_sync.c",
    "kernel/sync.h":            "kernel/core/core_sync.h",
    "kernel/timer.c":           "kernel/core/core_timer.c",
    "kernel/timer.h":           "kernel/core/core_timer.h",
    "kernel/rtc.c":             "kernel/core/core_rtc.c",
    "kernel/rtc.h":             "kernel/core/core_rtc.h",
    "kernel/syscall.c":         "kernel/core/core_syscall.c",
    "kernel/syscall.h":         "kernel/core/core_syscall.h",
    "kernel/usermode.c":        "kernel/core/core_usermode.c",
    "kernel/usermode.h":        "kernel/core/core_usermode.h",
    "kernel/power.c":           "kernel/core/core_power.c",
    "kernel/power.h":           "kernel/core/core_power.h",
    # ---- kernel/mem ----
    "kernel/pmm.c":             "kernel/mem/mem_pmm.c",
    "kernel/pmm.h":             "kernel/mem/mem_pmm.h",
    "kernel/vmm.c":             "kernel/mem/mem_vmm.c",
    "kernel/vmm.h":             "kernel/mem/mem_vmm.h",
    "kernel/heap.c":            "kernel/mem/mem_heap.c",
    "kernel/heap.h":            "kernel/mem/mem_heap.h",
    # ---- kernel/lib ----
    "kernel/string.c":          "kernel/lib/lib_string.c",
    "kernel/string.h":          "kernel/lib/lib_string.h",
    "kernel/log.c":             "kernel/lib/lib_log.c",
    "kernel/log.h":             "kernel/lib/lib_log.h",
    "kernel/gzip.c":            "kernel/lib/lib_gzip.c",
    "kernel/gzip.h":            "kernel/lib/lib_gzip.h",
    "kernel/tar.c":             "kernel/lib/lib_tar.c",
    "kernel/tar.h":             "kernel/lib/lib_tar.h",
    "kernel/config.c":          "kernel/lib/lib_config.c",
    "kernel/config.h":          "kernel/lib/lib_config.h",
    # ---- kernel/crypto ----
    "kernel/crypto.c":          "kernel/crypto/crypto_core.c",
    "kernel/crypto.h":          "kernel/crypto/crypto_core.h",
    "kernel/bn.c":              "kernel/crypto/crypto_bn.c",
    "kernel/bn.h":              "kernel/crypto/crypto_bn.h",
    "kernel/rsa.c":             "kernel/crypto/crypto_rsa.c",
    "kernel/rsa.h":             "kernel/crypto/crypto_rsa.h",
    "kernel/sha512.c":          "kernel/crypto/crypto_sha512.c",
    "kernel/sha512.h":          "kernel/crypto/crypto_sha512.h",
    "kernel/aead.c":            "kernel/crypto/crypto_aead.c",
    "kernel/aead.h":            "kernel/crypto/crypto_aead.h",
    "kernel/curve25519.c":      "kernel/crypto/crypto_curve25519.c",
    "kernel/curve25519.h":      "kernel/crypto/crypto_curve25519.h",
    "kernel/ec_nist.c":         "kernel/crypto/crypto_ec_nist.c",
    "kernel/ec_nist.h":         "kernel/crypto/crypto_ec_nist.h",
    "kernel/x509.c":            "kernel/crypto/crypto_x509.c",
    "kernel/x509.h":            "kernel/crypto/crypto_x509.h",
    "kernel/dh_scale_vectors.h":"kernel/crypto/crypto_dh_scale_vectors.h",
    "kernel/roots.h":           "kernel/crypto/crypto_roots.h",
    # ---- kernel/ota ----
    "kernel/update.c":          "kernel/ota/ota_update.c",
    "kernel/update.h":          "kernel/ota/ota_update.h",
    "kernel/ab_update.c":       "kernel/ota/ota_ab.c",
    "kernel/ab_update.h":       "kernel/ota/ota_ab.h",
    "kernel/update_test_vectors.inc": "kernel/ota/update_test_vectors.inc",
    # ---- drivers/block ----
    "kernel/blk.c":             "drivers/block/driver_block_blk.c",
    "kernel/blk.h":             "drivers/block/driver_block_blk.h",
    "kernel/blk_cache.c":       "drivers/block/driver_block_cache.c",
    "kernel/blk_cache.h":       "drivers/block/driver_block_cache.h",
    "kernel/ata.c":             "drivers/block/driver_block_ata.c",
    "kernel/ata.h":             "drivers/block/driver_block_ata.h",
    "kernel/ata_dma.c":         "drivers/block/driver_block_ata_dma.c",
    "kernel/ata_dma.h":         "drivers/block/driver_block_ata_dma.h",
    "kernel/ahci.c":            "drivers/block/driver_block_ahci.c",
    "kernel/ahci.h":            "drivers/block/driver_block_ahci.h",
    "kernel/nvme.c":            "drivers/block/driver_block_nvme.c",
    "kernel/nvme.h":            "drivers/block/driver_block_nvme.h",
    "kernel/virtio_blk.c":      "drivers/block/driver_block_virtio_blk.c",
    "kernel/virtio_blk.h":      "drivers/block/driver_block_virtio_blk.h",
    "kernel/part.c":            "drivers/block/driver_block_part.c",
    "kernel/part.h":            "drivers/block/driver_block_part.h",
    "kernel/disk_setup.c":      "drivers/block/driver_block_disk_setup.c",
    "kernel/disk_setup.h":      "drivers/block/driver_block_disk_setup.h",
    # ---- drivers/nic ----
    "kernel/nic.c":             "drivers/nic/driver_nic.c",
    "kernel/nic.h":             "drivers/nic/driver_nic.h",
    "kernel/nic_e1000e.c":      "drivers/nic/driver_nic_e1000e.c",
    "kernel/nic_igb.c":         "drivers/nic/driver_nic_igb.c",
    "kernel/nic_rtl8139.c":     "drivers/nic/driver_nic_rtl8139.c",
    "kernel/nic_rtl8169.c":     "drivers/nic/driver_nic_rtl8169.c",
    "kernel/nic_bcm57xx.c":     "drivers/nic/driver_nic_bcm57xx.c",
    "kernel/nic_ixgbe.c":       "drivers/nic/driver_nic_ixgbe.c",
    "kernel/nic_other.c":       "drivers/nic/driver_nic_other.c",
    # ---- drivers/snd ----
    "kernel/snd.c":             "drivers/snd/driver_snd.c",
    "kernel/snd.h":             "drivers/snd/driver_snd.h",
    "kernel/hda.c":             "drivers/snd/driver_snd_hda.c",
    "kernel/hda.h":             "drivers/snd/driver_snd_hda.h",
    "kernel/ac97.c":            "drivers/snd/driver_snd_ac97.c",
    "kernel/ac97.h":            "drivers/snd/driver_snd_ac97.h",
    "kernel/sb16.c":            "drivers/snd/driver_snd_sb16.c",
    "kernel/sb16.h":            "drivers/snd/driver_snd_sb16.h",
    "kernel/es1370.c":          "drivers/snd/driver_snd_es1370.c",
    "kernel/es1370.h":          "drivers/snd/driver_snd_es1370.h",
    "kernel/virtio_snd.c":      "drivers/snd/driver_snd_virtio.c",
    "kernel/virtio_snd.h":      "drivers/snd/driver_snd_virtio.h",
    # ---- drivers/usb ----
    "kernel/usb.c":             "drivers/usb/driver_usb.c",
    "kernel/usb.h":             "drivers/usb/driver_usb.h",
    "kernel/usb_ohci.c":        "drivers/usb/driver_usb_ohci.c",
    "kernel/usb_ehci.c":        "drivers/usb/driver_usb_ehci.c",
    "kernel/usb_xhci.c":        "drivers/usb/driver_usb_xhci.c",
    "kernel/usb_hid.c":         "drivers/usb/driver_usb_hid.c",
    "kernel/usb_hid.h":         "drivers/usb/driver_usb_hid.h",
    "kernel/usb_msc.c":         "drivers/usb/driver_usb_msc.c",
    "kernel/usb_msc.h":         "drivers/usb/driver_usb_msc.h",
    "kernel/usb_serial.c":      "drivers/usb/driver_usb_serial.c",
    "kernel/usb_serial.h":      "drivers/usb/driver_usb_serial.h",
    "kernel/usb_audio.c":       "drivers/usb/driver_usb_audio.c",
    "kernel/usb_audio.h":       "drivers/usb/driver_usb_audio.h",
    # ---- drivers/input ----
    "kernel/keyboard.c":        "drivers/input/driver_input_keyboard.c",
    "kernel/keyboard.h":        "drivers/input/driver_input_keyboard.h",
    # ---- drivers/display ----
    "kernel/fb.c":              "drivers/display/screen_fb.c",
    "kernel/fb.h":              "drivers/display/screen_fb.h",
    "kernel/font.c":            "drivers/display/screen_font.c",
    "kernel/font.h":            "drivers/display/screen_font.h",
    "kernel/font_data.c":       "drivers/display/screen_font_data.c",
    "kernel/console.c":         "drivers/display/screen_console.c",
    "kernel/console.h":         "drivers/display/screen_console.h",
    "kernel/console_in.c":      "drivers/display/screen_console_in.c",
    "kernel/console_in.h":      "drivers/display/screen_console_in.h",
    "kernel/serial_in.c":       "drivers/display/screen_serial_in.c",
    "kernel/serial_in.h":       "drivers/display/screen_serial_in.h",
    # ---- drivers/pci ----
    "kernel/pci.c":             "drivers/pci/driver_pci.c",
    "kernel/pci.h":             "drivers/pci/driver_pci.h",
    # ---- fs ----
    "kernel/vfs.c":             "fs/fs_vfs.c",
    "kernel/vfs.h":             "fs/fs_vfs.h",
    "kernel/ramfs.c":           "fs/fs_ramfs.c",
    "kernel/ramfs.h":           "fs/fs_ramfs.h",
    "kernel/fat32.c":           "fs/fs_fat32.c",
    "kernel/fat32.h":           "fs/fs_fat32.h",
    "kernel/exfat.c":           "fs/fs_exfat.c",
    "kernel/exfat.h":           "fs/fs_exfat.h",
    "kernel/ext4.c":            "fs/fs_ext4.c",
    "kernel/ext4.h":            "fs/fs_ext4.h",
    # ---- net ----
    "kernel/net.c":             "net/net_core.c",
    "kernel/net.h":             "net/net_core.h",
    "kernel/tcp_cc.c":          "net/net_tcp_cc.c",
    "kernel/tcp_cc.h":          "net/net_tcp_cc.h",
    "kernel/tls.c":             "net/net_tls.c",
    "kernel/tls.h":             "net/net_tls.h",
    "kernel/ssh.c":             "net/net_ssh.c",
    "kernel/ssh.h":             "net/net_ssh.h",
    "kernel/sshd.c":            "net/net_sshd.c",
    "kernel/sshd.h":            "net/net_sshd.h",
    # ---- shell ----
    "kernel/shell.c":           "shell/shell.c",
    "kernel/shell.h":           "shell/shell.h",
    "kernel/file_cmds.c":       "shell/shell_cmds_file.c",
    "kernel/file_cmds.h":       "shell/shell_cmds_file.h",
    "kernel/disk_cmds.c":       "shell/shell_cmds_disk.c",
    "kernel/disk_cmds.h":       "shell/shell_cmds_disk.h",
    "kernel/disk_test_cmds.c":  "shell/shell_cmds_disk_test.c",
    "kernel/disk_test_cmds.h":  "shell/shell_cmds_disk_test.h",
    "kernel/nic_test_cmds.c":   "shell/shell_cmds_nic_test.c",
    "kernel/nic_test_cmds.h":   "shell/shell_cmds_nic_test.h",
    "kernel/snd_test_cmds.c":   "shell/shell_cmds_snd_test.c",
    "kernel/snd_test_cmds.h":   "shell/shell_cmds_snd_test.h",
    "kernel/usb_test_cmds.c":   "shell/shell_cmds_usb_test.c",
    "kernel/usb_test_cmds.h":   "shell/shell_cmds_usb_test.h",
    "kernel/update_test_cmds.c":"shell/shell_cmds_update_test.c",
    "kernel/update_test_cmds.h":"shell/shell_cmds_update_test.h",
    "kernel/power_test_cmds.c": "shell/shell_cmds_power_test.c",
    "kernel/power_test_cmds.h": "shell/shell_cmds_power_test.h",
    # ---- l1 ----
    "kernel/ext.c":             "l1/l1_ext.c",
    "kernel/ext.h":             "l1/l1_ext.h",
    "kernel/ext_selftest.c":    "l1/l1_selftest.c",
    "kernel/ext_wp2.h":         "l1/l1_wp2.h",
    "kernel/ext_wp3.h":         "l1/l1_wp3.h",
    "kernel/ext_wp7.h":         "l1/l1_wp7.h",
    "kernel/ext_wp8a.c":        "l1/l1_wp8a.c",
    "kernel/ext_wp8a.h":        "l1/l1_wp8a.h",
    "kernel/ext_wp8b.c":        "l1/l1_wp8b.c",
    "kernel/ext_wp8b.h":        "l1/l1_wp8b.h",
    "kernel/ext_wp8cd.c":       "l1/l1_wp8cd.c",
    "kernel/ext_wp8cd.h":       "l1/l1_wp8cd.h",
}

def main():
    os.chdir(ROOT)
    done, errs = 0, []
    for old, new in MOVES.items():
        if not os.path.exists(old):
            errs.append(f"MISSING SRC: {old}")
            continue
        os.makedirs(os.path.dirname(new), exist_ok=True)
        r = subprocess.run(["git", "mv", old, new], capture_output=True, text=True)
        if r.returncode != 0:
            errs.append(f"FAIL {old} -> {new}: {r.stderr.strip()}")
        else:
            done += 1
    print(f"moved: {done}/{len(MOVES)}")
    for e in errs:
        print(e)
    # leftover check: what .c/.h/.S remain flat in kernel/ (should be only generated/ + .inc?)
    leftovers = subprocess.run(["ls", "kernel/"], capture_output=True, text=True).stdout.split()
    print("kernel/ now contains:", " ".join(sorted(leftovers)))
    sys.exit(1 if errs else 0)

if __name__ == "__main__":
    main()
