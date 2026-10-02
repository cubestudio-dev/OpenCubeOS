// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 cubestudio-dev <cubestudio@qq.com>
//
// Bilingual home-page data. zh strings are verbatim from lib/site.ts
// (which itself mirrors README.md / docs/INTERFACES.md / real build
// outputs); en strings are their translations — no numbers changed.

import type { Locale } from "@/lib/i18n";

export type Bi<T> = { zh: T; en: T };

export function pick<T>(bi: Bi<T>, locale: Locale): T {
  return bi[locale];
}

function fmtBytes(n: number): string {
  return n.toLocaleString("en-US");
}
export { fmtBytes };

// WP-10a stats (same caliber as lib/site.ts; see there for verify commands)
export const STATS: Bi<{ value: string; label: string }[]> = {
  zh: [
    { value: "56,019", label: "行源码" },
    { value: "65", label: "L1 扩展接口" },
    { value: "37", label: "系统调用" },
    { value: "98", label: "shell 命令" },
    { value: "10", label: "工作包" },
    { value: "18/18", label: "QEMU 回归" },
  ],
  en: [
    { value: "56,019", label: "lines of source" },
    { value: "65", label: "L1 extension interfaces" },
    { value: "37", label: "system calls" },
    { value: "98", label: "shell commands" },
    { value: "10", label: "work packages" },
    { value: "18/18", label: "QEMU regression" },
  ],
};

// WP-10a storage + WP-09 security transport features (user-facing list)
export const FEATURES: Bi<
  { name: string; tag: string; desc: string }[]
> = {
  zh: [
    {
      name: "AHCI SATA",
      tag: "WP-10a",
      desc: "SATA 硬盘/SSD 驱动：精确类别 0x010601 枚举、命令列表/H2D FIS、48 位 LBA DMA 读写、FLUSH CACHE，多控制器多端口。",
    },
    {
      name: "NVMe SSD",
      tag: "WP-10a",
      desc: "NVMe 驱动：管理队列对 + 双 I/O 队列对轮询、Identify、Read/Write/Flush，doorbell 提交完成。",
    },
    {
      name: "ATA Bus-Master DMA",
      tag: "WP-10a",
      desc: "经典 IDE 走 PCI BMDMA（BAR4 + PRDT）真 DMA 传输，PIO 路径保留为回退；自测含 DMA vs PIO 计时对比。",
    },
    {
      name: "virtio-blk",
      tag: "WP-10a",
      desc: "QEMU virtio 磁盘：容量读取修复（此前为 0）、virtqueue 请求/应答、读写刷全链路。",
    },
    {
      name: "FAT32 全设备挂载",
      tag: "WP-10a",
      desc: "FAT32 从直连 ATA 迁移到 blk 层：hda/sda/vda/nvme0 四类驱动均可 mkfs + 挂载 + 文件往返；MBR + GPT 分区解析。",
    },
    {
      name: "TLS 1.3 / TLS 1.2",
      tag: "kernel/tls.c",
      desc: "TLS 1.3（X25519 + AES-128-GCM，ChaCha20-Poly1305 套件）+ TLS 1.2 ECDHE-GCM 回退；X.509 CA 链验证（内嵌公共根），验证失败即握手失败。",
    },
    {
      name: "HTTPS",
      tag: "wget https://",
      desc: "wget https://host:port/path 经 TLS 下载，直接落入 VFS 文件；真实站点实测：GitHub Pages、google、cloudflare。",
    },
    {
      name: "SSH",
      tag: "ssh.c / sshd.c",
      desc: "客户端 + 服务端：curve25519-sha256 优先 + group14-sha256 回退、aes128-ctr 优先、hmac-sha2-256、主机密钥签名验证（TOFU + 指纹）、密码 + 公钥认证；与 paramiko 双向互操作。",
    },
  ],
  en: [
    {
      name: "AHCI SATA",
      tag: "WP-10a",
      desc: "SATA disk/SSD driver: exact class 0x010601 enumeration, command lists/H2D FIS, 48-bit LBA DMA, FLUSH CACHE, multi-controller multi-port.",
    },
    {
      name: "NVMe SSD",
      tag: "WP-10a",
      desc: "NVMe driver: admin queue pair + two round-robin I/O queue pairs, Identify, Read/Write/Flush with doorbells.",
    },
    {
      name: "ATA Bus-Master DMA",
      tag: "WP-10a",
      desc: "Classic IDE over PCI BMDMA (BAR4 + PRDT) with true DMA transfers; the PIO path stays as fallback; the self-test prints a DMA-vs-PIO timing comparison.",
    },
    {
      name: "virtio-blk",
      tag: "WP-10a",
      desc: "QEMU virtio disk: capacity read fixed (was 0), virtqueue request/reply, full read/write/flush path.",
    },
    {
      name: "FAT32 on every device",
      tag: "WP-10a",
      desc: "FAT32 moved from direct ATA I/O to the blk layer: mkfs + mount + file round-trips on hda/sda/vda/nvme0; MBR + GPT partition parsing.",
    },
    {
      name: "TLS 1.3 / TLS 1.2",
      tag: "kernel/tls.c",
      desc: "TLS 1.3 (X25519 + AES-128-GCM, ChaCha20-Poly1305 suites) + TLS 1.2 ECDHE-GCM fallback; X.509 CA chain verification against embedded public roots — the handshake fails closed on verification errors.",
    },
    {
      name: "HTTPS",
      tag: "wget https://",
      desc: "wget https://host:port/path downloads over TLS, landing directly in a VFS file; verified against three real sites: GitHub Pages, google, cloudflare.",
    },
    {
      name: "SSH",
      tag: "ssh.c / sshd.c",
      desc: "Client + server: curve25519-sha256 preferred with group14-sha256 fallback, aes128-ctr preferred, hmac-sha2-256, host-key signature verification (TOFU + fingerprint), password + publickey auth; bidirectional interop with paramiko.",
    },
  ],
};

