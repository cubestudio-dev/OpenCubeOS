// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 cubestudio-dev <cubestudio@qq.com>
//
// Central site data. Every number is a real WP-10u value taken from the
// repository docs (README.md, docs/INTERFACES.md, docs/EXTENSIONS_WP10u.md)
// and real sha256sum/build outputs.
// 3-way identical: local build + GitHub Release WP-10u + this site /downloads/

export const BASE = "/OpenCubeOS";

export const GITHUB_REPO = "https://github.com/cubestudio-dev/OpenCubeOS";
export const RELEASE_WP09 =
  "https://github.com/cubestudio-dev/OpenCubeOS/releases/tag/WP-10u";
export const RELEASES = "https://github.com/cubestudio-dev/OpenCubeOS/releases";
export const SITE_URL = "https://cubestudio-dev.github.io/OpenCubeOS/";

// Real assets (build/ + GitHub Release WP-10u + this site /downloads/, 3-way identical)
export const ISO_FILE = "opencube-wp10u.iso";
export const ISO_SIZE_B = 11003904;
export const ISO_SIZE_MB = "10.49";
export const ISO_SHA256 =
  "a929999129826b0f695f123f3af8fa110cce49c9e91e53bc68efce4a919991e7";
export const ISO_URL = `${BASE}/downloads/${ISO_FILE}`;

export const SRC_FILE = "opencube-wp10u-src.zip";
export const SRC_SIZE_B = 1986287;
export const SRC_SIZE_MB = "1.89";
export const SRC_SHA256 =
  "9bc71af94b6eed106d690569d967e9fc4c9c37f1e707d2b4c3f6513931b5b81e";
export const SRC_URL = `${BASE}/downloads/${SRC_FILE}`;

// WP-10u stats. Source lines: find kernel boot userprogs \\( -name '*.c' -o -name '*.h' -o -name '*.S' \\) | xargs wc -l (63,644 at commit f490328).
// Shell commands: live boot count = 129 (adds the 11 WP-10u test commands + update/rollback/reboot).
// L1 extension interfaces: 85 numbered items (57 through WP-09 + 8 WP-10a + 13 WP-10b + 7 WP-10u update items; docs/EXTENSIONS_WP10u.md).
export const STATS = [
  { value: "63,644", label: "行源码" },
  { value: "85", label: "L1 扩展接口" },
  { value: "37", label: "系统调用" },
  { value: "129", label: "shell 命令" },
  { value: "12", label: "工作包" },
  { value: "18/18", label: "QEMU 回归" },
];

// WP-10u update + WP-10b NIC + WP-10a storage + WP-09 security transport features (user-facing list)
export const FEATURES = [
  {
    name: "系统内自动更新（A/B 分区）",
    tag: "WP-10u",
    desc: "Windows-Update 式更新：内核自带 gzip/DEFLATE + ustar 解包与流式 SHA256 校验，A/B 双分区安装、boot 标志自动回滚、update --local 离线更新；real_update_test 实测下载-安装-重启进入 slot B-回滚全链路。",
  },
  {
    name: "网卡驱动（九族）",
    tag: "WP-10b",
    desc: "e1000e / igb / ixgbe / RTL8139 / RTL8168 / RTL8125 / RTL810x / BCM57xx 及 3c59x、nForce、AR81xx、Yukon 识别与驱动：nic_register/send/recv/link_status/get_mac 扩展接口；e1000e、igb、RTL8139 在 QEMU 实测 DHCP/ping/HTTPS 全链路。",
  },
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
  {
    no: "WP-10a",
    title: "存储驱动：AHCI / NVMe / ATA DMA / virtio-blk + blk_* 扩展接口",
  },
  {
    no: "WP-10b",
    title: "网卡驱动：九族主流有线网卡 + nic_* 扩展接口",
  },
  {
    no: "WP-10u",
    title: "系统内自动更新：A/B 分区 + tar.gz 更新包 + 回滚 + 离线更新",
  },
];

// Verification (README.md "Tests" + docs/EXTENSIONS_WP10a.md + docs/EXTENSIONS_WP10b.md)
export const VERIFY = [
  {
    name: "18/18 QEMU 全量回归",
    desc: "boot 横幅 + uname + 12 个用户程序 + p3_test + heaptest + l1test + crashlog，单次 QEMU 会话完成（挂全部四类盘）。",
  },
  {
    name: "WP-10a 存储测试 8 项",
    desc: "ahci_test / nvme_test / ata_dma_test（含 DMA vs PIO 计时）/ virtio_blk_test / disk_rw_test / partition_test（MBR+GPT）/ fs_mount_test（四设备 FAT32 往返）/ real_hw_test 全 PASS。",
  },
  {
    name: "WP-10b 网卡测试 10 项",
    desc: "e1000e_test / igb_test / rtl8139_test 在 QEMU 实测注册、MAC、链路、TX/RX 与 dhcp/ping/wget HTTPS 全链路全 PASS；ixgbe_test / rtl8168_test / rtl8125_test / rtl810x_test / bcm57xx_test / other_nic_test / nic_rw_test 无 QEMU 设备模型时如实 SKIPPED（数据手册实现），绝不伪造输出。",
  },
  {
    name: "WP-10u 更新测试 11 项",
    desc: "update_pkg_test 12/12（gzip 三种块型/头选项/坏 CRC/截断/单字节流式恢复/ustar 解析与坏校验和）、ab_partition_test 7/7、update_check/download/verify/install/rollback/local/status 全 PASS；real_update_test 7/7 端到端：下载-校验-安装-slot B-真实重启进入 test1 内核-回滚回 A 全链路。",
  },
  {
    name: "dhtest 5/5",
    desc: "DH modexp 正确性：Oakley Group 1 + group14 真值向量、8..256 字节尺度扫描、确定性验证。",
  },
  {
    name: "HTTPS E2E",
    desc: "内核 TLS 客户端访问三个真实站点：cubestudio-dev.github.io（update.json）、google.com、cloudflare.com——TLS 1.3 握手 + CA 链验证 + 加密传输全链路。",
  },
];
