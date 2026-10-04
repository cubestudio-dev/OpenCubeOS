#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Phase 1b: rewrite #include "old.h" -> "new.h" repo-wide after renames.
import os, re, sys

ROOT = "/home/z/my-project/oc-os"

# header basename map (only renamed headers; same-name-moved headers excluded)
H = {
    "idt.h": "arch_idt.h", "exceptions.h": "arch_exceptions.h", "irq.h": "arch_irq.h",
    "pic.h": "arch_pic.h", "multiboot2.h": "arch_multiboot2.h",
    "sched.h": "core_sched.h", "sync.h": "core_sync.h", "timer.h": "core_timer.h",
    "rtc.h": "core_rtc.h", "syscall.h": "core_syscall.h", "usermode.h": "core_usermode.h",
    "power.h": "core_power.h",
    "pmm.h": "mem_pmm.h", "vmm.h": "mem_vmm.h", "heap.h": "mem_heap.h",
    "string.h": "lib_string.h", "log.h": "lib_log.h", "gzip.h": "lib_gzip.h",
    "tar.h": "lib_tar.h", "config.h": "lib_config.h",
    "crypto.h": "crypto_core.h", "bn.h": "crypto_bn.h", "rsa.h": "crypto_rsa.h",
    "sha512.h": "crypto_sha512.h", "aead.h": "crypto_aead.h",
    "curve25519.h": "crypto_curve25519.h", "ec_nist.h": "crypto_ec_nist.h",
    "x509.h": "crypto_x509.h", "dh_scale_vectors.h": "crypto_dh_scale_vectors.h",
    "roots.h": "crypto_roots.h",
    "update.h": "ota_update.h", "ab_update.h": "ota_ab.h",
    "blk.h": "driver_block_blk.h", "blk_cache.h": "driver_block_cache.h",
    "ata.h": "driver_block_ata.h", "ata_dma.h": "driver_block_ata_dma.h",
    "ahci.h": "driver_block_ahci.h", "nvme.h": "driver_block_nvme.h",
    "virtio_blk.h": "driver_block_virtio_blk.h", "part.h": "driver_block_part.h",
    "disk_setup.h": "driver_block_disk_setup.h",
    "nic.h": "driver_nic.h",
    "snd.h": "driver_snd.h", "hda.h": "driver_snd_hda.h", "ac97.h": "driver_snd_ac97.h",
    "sb16.h": "driver_snd_sb16.h", "es1370.h": "driver_snd_es1370.h",
    "virtio_snd.h": "driver_snd_virtio.h",
    "usb.h": "driver_usb.h", "usb_hid.h": "driver_usb_hid.h", "usb_msc.h": "driver_usb_msc.h",
    "usb_serial.h": "driver_usb_serial.h", "usb_audio.h": "driver_usb_audio.h",
    "keyboard.h": "driver_input_keyboard.h",
    "fb.h": "screen_fb.h", "font.h": "screen_font.h",
    "console.h": "screen_console.h", "console_in.h": "screen_console_in.h",
    "serial_in.h": "screen_serial_in.h",
    "pci.h": "driver_pci.h",
    "vfs.h": "fs_vfs.h", "ramfs.h": "fs_ramfs.h", "fat32.h": "fs_fat32.h",
    "exfat.h": "fs_exfat.h", "ext4.h": "fs_ext4.h",
    "net.h": "net_core.h", "tcp_cc.h": "net_tcp_cc.h", "tls.h": "net_tls.h",
    "ssh.h": "net_ssh.h", "sshd_rsa_key.h": "net_sshd_rsa_key.h",
    "shell.h": "shell.h",  # unchanged name, kept for completeness
    "file_cmds.h": "shell_cmds_file.h", "disk_cmds.h": "shell_cmds_disk.h",
    "disk_test_cmds.h": "shell_cmds_disk_test.h", "nic_test_cmds.h": "shell_cmds_nic_test.h",
    "snd_test_cmds.h": "shell_cmds_snd_test.h", "usb_test_cmds.h": "shell_cmds_usb_test.h",
    "update_test_cmds.h": "shell_cmds_update_test.h", "power_test_cmds.h": "shell_cmds_power_test.h",
    "ext.h": "l1_ext.h", "ext_wp2.h": "l1_wp2.h", "ext_wp3.h": "l1_wp3.h",
    "ext_wp7.h": "l1_wp7.h", "ext_wp8a.h": "l1_wp8a.h", "ext_wp8b.h": "l1_wp8b.h",
    "ext_wp8cd.h": "l1_wp8cd.h",
}

SCAN_DIRS = ["kernel", "drivers", "fs", "net", "shell", "l1", "boot", "tools",
             "tests", "userprogs", "docs", "etc"]
SCAN_FILES = ["Makefile", "README.md", "MANIFEST.md", "linker.ld", "grub.cfg", "grub-ab.cfg",
              "tools/restructure_move.py"]

inc_re = re.compile(r'(#\s*include\s*")([^"]+)(")')

def main():
    total = {}
    for d in SCAN_DIRS:
        for dp, _, fns in os.walk(os.path.join(ROOT, d)):
            for fn in fns:
                p = os.path.join(dp, fn)
                if not os.path.isfile(p):
                    continue
                try:
                    with open(p, "r", encoding="utf-8", errors="replace") as f:
                        txt = f.read()
                except Exception:
                    continue
                new = inc_re.sub(lambda m: m.group(1) + H.get(m.group(2), m.group(2)) + m.group(3), txt)
                if new != txt:
                    with open(p, "w") as f:
                        f.write(new)
                    total[p] = total.get(p, 0) + 1
    for p in SCAN_FILES:
        fp = os.path.join(ROOT, p)
        if not os.path.isfile(fp):
            continue
        with open(fp, "r", encoding="utf-8", errors="replace") as f:
            txt = f.read()
        new = inc_re.sub(lambda m: m.group(1) + H.get(m.group(2), m.group(2)) + m.group(3), txt)
        if new != txt:
            with open(fp, "w") as f:
                f.write(new)
            total[p] = 1
    print(f"files with include rewrites: {len(total)}")
    sys.exit(0)

if __name__ == "__main__":
    main()
