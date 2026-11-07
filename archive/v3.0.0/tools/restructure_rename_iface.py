#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# WP-10-project_restructure-fix1: Phase 2 - rename all exported interfaces to
# the new [category]_[specific] convention.  Word-boundary aware; string
# literals are protected; struct members / locals that collide with prefix
# names are explicitly shielded.
import re, glob, sys, collections

ROOT = "/home/z/my-project/oc-os"

# ---- 0. shield list: identifiers that must NOT be touched ---------------
# (struct members / function parameters / static locals that happen to
#  carry one of the old prefixes)
SHIELD = [
    # xHCI command-ring locals & descriptor fields (driver_usb_xhci.c)
    "cmd_tab", "cmd_list", "cmd_enq", "cmd_phys", "cmd_cycle",
    "cmd_rc", "cmd_slot", "cmd_status", "cmd_type_len",
    # x509 pubkey struct members (crypto_x509.h)
    "rsa_n", "rsa_e", "rsa_n_len", "rsa_e_len",
    "ec_pub", "ec_pub_len", "ec_curve",
    # ahci port struct member
    "fis_rx",
]

# ---- 1. word-level mappings (exact identifier) --------------------------
WORD = [
    # shell command-group registration functions (defined per file)
    ("power_test_cmds_register", "shell_cmds_power_test_register"),
    ("nic_test_cmds_register",   "shell_cmds_nic_test_register"),
    ("snd_test_cmds_register",   "shell_cmds_snd_test_register"),
    ("usb_test_cmds_register",   "shell_cmds_usb_test_register"),
    ("update_test_cmds_register","shell_cmds_update_test_register"),
    ("disk_cmds_register",       "shell_cmds_disk_register"),
    ("disk_test_cmds_register",  "shell_cmds_disk_test_register"),
    ("file_cmds_register",       "shell_cmds_file_register"),
    ("disk_setup_cmds_register", "driver_block_disk_setup_register"),
    # crypto RSA exported functions (members rsa_n/rsa_e are shielded)
    ("rsa_verify_pkcs1",        "crypto_rsa_verify_pkcs1"),
    ("rsa_verify_pss",          "crypto_rsa_verify_pss"),
    ("rsa_public_op_wrapper",   "crypto_rsa_public_op_wrapper"),
    ("rsa_public_op",           "crypto_rsa_public_op"),
    ("rsa_hash_len",            "crypto_rsa_hash_len"),
    # EC point helpers (static in crypto_ec_nist.c); pt_len is an aead
    # parameter name and is NOT touched
    ("pt_add",          "crypto_pt_add"),
    ("pt_copy",         "crypto_pt_copy"),
    ("pt_double",       "crypto_pt_double"),
    ("pt_is_inf",       "crypto_pt_is_inf"),
    ("pt_scalar_mult",  "crypto_pt_scalar_mult"),
    ("pt_set_inf",      "crypto_pt_set_inf"),
    ("pt_to_affine_x",  "crypto_pt_to_affine_x"),
    ("pt_to_affine_xy", "crypto_pt_to_affine_xy"),
    # part/disk helpers
    ("disk_guid",            "driver_block_part_disk_guid"),
    ("disk_setup_puts",      "driver_block_disk_setup_puts"),
    ("mkfs_fat32_device",    "driver_block_mkfs_fat32_device"),
    # net TCP congestion control ops (cc_out in xhci is a different local)
    ("cc_beta",                  "net_tcp_cc_beta"),
    ("cc_compute_k",             "net_tcp_cc_compute_k"),
    ("cc_init",                  "net_tcp_cc_init"),
    ("cc_on_ack",                "net_tcp_cc_on_ack"),
    ("cc_on_fast_recovery_enter","net_tcp_cc_on_fast_recovery_enter"),
    ("cc_on_rto",                "net_tcp_cc_on_rto"),
    # arch idt remnants (incl. asm-visible symbols)
    ("idt_install_stubs", "arch_idt_install_stubs"),
    ("idt_load",          "arch_idt_load"),
    ("idt_ptr_init",      "arch_idt_ptr_init"),
    ("idt_ptr",           "arch_idt_ptr"),
    ("idt_stub",          "arch_idt_stub"),
    # syscall surface (decision: sys_mmap -> sys_mem_mmap etc.)
    ("sys_mmap",     "sys_mem_mmap"),
    ("sys_munmap",   "sys_mem_munmap"),
    ("sys_mprotect", "sys_mem_mprotect"),
    ("sys_brk",      "sys_mem_brk"),
    ("kernel_pipe_t",    "sys_pipe_t"),
    ("kernel_self",      "arch_kernel_self"),
    ("get_kernel_self",  "arch_get_kernel_self"),
    # virtio-net transport lives inside net_core.c
    ("virtio_alloc_virtq", "net_virtio_alloc_virtq"),
]

