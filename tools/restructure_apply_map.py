#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Phase 2a (final): build symbol rename map v2 and apply repo-wide.
import os, re, sys, json
from collections import defaultdict

ROOT = "/home/z/my-project/oc-os"
CODE_DIRS = ["kernel", "drivers", "fs", "net", "shell", "l1", "boot", "tools",
             "tests", "userprogs", "docs"]
EXTRA_FILES = ["README.md", "MANIFEST.md"]

# ---- exact token renames (checked before prefix rules) ----------------------
EXACT = {
    "kmain": "main",
    "_start": None,          # keep (link entry)
    "screen_console": "screen_console",
    "arch_irq": "arch_irq",
    "screen_renderer": "screen_renderer",
    "screen_active_renderer": "screen_active_renderer",
    "screen_default_renderer": "screen_default_renderer",
    "crypto_root_t": "crypto_root_t",
    "crypto_roots": "crypto_roots",
    "gunzip": "gunzip",
    "driver_input_kbd_handler_fn": "driver_input_kbd_handler_fn",
    "arch_load_gdt_idt_tr": "arch_load_gdt_idt_tr",
    "arch_set_tss_rsp0": "arch_set_tss_rsp0",
    "timer_soft_timer_t": "timer_soft_timer_t",
    "update_check_": "update_check_",          # comment text only
    "container_of": "container_of",
    # crypto bare helpers / scalar types
    "sha256": "crypto_sha256", "sha384": "crypto_sha384", "sha512": "crypto_sha512",
    "x25519": "crypto_x25519", "fe25519": "crypto_fe25519",
    "aes_ctx_t": "crypto_aes_ctx_t", "ghash_ctx_t": "crypto_ghash_ctx_t",
    "poly1305_ctx_t": "crypto_poly1305_ctx_t", "poly1305_mac": "crypto_poly1305_mac",
    "dh_modexp": "crypto_dh_modexp", "dh_modexp_n": "crypto_dh_modexp_n",
    "ecdsa_verify": "crypto_ecdsa_verify", "hkdf_expand": "crypto_hkdf_expand",
    "der_t": "crypto_der_t", "integer_cbrt": "crypto_integer_cbrt",
    # arch asm entries
    "enter_ring3": "arch_enter_ring3", "enter_ring3_fork": "arch_enter_ring3_fork",
    # user address-space helpers (mem)
    "copy_user_address_space": "mem_vmm_copy_user_address_space",
    "create_user_address_space": "mem_vmm_create_user_address_space",
    "map_user_pages": "mem_vmm_map_user_pages",
    # misc internal types
    "task_t": "sched_task_t",
    "ser_type_t": "driver_usb_serial_type_t",
    "tok_kind_t": "shell_tok_kind_t",
    "token_t": "shell_token_t",
    "install_ctx_t": "ota_install_ctx_t",
    "one_shot_ctx_t": "gzip_one_shot_ctx_t",
    "crash_log_entry_t": "arch_crash_log_entry_t",
    "cubic_state_t": "net_cc_cubic_state_t",
    "nf_ct_t": "net_filter_ct_t", "nf_dev_t": "net_filter_dev_t",
    "nf_init_core": "net_filter_init_core", "nf_rule_t": "net_filter_rule_t",
}