// Base kernel (WP-01..WP-08) one-liners, from README.md per-WP sections
export const BASE_KERNEL: Bi<{ k: string; v: string }[]> = {
  zh: [
    {
      k: "WP-01",
      v: "BIOS + UEFI 双引导（GRUB multiboot2）、64-bit long mode、800x600x32 帧缓冲、8x16 位图字体控制台",
    },
    {
      k: "WP-02",
      v: "256 项 IDT + GDT/TSS、PIC remap、PIT @ 100Hz、PS/2 键盘 + COM1、oc> 交互行编辑器",
    },
    {
      k: "WP-03",
      v: "bitmap PMM、4 级页表 VMM、真实页错误处理、free-list 内核堆（kmalloc/kfree/krealloc）",
    },
    {
      k: "WP-04",
      v: "抢占式调度（32 优先级 + 时间片）、spinlock/semaphore/mutex/condvar、Ring 3 + ELF 装载",
    },
    {
      k: "WP-05",
      v: "VFS + RamFS、env/alias/cwd、ls/cd/cat/mkdir/rm/cp/mv 等文件命令",
    },
    {
      k: "WP-06",
      v: "e1000 驱动、ARP/IPv4/ICMP/TCP/UDP、socket API、dhcp/ping/wget/netstat",
    },
    {
      k: "WP-07",
      v: "ATA/virtio-blk/NVMe、64 槽 LRU 回写块缓存、MBR 分区、FAT32(R/W) exFAT(R/W) ext4(RO)",
    },
    {
      k: "WP-08",
      v: "37 syscalls、ld.so 动态链接（5 种重定位 + dlopen/dlsym）、ush 用户 shell（20 内建工具）",
    },
  ],
  en: [
    {
      k: "WP-01",
      v: "BIOS + UEFI dual boot (GRUB multiboot2), 64-bit long mode, 800x600x32 framebuffer, 8x16 bitmap-font console",
    },
    {
      k: "WP-02",
      v: "256-entry IDT + GDT/TSS, PIC remap, PIT @ 100Hz, PS/2 keyboard + COM1, oc> interactive line editor",
    },
    {
      k: "WP-03",
      v: "bitmap PMM, 4-level page-table VMM, real page-fault handling, free-list kernel heap (kmalloc/kfree/krealloc)",
    },
    {
      k: "WP-04",
      v: "Preemptive scheduler (32 priorities + time slices), spinlock/semaphore/mutex/condvar, Ring 3 + ELF loading",
    },
    {
      k: "WP-05",
      v: "VFS + RamFS, env/alias/cwd, ls/cd/cat/mkdir/rm/cp/mv file commands",
    },
    {
      k: "WP-06",
      v: "e1000 driver, ARP/IPv4/ICMP/TCP/UDP, socket API, dhcp/ping/wget/netstat",
    },
    {
      k: "WP-07",
      v: "ATA/virtio-blk/NVMe, 64-slot LRU write-back block cache, MBR partitions, FAT32(R/W) exFAT(R/W) ext4(RO)",
    },
    {
      k: "WP-08",
      v: "37 syscalls, ld.so dynamic linking (5 relocation types + dlopen/dlsym), ush user shell (20 built-in tools)",
    },
  ],
};

