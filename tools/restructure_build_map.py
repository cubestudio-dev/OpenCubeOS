#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# WP-10-project_restructure-fix1 Phase 2a: build the exact symbol rename map
# (functions declared in headers + non-static definitions + typedefs), apply
# the naming rules, and report anything that still needs a manual decision.
import os, re, sys, json

ROOT = "/home/z/my-project/oc-os"
CODE_DIRS = ["kernel", "drivers", "fs", "net", "shell", "l1", "boot", "tools",
             "tests", "userprogs"]

# --- naming rules: longest prefix first -------------------------------------
RULES = [
    ("screen_console_in_", "screen_console_in_"),
    ("screen_console_",    "screen_console_"),
    ("serial_in_",     "screen_serial_in_"),
    ("screen_fb_",         "screen_fb_"),
    ("screen_renderer_",   "screen_renderer_"),
    ("driver_input_keyboard_",   "driver_input_keyboard_"),
    ("screen_color_rgb",   "screen_color_rgb"),
    ("config_",     "config_"),
    ("ab_",         "ab_"),
    ("arch_exc_",        "arch_exc_"),
    ("arch_irq_",        "arch_irq_"),
    ("arch_pic_",        "arch_pic_"),
    ("arch_context_switch", "arch_context_switch"),
    ("l1_ext_",        "l1_ext_"),
    ("memcpy", "memcpy"), ("memset", "memset"), ("memmove", "memmove"),
    ("memcmp", "memcmp"),
    ("strlen", "strlen"), ("strcmp", "strcmp"), ("strncmp", "strncmp"),
    ("strcpy", "strcpy"), ("strncpy", "strncpy"), ("strcat", "strcat"),
    ("strchr", "strchr"), ("strcasecmp", "strcasecmp"),
    ("u64_to_str", "u64_to_str"), ("u64_to_hex", "u64_to_hex"),
    ("align_down", "align_down"), ("align_up", "align_up"),
    ("crc32", "crc32"),
    ("update_check_async", "update_check_async"),
    ("update_check_strerror", "update_check_strerror"),
    ("update_check", "update_check"),
    # block / storage
    ("blk_cache_", "driver_block_cache_"),
    ("blk_",       "driver_block_"),
    ("ata_dma_",   "driver_block_ata_dma_"),
    ("ata_",       "driver_block_ata_"),
    ("ahci_",      "driver_block_ahci_"),
    ("nvme_",      "driver_block_nvme_"),
    ("virtio_blk_", "driver_block_virtio_blk_"),
    ("virtio_snd_", "driver_snd_virtio_"),
    ("part_",      "driver_block_part_"),
    ("disk_setup_", "driver_block_disk_setup_"),
    # shell command-group registrars
    ("disk_cmds_",      "shell_cmds_disk_"),
    ("disk_test_cmds_", "shell_cmds_disk_test_"),
    ("file_cmds_",      "shell_cmds_file_"),
    ("nic_test_cmds_",  "shell_cmds_nic_test_"),
    ("snd_test_cmds_",  "shell_cmds_snd_test_"),
    ("usb_test_cmds_",  "shell_cmds_usb_test_"),
    ("update_test_cmds_", "shell_cmds_update_test_"),
    ("power_test_cmds_",  "shell_cmds_power_test_"),
    # other drivers
    ("nic_",  "driver_nic_"),
    ("snd_",  "driver_snd_"),
    ("hda_",  "driver_snd_hda_"),
    ("ac97_", "driver_snd_ac97_"),
    ("sb16_", "driver_snd_sb16_"),
    ("es1370_", "driver_snd_es1370_"),
    ("usb_",  "driver_usb_"),
    ("pci_",  "driver_pci_"),
    # fs
    ("vfs_",   "fs_vfs_"),
    ("fat32_", "fs_fat32_"),
    ("exfat_", "fs_exfat_"),
    ("ext4_",  "fs_ext4_"),
    ("ramfs_", "fs_ramfs_"),
    # net
    ("tcp_cc_",  "net_tcp_cc_"),
    ("tcp_",     "net_tcp_"),
    ("udp_",     "net_udp_"),
    ("arp_",     "net_arp_"),
    ("dns_",     "net_dns_"),
    ("route_",   "net_route_"),
    ("tls13_",   "net_tls13_"),
    ("tls_",     "net_tls_"),
    ("sshd_",    "net_sshd_"),
    ("ssh_",     "net_ssh_"),
    ("cc_",      "net_cc_"),
    ("netfilter_", "net_filter_"),
    # crypto
    ("bn_", "crypto_bn_"), ("rsa_", "crypto_rsa_"), ("sha512_", "crypto_sha512_"),
    ("sha384_", "crypto_sha384_"), ("sha256_", "crypto_sha256_"),
    ("aead_", "crypto_aead_"), ("curve25519_", "crypto_curve25519_"),
    ("x25519_", "crypto_x25519_"), ("ec_nist_", "crypto_ec_nist_"),
    ("ec_", "crypto_ec_"), ("x509_", "crypto_x509_"), ("hmac_", "crypto_hmac_"),
    ("aes128_", "crypto_aes128_"),
    # mem
    ("pmm_", "mem_pmm_"), ("vmm_", "mem_vmm_"), ("heap_", "mem_heap_"),
    # syscalls / proc / usermode / shell cmd handlers
    ("sys_mprotect", "sys_mem_mprotect"), ("sys_mmap", "sys_mem_mmap"),
    ("sys_munmap", "sys_mem_munmap"), ("sys_brk", "sys_mem_brk"),
    ("sys_chdir", "sys_fs_chdir"), ("sys_getcwd", "sys_fs_getcwd"),
    ("sys_ioctl", "sys_io_ioctl"), ("sys_poll", "sys_io_poll"),
    ("sys_select", "sys_io_select"),
    ("proc_", "sys_proc_"),
    ("user_", "usermode_"),
    ("cmd_",  "shell_cmd_"),
    # font / display misc
    ("font_", "screen_font_"),
]