# ---- prefix rules (longest first at apply time) ------------------------------
RULES = [
    ("screen_console_in_", "screen_console_in_"),
    ("screen_console_",    "screen_console_"),
    ("screen_serial_in_",  "screen_serial_in_"),
    ("screen_fb_",         "screen_fb_"),
    ("screen_renderer_",   "screen_renderer_"),
    ("driver_input_keyboard_",   "driver_input_keyboard_"),
    ("screen_color_rgb",   "screen_color_rgb"),
    ("screen_font_",       "screen_font_"),
    ("config_",     "config_"),
    ("ab_",         "ab_"),
    ("arch_exc_",        "arch_exc_"),
    ("arch_irq_",        "arch_irq_"),
    ("arch_pic_",        "arch_pic_"),
    ("arch_idt_",        "arch_idt_"),
    ("arch_gdt_",        "arch_gdt_"),
    ("arch_tss",         "arch_tss"),
    ("arch_isr_",        "arch_isr_"),
    ("l1_ext_",        "l1_ext_"),
    ("arch_context_switch", "arch_context_switch"),
    ("log_",        "log_"),
    ("timer_",      "timer_"),
    ("gzip_",       "gzip_"),
    ("tar_",        "tar_"),
    ("update_",     "update_"),
    ("mb2_",        "mb2_"),
    ("grub_",       "grub_"),
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
    ("bmdma_",     "driver_block_bmdma_"),
    ("ata_",       "driver_block_ata_"),
    ("ahci_",      "driver_block_ahci_"),
    ("nvme_",      "driver_block_nvme_"),
    ("virtio_blk_", "driver_block_virtio_blk_"),
    ("virtio_snd_", "driver_snd_virtio_"),
    ("vsnd_",      "driver_snd_virtio_"),
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
    # per-chip driver internals
    ("e1000e_",  "driver_nic_e1000e_"),
    ("bcm57xx_", "driver_nic_bcm57xx_"),
    ("igb_",     "driver_nic_igb_"),
    ("ixgbe_",   "driver_nic_ixgbe_"),
    ("rtl8139_", "driver_nic_rtl8139_"),
    ("rtl8168_", "driver_nic_rtl8168_"),
    ("rtl8169_", "driver_nic_rtl8169_"),
    ("rtl8125_", "driver_nic_rtl8125_"),
    ("rtl810x_", "driver_nic_rtl810x_"),
    ("other_nics_", "driver_nic_other_"),
    ("c3_",      "driver_nic_c3_"),
    ("ehci_",    "driver_usb_ehci_"),
    ("ohci_",    "driver_usb_ohci_"),
    ("uhci_",    "driver_usb_uhci_"),
    ("xhci_",    "driver_usb_xhci_"),
    # generic driver categories
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
    ("icmp_",    "net_icmp_"),
    ("ip_",      "net_ip_"),
    ("eth_",     "net_eth_"),
    ("dhcp_",    "net_dhcp_"),
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
    ("aes128_",  "crypto_aes128_"),
    ("aes256_",  "crypto_aes256_"),
    ("aes_",     "crypto_aes_"),
    ("chacha20poly1305_", "crypto_chacha20poly1305_"),
    ("chacha20_", "crypto_chacha20_"),
    ("bn_", "crypto_bn_"), ("rsa_", "crypto_rsa_"), ("sha512_", "crypto_sha512_"),
    ("sha384_", "crypto_sha384_"), ("sha256_", "crypto_sha256_"),
    ("aead_", "crypto_aead_"), ("curve25519_", "crypto_curve25519_"),
    ("x25519_", "crypto_x25519_"), ("fe25519_", "crypto_fe25519_"),
    ("ec_nist_", "crypto_ec_nist_"),
    ("ec_", "crypto_ec_"), ("x509_", "crypto_x509_"), ("hmac_", "crypto_hmac_"),
    ("dh_", "crypto_dh_"),
    # mem
    ("pmm_", "mem_pmm_"), ("vmm_", "mem_vmm_"), ("heap_", "mem_heap_"),
    # syscalls / proc / usermode / shell handlers
    ("sys_mprotect", "sys_mem_mprotect"), ("sys_mmap", "sys_mem_mmap"),
    ("sys_munmap", "sys_mem_munmap"), ("sys_brk", "sys_mem_brk"),
    ("sys_chdir", "sys_fs_chdir"), ("sys_getcwd", "sys_fs_getcwd"),
    ("sys_ioctl", "sys_io_ioctl"), ("sys_poll", "sys_io_poll"),
    ("sys_select", "sys_io_select"),
    ("proc_", "sys_proc_"),
    ("user_", "usermode_"),
    ("cmd_",  "shell_cmd_"),
    ("font_", "screen_font_"),
    ("ext_wp8cd_", "l1_wp8cd_"), ("ext_wp8b_", "l1_wp8b_"), ("ext_wp8a_", "l1_wp8a_"),
    ("ext_wp7_", "l1_wp7_"), ("ext_wp3_", "l1_wp3_"), ("ext_wp2_", "l1_wp2_"),
]

# symbols whose FIRST underscore segment marks them compliant/industry: keep
KEEP_FIRST = set("""kmalloc kfree kzalloc krealloc kthread mutex sem spin spinlock cond
sched sync timer rtc power shell update log gzip tar job tool so multiboot2 syscall
mb2 panic kprintf console screen crypto random net usermode fd signal sigaction
pipe kernel pollfd elf elf64 dl dlopen dlsym dlclose ldso mkfs grub gpt mbr mmio
partition virtio virtq vq reloc symbol container_of memcpy memset memmove memcmp
strlen strcmp strncmp strcpy strncpy strcat strchr strcasecmp align_down align_up
crc32 u64_to_str u64_to_hex memcpy""".split())