# ---- 2. prefix mappings (longest-first applied via word boundary) -------
PREFIX = [
    # display / input early console
    ("serial_",  "screen_serial_"),
    # shell command handlers cmd_* -> shell_cmd_*  (shielded xhci/ahci/nic locals)
    ("cmd_",     "shell_cmd_"),
    ("wp_",      "shell_wp_"),
    # usb family
    ("usbaudio_", "driver_usb_audio_"),
    ("cdc_",      "driver_usb_serial_cdc_"),
    ("ftdi_",     "driver_usb_serial_ftdi_"),
    ("xhci_",     "driver_usb_xhci_"),
    ("uhci_",     "driver_usb_uhci_"),
    ("ohci_",     "driver_usb_ohci_"),
    ("ehci_",     "driver_usb_ehci_"),
    ("msc_",      "driver_usb_msc_"),
    ("hub_",      "driver_usb_hub_"),
    ("iso_",      "driver_usb_iso_"),
    ("usb_",      "driver_usb_"),
    # block family
    ("bmdma_",     "driver_block_ata_dma_"),
    ("ata_",       "driver_block_ata_"),
    ("ahci_",      "driver_block_ahci_"),
    ("nvme_",      "driver_block_nvme_"),
    ("part_",      "driver_block_part_"),
    ("virtio_blk_","driver_block_virtio_blk_"),
    ("vblk_",      "driver_block_virtio_blk_"),
    ("blk_",       "driver_block_"),
    # nic family
    ("e1000e_",  "driver_nic_e1000e_"),
    ("igb_",     "driver_nic_igb_"),
    ("ixgbe_",   "driver_nic_ixgbe_"),
    ("bcm_",     "driver_nic_bcm57xx_"),
    ("rtl8139_", "driver_nic_rtl8139_"),
    ("rtl8169_", "driver_nic_rtl8169_"),
    ("rtl_",     "driver_nic_rtl8169_"),
    ("nic_",     "driver_nic_"),
    ("pci_",     "driver_pci_"),
    # snd family
    ("virtio_snd_", "driver_snd_virtio_"),
    ("vsnd_",       "driver_snd_virtio_"),
    ("hda_",        "driver_snd_hda_"),
    ("ac97_",       "driver_snd_ac97_"),
    ("sb16_",       "driver_snd_sb16_"),
    ("sb_",         "driver_snd_sb16_"),
    ("es1370_",     "driver_snd_es1370_"),
    ("vq_",         "driver_snd_virtio_vq_"),
    ("snd_",        "driver_snd_"),
    # fs family
    ("fat32_", "fs_fat32_"),
    ("exfat_", "fs_exfat_"),
    ("ext4_",  "fs_ext4_"),
    ("ramfs_", "fs_ramfs_"),
    ("vfs_",   "fs_vfs_"),
    # net family
    ("tls13_",     "net_tls13_"),
    ("tls12_",     "net_tls12_"),
    ("sshd_",      "net_sshd_"),
    ("sd_",        "net_sshd_"),
    ("netfilter_", "net_netfilter_"),
    ("nf_",        "net_netfilter_"),
    ("dhcp_",      "net_dhcp_"),
    ("icmp_",      "net_icmp_"),
    ("arp_",       "net_arp_"),
    ("udp_",       "net_udp_"),
    ("dns_",       "net_dns_"),
    ("virtio_net_","net_virtio_"),
    ("virtq_",     "net_virtq_"),
    ("ip_",        "net_ip_"),
    ("tcp_",       "net_tcp_"),
    ("tls_",       "net_tls_"),
    ("ssh_",       "net_ssh_"),
    ("http_",      "ota_update_http_"),
    # core family
    ("kthread_", "core_kthread_"),
    ("sched_",   "core_sched_"),
    ("timer_",   "core_timer_"),
    ("rtc_",     "core_rtc_"),
    ("syscall_", "core_syscall_"),
    ("power_",   "core_power_"),
    ("proc_",    "sys_proc_"),
    # mem family
    ("kern_",  "mem_vmm_kern_"),
    ("pmm_",   "mem_pmm_"),
    ("vmm_",   "mem_vmm_"),
    ("heap_",  "mem_heap_"),
    # lib family
    ("config_", "lib_config_"),
    ("log_",    "lib_log_"),
    ("gzip_",   "lib_gzip_"),
    ("tar_",    "lib_tar_"),
    # crypto family
    ("sha512_",   "crypto_sha512_"),
    ("sha256_",   "crypto_sha256_"),
    ("sha384_",   "crypto_sha384_"),
    ("aes128_",   "crypto_aes128_"),
    ("aes256_",   "crypto_aes256_"),
    ("aes_",      "crypto_aes_"),
    ("hmac_",     "crypto_hmac_"),
    ("ghash_",    "crypto_ghash_"),
    ("gcm_",      "crypto_gcm_"),
    ("poly1305_", "crypto_poly1305_"),
    ("chacha20_", "crypto_chacha20_"),
    ("x25519_",   "crypto_x25519_"),
    ("dhe_",      "crypto_dhe_"),
    ("dh_",       "crypto_dh_"),
    ("ec_",       "crypto_ec_"),
    ("der_",      "crypto_der_"),
    ("oid_",      "crypto_oid_"),
    ("x509_",     "crypto_x509_"),
    ("bn_",       "crypto_bn_"),
    # ota family
    ("update_", "ota_update_"),
    ("ab_",     "ota_ab_"),
    # arch family
    ("mb2_", "arch_multiboot2_"),
    ("gdt_", "arch_gdt_"),
    ("exc_", "arch_exc_"),
]

