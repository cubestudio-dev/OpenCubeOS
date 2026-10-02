// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 cubestudio-dev <cubestudio@qq.com>
//
// Central site data. Every number is a real WP-09 value taken from the
// repository docs (README.md, docs/INTERFACES.md, docs/EXTENSIONS_WP09.md)
// and real sha256sum/build outputs.
// 3-way identical: local build + GitHub Release WP-09-docs-comments + this site /downloads/

export const BASE = "/OpenCubeOS";

export const GITHUB_REPO = "https://github.com/cubestudio-dev/OpenCubeOS";
export const RELEASE_WP09 =
  "https://github.com/cubestudio-dev/OpenCubeOS/releases/tag/WP-09-docs-comments";
export const RELEASES = "https://github.com/cubestudio-dev/OpenCubeOS/releases";
export const SITE_URL = "https://cubestudio-dev.github.io/OpenCubeOS/";

// Real assets (build/ + GitHub Release WP-09-docs-comments + this site /downloads/, 3-way identical)
export const ISO_FILE = "opencube-wp09-docs-comments.iso";
export const ISO_SIZE_B = 10872832;
export const ISO_SIZE_MB = "10.37";
export const ISO_SHA256 =
  "7c1399601549672f497995607f16c175acd9ea9b85648c926a31fab0f60bdfb9";
export const ISO_URL = `${BASE}/downloads/${ISO_FILE}`;

export const SRC_FILE = "opencube-wp09-docs-comments-src.zip";
export const SRC_SIZE_B = 7385488;
export const SRC_SIZE_MB = "7.04";
export const SRC_SHA256 =
  "bd5523bb2c48ee82ca0995551cf6731eec649d160537d7a902dca3da614b0481";
export const SRC_URL = `${BASE}/downloads/${SRC_FILE}`;

// WP-09 mainstream stats. Source lines: find kernel boot userprogs -name '*.c' -o -name '*.h' -o -name '*.S' -o -name '*.asm' -o -name '*.ld' | xargs wc -l (54,621 at commit 5fa087e).
// Shell commands: live boot self-test + help count = 87 (includes nf_test, tcpstats, tcpcc_test, dnstest, tcptest).
// L1 extension interfaces: 57 numbered items (docs/EXTENSIONS_WP08cd.md: WP-01..07 32 + WP-08 25); WP-09 adds transport APIs, not numbered L1 interfaces.
export const STATS = [
  { value: "54,621", label: "行源码" },
  { value: "57", label: "L1 扩展接口" },
  { value: "37", label: "系统调用" },
  { value: "87", label: "shell 命令" },
  { value: "9", label: "工作包" },
  { value: "18/18", label: "QEMU 回归" },
];

// WP-09 security transport + network features (user-facing list)
export const FEATURES = [
  {
    name: "TCP 可靠传输",
    tag: "kernel/net.c",
    desc: "面向连接的可靠字节流：三次握手建立连接、按需重传分段、四次挥手拆除（FIN/ACK）。",
  },
  {
    name: "TCP 选项",
    tag: "WP-09",
    desc: "SYN 携带 MSS、Window Scale、SACK-Permitted、Timestamps 选项，入包选项双向解析。",
  },
  {
    name: "路由",
    tag: "route",
    desc: "route 命令 — WP-09 新增网络运维命令组（route / arp / firewall / tcpstats / dns）之一。",
  },
  {
    name: "DNS",
    tag: "dns_resolve",
    desc: "内核 dns_resolve API + dns 命令；DHCP 自动下发 DNS 配置（option 6）。",
  },
  {
    name: "防火墙",
    tag: "firewall",
    desc: "firewall 命令 — 内核包过滤运维接口，WP-09 网络可见性命令组。",
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
];

// Base kernel (WP-01..WP-08) one-liners, from README.md per-WP sections
export const BASE_KERNEL = [
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
];

// Work packages (README.md section titles)
export const WORK_PACKAGES = [
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
];

// Verification (README.md "Tests" + docs/EXTENSIONS_WP09.md)
export const VERIFY = [
  {
    name: "18/18 QEMU 全量回归",
    desc: "boot 横幅 + uname + 12 个用户程序 + p3_test + heaptest + l1test + crashlog，单次 QEMU 会话完成。",
  },
  {
    name: "dhtest 5/5",
    desc: "DH modexp 正确性：Oakley Group 1 + group14 真值向量、8..256 字节尺度扫描、确定性验证。",
  },
  {
    name: "HTTPS E2E",
    desc: "内核 TLS 客户端访问三个真实站点：cubestudio-dev.github.io（10148 字节 update.json）、google.com、cloudflare.com——TLS 1.3 握手 + CA 链验证 + 加密传输全链路。",
  },
  {
    name: "SSH 双向互操作",
    desc: "与 paramiko 5.0 双向互通：客户端→内核 sshd 4/4 检查；内核 ssh→paramiko 服务端 K 字节级一致。",
  },
];