SKIP = set("""_ start d e n t u64 void int bool char break continue return if else for
while do case goto switch sizeof default unsigned signed long short float double
union enum extern volatile register static struct const""".split())

def iter_files():
    for d in CODE_DIRS:
        for dp, _, fns in os.walk(os.path.join(ROOT, d)):
            for fn in fns:
                if fn.endswith((".c", ".h", ".S", ".inc", ".asm", ".ld", ".md", ".py", ".sh")):
                    yield os.path.join(dp, fn)
    for f in EXTRA_FILES:
        yield os.path.join(ROOT, f)

decl_re = re.compile(
    r'^\s*(?:const\s+|static\s+|inline\s+|unsigned\s+|signed\s+|long\s+|short\s+|'
    r'struct\s+|union\s+|enum\s+)*[A-Za-z_][A-Za-z0-9_]*\s*\*?\s*([a-z_][a-z0-9_]*)\s*\(',
    re.M)
typedef_re = re.compile(r'\}\s*([a-z_][a-z0-9_]*)\s*;', re.M)

def map_name(name):
    if name in EXACT:
        return EXACT[name]          # may be None => keep
    if name in SKIP:
        return None
    if name in KEEP_FIRST or name.split("_")[0] in KEEP_FIRST:
        return None
    for old, new in RULES:
        if name.startswith(old):
            return new + name[len(old):]
    return "?"                      # unresolved

def main():
    funcs, typedefs, oc_tokens = set(), set(), set()
    files = list(iter_files())
    for p in files:
        try:
            txt = open(p, encoding="utf-8", errors="replace").read()
        except Exception:
            continue
        if p.endswith((".c", ".h")):
            funcs.update(decl_re.findall(txt))
            typedefs.update(typedef_re.findall(txt))
        oc_tokens.update(re.findall(r'\boc_[a-z0-9_]+\b', txt))
    symbols = funcs | typedefs | oc_tokens
    mapping, unresolved, conflicts = {}, [], []
    for s in sorted(symbols):
        if s.startswith("oc_"):
            m = map_name(s)
            if m in (None, "?"):
                # oc_ must always resolve; try generic rules as fallback
                for old, new in RULES:
                    if s.startswith(old):
                        m = new + s[len(old):]
                        break
            if m == "?" or m is None:
                unresolved.append(s)
            else:
                mapping[s] = m
        else:
            m = map_name(s)
            if m == "?":
                unresolved.append(s)
    # conflict check: new name already a known symbol
    for old, new in mapping.items():
        if new in symbols and new != old:
            conflicts.append((old, new))
    print(f"symbols total: {len(symbols)}  mapped: {len(mapping)}  unresolved: {len(unresolved)}")
    for u in sorted(unresolved):
        print("  ?", u)
    if conflicts:
        print(f"CONFLICTS: {len(conflicts)}")
        for o, n in sorted(conflicts):
            print(f"  ! {o} -> {n} (already exists)")
    with open("/tmp/rename_map.json", "w") as f:
        json.dump(mapping, f, indent=0, sort_keys=True)
    # apply
    keys = sorted(mapping.keys(), key=len, reverse=True)
    pat_cache = {}
    for k in keys:
        if k.endswith("_"):
            pat_cache[k] = ("prefix", k, mapping[k])
        else:
            pat_cache[k] = ("word", re.compile(r'\b' + re.escape(k) + r'\b'), mapping[k])
    changed, total_subs = 0, 0
    for p in files:
        try:
            txt = open(p, encoding="utf-8", errors="replace").read()
        except Exception:
            continue
        new_txt, subs = txt, 0
        for k in keys:
            kind = pat_cache[k]
            if kind[0] == "prefix":
                if k in new_txt:
                    c = new_txt.count(k)
                    new_txt = new_txt.replace(k, kind[2])
                    subs += c
            else:
                new_txt, c = kind[1].subn(kind[2], new_txt)
                subs += c
        if new_txt != txt:
            open(p, "w").write(new_txt)
            changed += 1
            total_subs += subs
    print(f"files changed: {changed}  substitutions: {total_subs}")

if __name__ == "__main__":
    main()