# struct oc_tar -> struct lib_tar (lib_tar internal type)
PHRASE = [("struct oc_tar", "struct lib_tar")]

def build_patterns():
    pats = []
    for old, new in WORD:
        pats.append((re.compile(r'\b' + re.escape(old) + r'\b'), new, old))
    for old, new in PHRASE:
        pats.append((re.compile(r'\b' + old + r'\b'), new, old))
    for old, new in PREFIX:
        pats.append((re.compile(r'\b' + re.escape(old) + r'([a-z0-9_]*)'), new + r'\1', old))
    return pats

# string literals (single-line), char literals, block comments, line comments
TOK_RE = re.compile(
    r'"(?:\\.|[^"\\\n])*"'
    r"|'(?:\\.|[^'\\\n])*'"
    r'|/\*.*?\*/'
    r'|//[^\n]*',
    re.S)

def convert(src, pats, shield_re, stats):
    strings, comments = [], []
    def stash(m):
        tok = m.group(0)
        if tok[0] == '/':                       # comment: rename inside, too
            new = shield_re.sub(lambda k: '\x00K' + k.group(0) + '\x00', tok)
            for pat, rep, old in pats:
                new, n = pat.subn(rep, new)
                if n: stats[old] += n
            new = re.sub(r'\x00K(.+?)\x00', r'\1', new)
            comments.append(new)
            return '\x00C' + str(len(comments) - 1) + '\x00'
        strings.append(tok)                     # string literal: untouched
        return '\x00S' + str(len(strings) - 1) + '\x00'
    work = TOK_RE.sub(stash, src)
    # shield identifiers (temporarily mark so prefix pass can't hit them)
    def shield_fn(m):
        return '\x00K' + m.group(0) + '\x00'
    work = shield_re.sub(shield_fn, work)
    for pat, new, old in pats:
        work, n = pat.subn(new, work)
        if n: stats[old] += n
    # restore shields, comments and strings
    work = re.sub(r'\x00K(.+?)\x00', r'\1', work)
    work = re.sub(r'\x00C(\d+)\x00', lambda m: comments[int(m.group(1))], work)
    work = re.sub(r'\x00S(\d+)\x00', lambda m: strings[int(m.group(1))], work)
    return work

def main():
    files = []
    for pat in ['kernel/**/*.c', 'kernel/**/*.h', 'kernel/**/*.S',
                'drivers/**/*.c', 'drivers/**/*.h',
                'fs/*.c', 'fs/*.h', 'net/*.c', 'net/*.h',
                'shell/*.c', 'shell/*.h', 'l1/*.c', 'l1/*.h',
                'boot/*.c', 'boot/*.h',
                'tests/*.c', 'tools/host_*.c']:
        files += glob.glob(ROOT + '/' + pat, recursive=True)
    files = sorted(set(files))
    shield_re = re.compile(r'\b(?:' + '|'.join(re.escape(s) for s in SHIELD) + r')\b')
    pats = build_patterns()
    stats = collections.Counter()
    changed = 0
    for f in files:
        src = open(f, encoding='utf-8', errors='replace').read()
        out = convert(src, pats, shield_re, stats)
        if out != src:
            open(f, 'w', encoding='utf-8').write(out)
            changed += 1
    total = sum(stats.values())
    print(f"files scanned: {len(files)}, files changed: {changed}, replacements: {total}")
    for k, v in sorted(stats.items(), key=lambda x: -x[1]):
        print(f"  {k:32s} -> {v}")

if __name__ == '__main__':
    main()
