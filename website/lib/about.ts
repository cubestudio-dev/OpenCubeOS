// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 cubestudio-dev <cubestudio@qq.com>
//
// Bilingual about-page data. zh strings are verbatim from the previous
// single-locale about page (app/about/page.tsx, sourced from README.md /
// docs/EXTENSIONS_WP09.md / docs/INTERFACES.md); en strings are their
// translations — no facts or numbers changed.

import type { Locale } from "@/lib/i18n";
import type { Bi } from "@/lib/home";

export const TIMELINE: Bi<
  { no: string; title: string; desc: string }[]
> = {
  zh: [
    {
      no: "WP-01",
      title: "引导 + framebuffer + 文本渲染",
      desc: "BIOS + UEFI 双引导（GRUB multiboot2）、64-bit long mode、4 GiB 恒等映射、800x600x32 帧缓冲、8x16 位图字体控制台，首批 4 个 L0→L1 扩展接口。",
    },
    {
      no: "WP-02",
      title: "中断 + 定时器 + 键盘",
      desc: "256 项 IDT + GDT/TSS、PIC remap、CPU 异常处理、PIT @ 100Hz 真实系统滴答、PS/2 键盘 + COM1 串口输入、oc> 交互行编辑器。",
    },
    {
      no: "WP-03",
      title: "物理内存 + 虚拟内存 + 内核堆",
      desc: "bitmap PMM（解析 multiboot2 mmap）、4 级页表 VMM、真实页错误处理（栈/堆增长）、free-list 堆分配器（first-fit + 合并 + 双重释放检测）。",
    },
    {
      no: "WP-04",
      title: "调度器 + 同步原语 + 用户态",
      desc: "抢占式调度器（32 优先级 + 时间片）、spinlock/semaphore/mutex/condvar、Ring 3 用户态（TSS RSP0 切换）、静态 ELF + PIE 装载。",
    },
    {
      no: "WP-05",
      title: "Shell 增强 + 文件系统",
      desc: "Shell 环境变量/别名/引号处理、VFS（mount/unmount/open/read/write/stat/readdir/…）、RamFS 根文件系统、ls/cd/cat/mkdir 等文件命令。",
    },
    {
      no: "WP-06",
      title: "网络协议栈",
      desc: "e1000 网卡驱动（PCI bus master）、ARP + IPv4 + ICMP + TCP + UDP、socket API（socket/bind/listen/accept/connect/send/recv/close）、dhcp/ping/wget/dns/netstat。",
    },
    {
      no: "WP-07",
      title: "磁盘子系统",
      desc: "ATA/IDE PIO（LBA28）、virtio-blk（modern PCI）、NVMe（admin + IO 队列）、64 槽 LRU 回写块缓存、MBR 分区、FAT32(R/W) exFAT(R/W) ext4(RO)。",
    },
    {
      no: "WP-08",
      title: "完整 syscall + 动态链接 + 用户 shell + 审计修复",
      desc: "37 个系统调用、用户态 ld.so（5 种重定位、dlopen/dlsym/dlclose、ldd）、ush 用户 shell（20 内建工具 + Tab 补全 + 作业控制 + 管道）、15 个用户态测试程序；47 项原始审计修复，截至 WP-08 累计修复 120 个 bug。",
    },
    {
      no: "WP-09",
      title: "安全传输：SSH + TLS 1.3 / TLS 1.2 / HTTPS + crypto 核心",
      desc: "crypto 核心（AES-128、SHA-256、HMAC-SHA256、任意长度 DH modexp、bignum/P-256/X25519/RSA/AEAD 原语）；SSH 客户端 + 服务端（curve25519-sha256 优先 + group14-sha256 回退、aes128-ctr 优先、hmac-sha2-256、rsa-sha2-256/512 主机密钥签名验证 TOFU、密码 + 公钥认证、session exec，与 paramiko 双向互操作、K 字节级一致）；TLS 1.3（X25519 + AES-128-GCM）+ TLS 1.2 ECDHE-GCM 回退、X.509 CA 链验证（内嵌 10 个公共根，验证失败即握手失败）；HTTPS 下载入 VFS（真实站点实测：GitHub Pages / google / cloudflare）；route/arp/firewall/tcpstats/dns 运维命令；TCP 可靠性（CUBIC、SACK、快速重传）与完整选项（MSS、Window Scale、SACK-Permitted、Timestamps）；netfilter（规则/conntrack/策略）。",
    },
    {
      no: "WP-10a",
      title: "存储驱动：AHCI / NVMe / ATA DMA / virtio-blk + blk_* 扩展接口",
      desc: "四类主流存储驱动统一注册到 blk 层：AHCI SATA（精确类别 0x010601、命令列表/H2D FIS、48 位 LBA DMA 读写、FLUSH CACHE、多控制器多端口）；NVMe（管理队列对 + 双 I/O 队列对轮询、Identify、Read/Write/Flush）；ATA Bus-Master DMA（BAR4 BMDMA + PRDT 真 DMA，PIO 回退保留，自测含 DMA vs PIO 计时）；virtio-blk（容量读取修复）；FAT32 迁移到 blk 层后四类设备均可挂载并完成文件往返（MBR + GPT 分区解析）；新增 blk_register/blk_read/blk_write/blk_flush/blk_set_ops 等 8 项 L1 接口与 ahci/nvme/ata 状态命令 + 八项存储测试；根治 fork #PF 潜伏缺陷（pmm 保留 0x400000-0x600000 物理窗口）。",
    },
    {
      no: "WP-10b",
      title: "网卡驱动：九族主流有线网卡 + nic_* 扩展接口",
      desc: "网卡驱动框架 + 九族驱动，使内核能在真机上识别并驱动主流有线以太网卡：e1000e（MMIO、legacy 16 B 描述符）、igb（MMIO、2 RX + 2 TX 队列）、ixgbe（MMIO）、rtl8139（PIO、4 个固定 TX 槽 + 64 KiB RX 环）、rtl8168/8125/810x（PIO 描述符环）、bcm57xx（MMIO、host rings + mailboxes）及 3c59x/nForce/AR81xx/Yukon 识别。nic 注册表（8 槽）与 blk 层同构，协议栈经框架路由帧，legacy e1000/virtio-net 路径保留为回退，WP-06..WP-09 行为不变。DMA 缓冲/描述符环全部来自恒等映射 PMM 区域。新增 nic_register/send/recv/link_status/get_mac 与八个 per-driver init 入口（L1 接口总数增至 78）；boot 实测 116 条命令（10 个 NIC 测试 + 8 个状态命令）。e1000e、igb、rtl8139 在 QEMU 实测 TX/RX/DHCP/ping/HTTPS 全链路；其余无 QEMU 设备模型时如实 SKIPPED（数据手册实现），绝不伪造输出。SeaBIOS 与 OVMF 双引导验证。",
    },
  ],
  en: [
    {
      no: "WP-01",
      title: "Boot + framebuffer + text rendering",
      desc: "BIOS + UEFI dual boot (GRUB multiboot2), 64-bit long mode, 4 GiB identity mapping, 800x600x32 framebuffer, 8x16 bitmap-font console, and the first 4 L0→L1 extension interfaces.",
    },
    {
      no: "WP-02",
      title: "Interrupts + timer + keyboard",
      desc: "256-entry IDT + GDT/TSS, PIC remap, CPU exception handling, PIT @ 100Hz real system tick, PS/2 keyboard + COM1 serial input, oc> interactive line editor.",
    },
    {
      no: "WP-03",
      title: "Physical memory + virtual memory + kernel heap",
      desc: "bitmap PMM (parses multiboot2 mmap), 4-level page-table VMM, real page-fault handling (stack/heap growth), free-list heap allocator (first-fit + coalescing + double-free detection).",
    },
    {
      no: "WP-04",
      title: "Scheduler + sync primitives + user mode",
      desc: "Preemptive scheduler (32 priorities + time slices), spinlock/semaphore/mutex/condvar, Ring 3 user mode (TSS RSP0 switch), static ELF + PIE loading.",
    },
    {
      no: "WP-05",
      title: "Shell enhancements + file system",
      desc: "Shell env vars/aliases/quoting, VFS (mount/unmount/open/read/write/stat/readdir/…), RamFS root file system, ls/cd/cat/mkdir file commands.",
    },
    {
      no: "WP-06",
      title: "Network protocol stack",
      desc: "e1000 NIC driver (PCI bus master), ARP + IPv4 + ICMP + TCP + UDP, socket API (socket/bind/listen/accept/connect/send/recv/close), dhcp/ping/wget/dns/netstat.",
    },
    {
      no: "WP-07",
      title: "Disk subsystem",
      desc: "ATA/IDE PIO (LBA28), virtio-blk (modern PCI), NVMe (admin + IO queues), 64-slot LRU write-back block cache, MBR partitions, FAT32(R/W) exFAT(R/W) ext4(RO).",
    },
    {
      no: "WP-08",
      title: "Complete syscall + dynamic linking + user shell + audit fixes",
      desc: "37 syscalls, user-space ld.so (5 relocation types, dlopen/dlsym/dlclose, ldd), ush user shell (20 built-in tools + Tab completion + job control + pipes), 15 user-mode test programs; 47 original audit fixes, 120 bugs fixed cumulatively as of WP-08.",
    },
    {
      no: "WP-09",
      title: "Secure transport: SSH + TLS 1.3 / TLS 1.2 / HTTPS + crypto core",
      desc: "Crypto core (AES-128, SHA-256, HMAC-SHA256, arbitrary-length DH modexp, bignum/P-256/X25519/RSA/AEAD primitives); SSH client + server (curve25519-sha256 preferred with group14-sha256 fallback, aes128-ctr preferred, hmac-sha2-256, rsa-sha2-256/512 host-key signature verification with TOFU, password + publickey auth, session exec, bidirectional paramiko interop with byte-identical K); TLS 1.3 (X25519 + AES-128-GCM) + TLS 1.2 ECDHE-GCM fallback, X.509 CA chain verification (10 embedded public roots, fails closed); HTTPS downloads into VFS (verified against real sites: GitHub Pages / google / cloudflare); route/arp/firewall/tcpstats/dns ops commands; TCP reliability (CUBIC, SACK, fast retransmit) and full options (MSS, Window Scale, SACK-Permitted, Timestamps); netfilter (rules/conntrack/policies).",
    },
    {
      no: "WP-10a",
      title: "Storage drivers: AHCI / NVMe / ATA DMA / virtio-blk + blk_* extension API",
      desc: "Four mainstream storage drivers unified under the blk layer: AHCI SATA (exact class 0x010601, command lists/H2D FIS, 48-bit LBA DMA reads/writes, FLUSH CACHE, multi-controller multi-port); NVMe (admin queue pair + two round-robin I/O queue pairs, Identify, Read/Write/Flush); ATA Bus-Master DMA (BAR4 BMDMA + PRDT, true DMA with the PIO path kept as fallback, a DMA-vs-PIO timing self-test); virtio-blk (capacity read fixed); FAT32 moved to the blk layer so all four device types mount with file round-trips (MBR + GPT parsing); 8 new L1 interfaces (blk_register/blk_read/blk_write/blk_flush/blk_set_ops, ...) plus the ahci/nvme/ata status commands and an eight-test storage suite; root-fixed a latent fork #PF (pmm reserves the 0x400000-0x600000 physical window).",
    },
    {
      no: "WP-10b",
      title: "NIC drivers: nine mainstream wired Ethernet families + the nic_* extension API",
      desc: "A NIC driver framework plus nine driver families so the kernel can detect and drive mainstream wired Ethernet adapters on real machines: e1000e (MMIO, legacy 16-byte descriptors), igb (MMIO, 2 RX + 2 TX queues), ixgbe (MMIO), rtl8139 (PIO, 4 fixed TX slots + a 64 KiB RX ring), rtl8168/8125/810x (PIO descriptor rings), bcm57xx (MMIO, host rings + mailboxes), plus 3c59x/nForce/AR81xx/Yukon detection. The NIC registry (8 slots) mirrors the blk layer; the protocol stack routes frames through the framework with the legacy e1000/virtio-net paths kept as fallbacks, so WP-06..WP-09 behaviour is unchanged. All DMA buffers/descriptor rings come from the identity-mapped PMM region. New nic_register/send/recv/link_status/get_mac plus eight per-driver init entry points (78 L1 interfaces in total); 116 commands registered at boot (10 NIC tests + 8 NIC status commands). e1000e, igb and rtl8139 verified live in QEMU with the TX/RX/DHCP/ping/HTTPS full chain; the rest report SKIPPED honestly without a QEMU device model (datasheet-derived), never fabricating results. Boot verified with SeaBIOS and OVMF.",
    },
  ],
};

export function pickAbout<T>(bi: Bi<T>, locale: Locale): T {
  return bi[locale];
}