// Work packages (README.md section titles)
export const WORK_PACKAGES: Bi<{ no: string; title: string }[]> = {
  zh: [
    { no: "WP-01", title: "引导 + framebuffer + 文本渲染" },
    { no: "WP-02", title: "中断 + 定时器 + 键盘" },
    { no: "WP-03", title: "物理内存 + 虚拟内存 + 内核堆" },
    { no: "WP-04", title: "调度器 + 同步原语 + 用户态" },
    { no: "WP-05", title: "Shell 增强 + 文件系统" },
    { no: "WP-06", title: "网络协议栈" },
    { no: "WP-07", title: "磁盘子系统" },
    {
      no: "WP-08",
      title: "完整 syscall + 动态链接 + 用户 shell + 工具 + 审计修复",
    },
    {
      no: "WP-09",
      title: "安全传输：SSH（客户端 + 服务端）、TLS 1.3 / TLS 1.2 / HTTPS、crypto 核心",
    },
    {
      no: "WP-10a",
      title: "存储驱动：AHCI / NVMe / ATA DMA / virtio-blk + blk_* 扩展接口",
    },
  ],
  en: [
    { no: "WP-01", title: "Boot + framebuffer + text rendering" },
    { no: "WP-02", title: "Interrupts + timer + keyboard" },
    { no: "WP-03", title: "Physical memory + virtual memory + kernel heap" },
    { no: "WP-04", title: "Scheduler + sync primitives + user mode" },
    { no: "WP-05", title: "Shell enhancements + file system" },
    { no: "WP-06", title: "Network protocol stack" },
    { no: "WP-07", title: "Disk subsystem" },
    {
      no: "WP-08",
      title: "Complete syscall + dynamic linking + user shell + tools + audit fixes",
    },
    {
      no: "WP-09",
      title: "Secure transport: SSH (client + server), TLS 1.3 / TLS 1.2 / HTTPS, crypto core",
    },
    {
      no: "WP-10a",
      title: "Storage drivers: AHCI / NVMe / ATA DMA / virtio-blk + blk_* extension API",
    },
  ],
};

// Verification (README.md "Tests" + docs/EXTENSIONS_WP10a.md)
export const VERIFY: Bi<{ name: string; desc: string }[]> = {
  zh: [
    {
      name: "18/18 QEMU 全量回归",
      desc: "boot 横幅 + uname + 12 个用户程序 + p3_test + heaptest + l1test + crashlog，单次 QEMU 会话完成（挂全部四类盘）。",
    },
    {
      name: "WP-10a 存储测试 8 项",
      desc: "ahci_test / nvme_test / ata_dma_test（含 DMA vs PIO 计时）/ virtio_blk_test / disk_rw_test / partition_test（MBR+GPT）/ fs_mount_test（四设备 FAT32 往返）/ real_hw_test 全 PASS。",
    },
    {
      name: "dhtest 5/5",
      desc: "DH modexp 正确性：Oakley Group 1 + group14 真值向量、8..256 字节尺度扫描、确定性验证。",
    },
    {
      name: "HTTPS E2E",
      desc: "内核 TLS 客户端访问三个真实站点：cubestudio-dev.github.io（update.json）、google.com、cloudflare.com——TLS 1.3 握手 + CA 链验证 + 加密传输全链路。",
    },
  ],
  en: [
    {
      name: "18/18 full QEMU regression",
      desc: "boot banner + uname + 12 user programs + p3_test + heaptest + l1test + crashlog, executed in a single QEMU session with all four disk types attached.",
    },
    {
      name: "8 WP-10a storage tests",
      desc: "ahci_test / nvme_test / ata_dma_test (with DMA-vs-PIO timing) / virtio_blk_test / disk_rw_test / partition_test (MBR+GPT) / fs_mount_test (FAT32 round-trip on all four drivers) / real_hw_test — all PASS.",
    },
    {
      name: "dhtest 5/5",
      desc: "DH modexp correctness: Oakley Group 1 + group14 truth vectors, 8..256-byte scale sweep, determinism check.",
    },
    {
      name: "HTTPS E2E",
      desc: "Kernel TLS client against three real sites: cubestudio-dev.github.io (update.json), google.com, cloudflare.com — TLS 1.3 handshake + CA chain verification + encrypted transport, end to end.",
    },
  ],
};