# symbols that must NOT be renamed (industry-standard / already compliant)
KEEP = set("""kmalloc kfree kzalloc krealloc kthread mutex sem spin cond sched sync
timer rtc power shell update log gzip tar job tool so multiboot2 syscall mb2
panic kprintf screen console crypto random main""".split())

def iter_code_files():
    for d in CODE_DIRS:
        for dp, _, fns in os.walk(os.path.join(ROOT, d)):
            for fn in fns:
                if fn.endswith((".c", ".h", ".S", ".inc")):
                    yield os.path.join(dp, fn)

decl_re = re.compile(
    r'^\s*(?:const\s+|static\s+|inline\s+|unsigned\s+|signed\s+|long\s+|short\s+|'
    r'struct\s+|union\s+|enum\s+)*[A-Za-z_][A-Za-z0-9_]*\s*\*?\s*([a-z_][a-z0-9_]*)\s*\(',
    re.M)
def_re = re.compile(
    r'^(?:const\s+|unsigned\s+|signed\s+|long\s+|short\s+|struct\s+|union\s+|enum\s+)*'
    r'[A-Za-z_][A-Za-z0-9_]*\s*\*?\s*([a-z_][a-z0-9_]*)\s*\([^;]*?\)\s*\{',
    re.M)
typedef_re = re.compile(r'\}\s*([a-z_][a-z0-9_]*)\s*;', re.M)

def main():
    funcs, typedefs = set(), set()
    for p in iter_code_files():
        txt = open(p, encoding="utf-8", errors="replace").read()
        if p.endswith(".h"):
            funcs.update(decl_re.findall(txt))
        if p.endswith(".c"):
            for m in def_re.finditer(txt):
                # exclude static definitions
                line_start = txt.rfind("\n", 0, m.start()) + 1
                if not txt[line_start:m.start()].lstrip().startswith("static"):
                    funcs.add(m.group(1))
        typedefs.update(typedef_re.findall(txt))
    oc_tokens = set()
    for p in iter_code_files():
        txt = open(p, encoding="utf-8", errors="replace").read()
        oc_tokens.update(re.findall(r'\boc_[a-z0-9_]+\b', txt))
    # map
    mapping, unknown = {}, set()
    for name in sorted(funcs | oc_tokens):
        if name in KEEP:
            continue
        if name.startswith("oc_"):
            for old, new in RULES:
                if name.startswith(old):
                    mapping[name] = new + name[len(old):]
                    break
            else:
                unknown.add(name)
        else:
            for old, new in RULES:
                if name.startswith(old):
                    mapping[name] = new + name[len(old):]
                    break
            else:
                unknown.add(name)
    # typedefs too
    for t in sorted(typedefs):
        if t in KEEP or t in mapping:
            continue
        for old, new in RULES:
            if t.startswith(old):
                mapping[t] = new + t[len(old):]
                break
        else:
            unknown.add(t)
    with open("/tmp/rename_map.json", "w") as f:
        json.dump(mapping, f, indent=0, sort_keys=True)
    print(f"symbols mapped: {len(mapping)}")
    print(f"UNKNOWN (no rule): {len(unknown)}")
    for u in sorted(unknown):
        print("  ?", u)

if __name__ == "__main__":
    main()
