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
    {
      no: "WP-10u",
      title: "系统内自动更新：A/B 分区 + tar.gz 更新包 + 回滚 + 离线更新",
      desc: "Windows-Update 式系统内更新：磁盘四分区布局（boot/flags + slot A + slot B + data），分区注册为独立块设备（hdapN）并挂载 /ab/boot、/ab/a、/ab/b、/data；内核自带完整 gzip（RFC 1952/1951，stored/fixed/dynamic 三种块、可跨 feed 恢复、CRC32+ISIZE 校验）与流式 ustar 解析（头部校验和、GNU 长名）；下载器为两阶段流式 HTTP/HTTPS（状态码 + Content-Length 校验）；SHA256 校验双层（update.json 的包哈希 + 包内 manifest 的载荷哈希）；boot 标志协议（next_B/ok_B/bootfail_B）实现 GRUB 侧自动回滚；配置扩至 4 项（package_url、online_update）；新增 update/update --local/update --status/rollback/reboot 命令与 7 项 L1 更新接口（总数 85）、11 项测试命令（共 129 条命令）；real_update_test 端到端实测：下载-校验-安装至 slot B-真实重启进入 test1 内核-确认-回滚回 slot A。SeaBIOS 与 OVMF 双引导验证。",
    },
    {
      no: "WP-10c",
      title: "声卡驱动：Intel HDA / AC'97 / SB16 / ES1370 / virtio-snd / USB 音频 + snd_* 扩展接口",
      desc: "声卡框架（snd.c，8 槽注册表）与六族驱动：Intel HDA（MMIO BAR、控制器复位/CORB/RIRB 命令环、codec 地址发现、widget 树枚举、流水线/转换器/引脚配置、BDL DMA + IOC 中断、LPIB 流控）、AC'97 82801AA（mixer + 总线主控 BDL DMA）、Sound Blaster 16（ISA DSP 4.05、8/16 位 DMA、块中断）、ES1370/1371（DAC2 帧 DMA + PCLKDIV）、virtio-snd（modern virtio-pci 控制队列 + TX 队列）、USB Audio Class 1.0（新 UHCI 主机栈：控制传输、设备枚举、同步 OUT 按 1ms 帧调度）；snd_register/play/stop/set_rate/set_volume/get_caps + 六个 per-driver init 入口与 usb.h 主机栈接口（L1 接口总数增至 111）；新增 sound/hda/ac97/sb16/es1370/virtiosnd/usbaudio/play/volume 命令（boot 实测 150 条命令，含 abdisk/install/grub-install/abcfg）、9 项测试命令；44.1/48kHz 采样率配置。QEMU 五卡实测播放（真 DMA 真中断）；virtio-snd 无 QEMU 设备模型如实 SKIPPED。SeaBIOS 与 OVMF 双引导验证。",
    },
    {
      no: "WP-10d",
      title: "USB 主机栈：UHCI/OHCI/EHCI/XHCI 设备级枚举 + HID/MSC/串口/音频类驱动 + Hub/热插拔",
      desc: "四个主机控制器后端全部设备级跑通：UHCI（PIIX3，帧表+QH/TD 调度、全链路含 Hub 级联与热插拔）、OHCI（HCCA+ED/TD 池，独立验证会话全 PASS：kbd/mouse 枚举含产品字符串、usb_core_test 5/5、MSC 容量/MBR/写读回、FAT32 mkfs+挂载+文件往返、真热插拔；修复 3 bug：控制 DATA TD 补 TD_R 缓冲取整、状态 TD 方向位 u8 截断导致每个状态阶段被 STALL、中断 IN 补 TD_R）、EHCI（异步环+qTD 引擎，kbd/mouse/存储枚举、MSC 读写、FAT32 往返、STALL 恢复）、XHCI（命令/事件环+DCBAA、两段式 AddressDevice、端点上下文按 spec 布局；修复 Reset Endpoint/Set TR Dequeue 的 EPID 编码至 bits 20:16——旧布局使控制器以 TRB Error 结束命令、STALL 端点永久无法恢复；枚举/HID/核心测试 PASS，qemu-xhci 的 usb-storage CSW 缺口如实标注）；类驱动：HID 键盘/鼠标（报告描述符解析）、MSC（BOT+SCSI 对接 blk 层）、CDC-ACM/FTDI 串口、UAC 1.0/2.0；外部 Hub 级联枚举与真热插拔；usb/usbdev 状态命令 + 7 项测试命令（boot 实测 159 条命令，L1 接口总数增至 122）。SeaBIOS 与 OVMF 双引导验证。",
    },
    {
      no: "RESTRUCT",
      title: "项目结构重构：新文件体系 + 接口命名规范",
      desc: "全仓目录重组为 kernel/（main.c、arch/x86_64/、core/、mem/、lib/、crypto/、ota/）、drivers/（block/nic/snd/usb/input/display/pci）、fs/（vfs/ramfs/fat32/exfat/ext4）、net/（含 icmp/udp/tcp/dhcp/dns/tls/ssh）、shell/、l1/、libs/、boot/、userprogs/、tools/、tests/、docs/、website/；接口命名统一为 [大类]_[具体]_[更小一级]（kmalloc 保留原名）；纯结构与命名变更，行为不变，全部回归复测 PASS。",
    },
    {
      no: "WP-10-wp08fix1",
      title: "Shell 补全：oc>/ush 全键位行编辑 + ush 15 个工具 + nano 风格编辑器 + 9 个 L1 接口",
      desc: "按 WP-08 规格补全 Shell（不是修 WP-08，是补做没做的）：oc> 与 ush 双端全键位行编辑（上下键历史翻页 32 条草稿保留、左右/Home/End 光标、Tab 命令+VFS 路径补全、Ctrl+A/E/U/K/W、Delete、Ctrl+C）；ush 补齐 15 个工具：ln（真实硬链接共享 inode + nlink）、ln -s（路径解析跟随，8 跳防环）、chmod/chown、sed、awk、ping/wget/netstat/ifconfig 对接内核协议栈（sys_netcmd）、ps/kill/top/du 对接 sys_proc_*、stat/env 补实 help 一直承诺但从未实现的两个命令；nano 风格编辑器双端可用（nano/vi，^O 保存 ^X 退出，真 VFS 落盘）；新增 9 个 L1 扩展接口（item 130-138：shell_lineedit_init/history_add/history_get/cursor_move/tab_complete/ctrlc + editor_open/save/close，L1 总数增至 138）；7 个新 syscall（96-102：symlink/readlink/link/chmod/chown/netcmd/ps）；boot 实测 172 条命令；tools/wp10_wp08fix1_test.py 真实按键端到端 24/24 全 PASS。SeaBIOS 与 OVMF 双引导验证。",
    },
    {
      no: "WP-AUDIT-01",
      title: "完整审查（18 路，607 条）+ 全部 41 个 P0 修复 + P1 修复推进（p1fix1 前 31 条 + p1fix2 第 32~62 条）",
      desc: "18 路逐行完整审查产出 607 条发现（P0 41 / P1 94 / P2 214 / P3 258）。WP-AUDIT-01-p0fix1 修复前 20 个 P0（BUG-0001..0020）：VFS 挂载消息栈溢出、rmdir/umount 挂载点保护、ext4 恶意卷除零、FAT32 BPB/簇号越界写盘、tcptest 引导栈溢出、内核线程栈 1 页改 4 页、SSH 客户端与 sshd 共 14 处预认证溢出。WP-AUDIT-01-p0fix2 修复其余 21 个 P0（BUG-0021..0041）：TLS 请求/记录/消息边界与 TLS 1.3 CertVerify DER、shell glob/fsck/mv、sys_poll nfds 回绕、SYS_EXIT 缓冲、execve CR3 切换次序、sys_ps ktab 堆化、X509 DER 长度溢出、config 写边界、OTA changes/request/路径遍历拒绝（恶意包 a/../..//data 越界写入实测被拒）、NVMe 非 512B LBA 拒绝、嵌套 #PF 防护。41 个 P0 全部修复完毕后，WP-AUDIT-01-p1fix1 修复前 31 个 P1（BUG-0042..0072）：每任务 FPU/SSE fxsave 上下文 + CR4.OSFXSR + fork 继承、PMM 位图 cli 原子性、页错误语义（P=1 拒绝、8MiB 栈下限、U/S 特权环、内核在用户地址空间拒绝）、XHCI 事件环 LINK 与轮询闸锁、EHCI CONFIGFLAG、OHCI 中断表/TD_R/NPS、MSC residue 与 sector_size、CDC-ACM 协议、SS EP0 mps9、FAT32 rmdir 点项 + UAF + 簇环越界 + unlink 保护、exFAT 位图生命周期、ext4 extent 偏移与恶意卷越界、e1000 strcat、ld_so 边界、网络 IP 帧校验 + RX 校验和 + 序号回绕 + RST 校验 + SYN_RCVD 回收 + SYN 选项 + udp_bind 去重 + 窗口缩放 + RTO 临界区。每个修复均有验证证据（QEMU 复现修复前 FAIL / 修复后 PASS、宿主 ASAN 16/16 或构造级路径证据）；回归全绿（18/18 + dhtest 5/5 + cryptotest 3/3 + SSH 双向 + 真网 checkupdate + sse_test/pf_test 新测试 + tcptest/fork_test/ping/pmmrace）。随后 WP-AUDIT-01-p1fix2 修复 P1 第 32~62 条（BUG-0073..0103）：SSH 主机密钥 TOFU 锚点（/etc/ssh_known_hosts 持久化，换钥硬失败拒连）与按机生成 RSA-2048 主机密钥（移除镜像内嵌万能私钥，客户端身份改 /etc/ssh_client_key，全协议调用点核验消费 rdrand CSPRNG）；TLS 按套件密钥长度（ChaCha20/AES-256-GCM 32B）、CertificateEntry 扩展数据跳过、32KiB transcript 懒分配+溢出硬失败、ServerHello 全边界校验、零长记录卡死上限、KeyUpdate/NST 递归改循环、X509 负长度四入口 host_a401_entry_test（ASAN 全拒）；shell capture 栈化（4 层）、相对路径通配符自覆盖、edit 相对路径+截断保护、cp 全量复制、ls/tree/du 有界拼接；ush unalias 空参、nano/cp/mv 补 O_TRUNC、重定向 fd 生命周期三缺陷（泄漏/双重 close/stdin 永久改绑）连同内核 console fd dup/dup2 与 sys_read(0) 键盘行模式（^D EOF）一并修复；L1 console hook 抑制契约、WP-08cd 注册真实接入 oc> 派发与 sys_execve 并补注销接口；文档 ota_update_url→update_url 与 tools/update_server.py 修正；新增 fdref_test/select_zero_test 用户测试。",
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
    {
      no: "WP-10u",
      title: "In-system update: A/B partitions + tar.gz packages + rollback + offline update",
      desc: "Windows-Update-style in-system updates: a four-partition disk layout (boot/flags + slot A + slot B + data) with partitions registered as their own block devices (hdapN) and mounted at /ab/boot, /ab/a, /ab/b and /data; the kernel ships a complete gzip decoder (RFC 1952/1951, stored/fixed/dynamic blocks, resumable across feed boundaries, CRC32+ISIZE verification) and a streaming ustar parser (header checksums, GNU long names); a two-phase streaming HTTP/HTTPS downloader (status code + Content-Length enforced); two SHA256 layers (the package digest in update.json plus the payload digest inside the package manifest); a boot-flag protocol (next_B/ok_B/bootfail_B) that gives GRUB-side automatic rollback; the config file grows to 4 keys (package_url, online_update); new update / update --local / update --status / rollback / reboot commands plus 7 L1 update interfaces (85 in total) and 11 test commands (129 commands at boot); real_update_test verifies the end-to-end chain: download, verify, install into slot B, a real reboot into the test1 kernel, confirmation, and rollback back to slot A. Boot verified with SeaBIOS and OVMF.",
    },
    {
      no: "WP-10c",
      title: "Sound card drivers: Intel HDA / AC'97 / SB16 / ES1370 / virtio-snd / USB audio + the snd_* extension API",
      desc: "A sound-card framework (snd.c, 8-slot registry) plus six driver families: Intel HDA (MMIO BARs, controller reset, CORB/RIRB command rings, codec address discovery, widget-tree enumeration, pin/converter/nid wiring, BDL DMA + IOC interrupts, LPIB flow control), AC'97 82801AA (mixer + bus-master BDL DMA), Sound Blaster 16 (ISA DSP 4.05, 8/16-bit DMA, block interrupts), ES1370/1371 (DAC2 frame DMA + PCLKDIV), virtio-snd (modern virtio-pci control + TX queues) and USB Audio Class 1.0 over a new UHCI host stack (control transfers, device enumeration, isochronous OUT scheduled per 1 ms frame); snd_register/play/stop/set_rate/set_volume/get_caps plus six per-driver init entry points and the usb.h host-stack interface (111 L1 interfaces in total); new sound/hda/ac97/sb16/es1370/virtiosnd/usbaudio/play/volume commands (150 registered at boot, incl. abdisk/install/grub-install/abcfg) and 9 test commands; 44.1/48 kHz sample-rate configuration. Five cards verified playing live in QEMU (real DMA + real interrupts); virtio-snd reports SKIPPED honestly without a QEMU device model. Boot verified with SeaBIOS and OVMF.",
    },
    {
      no: "WP-10d",
      title: "USB host stack: device-level bring-up on UHCI/OHCI/EHCI/XHCI + HID/MSC/serial/audio class drivers + hub and hot-plug",
      desc: "All four host-controller backends verified at device level. UHCI (PIIX3, frame list + QH/TD schedule): full chain incl. hub cascade and hot-plug. OHCI (HCCA + ED/TD pools): independent verification session ALL PASS - kbd/mouse enumeration with product strings, usb_core_test 5/5, MSC capacity/MBR/write-read-back, FAT32 mkfs + mount + file round-trip, real hot-plug; 3 bugs fixed: control DATA TDs now set TD_R (buffer rounding), the status-TD direction was an u8 that truncated the bit-19/20 direction field to SETUP (STALLing every status stage), and interrupt IN TDs set TD_R. EHCI (async ring + qTD engine): kbd/mouse/storage enumeration, MSC read/write, FAT32 round-trip, STALL recovery. XHCI (command/event rings + DCBAA, two-stage AddressDevice, spec-layout endpoint contexts): fixed Reset Endpoint / Set TR Dequeue endpoint-ID encoding to control bits 20:16 - the old layout made the controller retire the commands with TRB Error, leaving STALLed endpoints unrecoverable; enumeration/HID/core tests PASS with the qemu-xhci usb-storage CSW gap documented honestly. Class drivers: HID keyboard/mouse (report-descriptor parsing), MSC (BOT+SCSI wired into blk), CDC-ACM/FTDI serial, UAC 1.0/2.0; external hub cascade and real hot-plug; usb/usbdev status commands + 7 test commands (159 registered at boot, 122 L1 interfaces). Boot verified with SeaBIOS and OVMF.",
    },
    {
      no: "RESTRUCT",
      title: "Project restructure: new file tree + interface naming convention",
      desc: "The whole repository reorganized into kernel/ (main.c, arch/x86_64/, core/, mem/, lib/, crypto/, ota/), drivers/ (block/nic/snd/usb/input/display/pci), fs/ (vfs/ramfs/fat32/exfat/ext4), net/ (incl. icmp/udp/tcp/dhcp/dns/tls/ssh), shell/, l1/, libs/, boot/, userprogs/, tools/, tests/, docs/ and website/; interface names unified to [category]_[specific]_[smaller] (kmalloc keeps its name); a pure structure/naming change with unchanged behaviour — every regression re-run PASS.",
    },
    {
      no: "WP-10-wp08fix1",
      title: "Shell completion: full line editing on oc>/ush + 15 ush tools + the nano-style editor + 9 L1 interfaces",
      desc: "Completed the WP-08 shell specification (not fixing WP-08 — delivering what was never done): full-featured line editing on both oc> and ush (Up/Down history paging over a 32-entry ring with draft preserved, Left/Right/Home/End cursor, Tab command+VFS-path completion, Ctrl+A/E/U/K/W, Delete, Ctrl+C); 15 missing ush tools: ln (real hard links sharing the inode + nlink), ln -s (symlink-following resolution with an 8-hop loop guard), chmod/chown, sed, awk, ping/wget/netstat/ifconfig wired into the kernel network stack (sys_netcmd), ps/kill/top/du wired into sys_proc_*, plus stat/env — the two commands help had always promised but no dispatcher ever implemented; a nano-style editor on both shells (nano/vi, ^O save ^X exit, real VFS persistence); 9 new L1 extension interfaces (items 130-138: shell_lineedit_init/history_add/history_get/cursor_move/tab_complete/ctrlc + editor_open/save/close, 138 in total); 7 new syscalls (96-102: symlink/readlink/link/chmod/chown/netcmd/ps); 172 commands registered at boot; tools/wp10_wp08fix1_test.py end-to-end 24/24 with real keystrokes. Boot verified with SeaBIOS and OVMF.",
    },
    {
      no: "WP-AUDIT-01",
      title: "Full audit (18 routes, 607 findings) + all 41 P0 fixes + P1 fixes in progress (p1fix1 first 31 + p1fix2 items 32..62)",
      desc: "An 18-route line-by-line audit produced 607 findings (P0 41 / P1 94 / P2 214 / P3 258). WP-AUDIT-01-p0fix1 fixed the first 20 P0s (BUG-0001..0020): the VFS mount-message stack overflow, rmdir/umount mount-point protection, the ext4 malicious-volume divide fault, FAT32 BPB/cluster bounds, the tcptest boot-stack overflow, kthread stacks grown from 1 page to 4, and 14 pre-auth overflows across the SSH client and sshd. WP-AUDIT-01-p0fix2 fixed the remaining 21 P0s (BUG-0021..0041): TLS request/record/message bounds and the TLS 1.3 CertVerify DER, shell glob/fsck/mv, sys_poll nfds wrap, SYS_EXIT buffer, execve CR3-switch ordering, sys_ps ktab heap allocation, X509 DER length overflow, config write bounds, OTA changes/request/path-traversal rejection (a hostile a/../..//data package write verified rejected on the real A/B boot path), NVMe non-512-byte LBA refusal, and the nested-#PF guard. With all 41 P0s closed, WP-AUDIT-01-p1fix1 fixed the first 31 P1s (BUG-0042..0072): per-task FPU/SSE fxsave context + CR4.OSFXSR + fork inheritance, PMM bitmap cli atomicity, page-fault semantics (P=1 refusal, 8MiB stack floor, U/S ring, kernel-on-user-address-space refusal), the XHCI event-ring LINK wrap and poll latch, EHCI CONFIGFLAG, the OHCI interrupt table / TD_R / NPS, MSC residue and sector_size, the CDC-ACM protocol, SS EP0 mps9, FAT32 rmdir dot entries + UAF + cluster-cycle bounds + the unlink guard, the exFAT bitmap lifecycle, ext4 extent offsets and crafted-volume bounds, the e1000 strcat, ld_so bounds, and the network stack (IP frame checks + RX checksums + sequence wrap + RST validation + SYN_RCVD reaping + SYN options + udp_bind dedup + window scaling + RTO critical sections). Every fix has verification evidence (QEMU FAIL-before/PASS-after, host ASAN 16/16, or construction-level path evidence); the full regression stayed green (18/18 + dhtest 5/5 + cryptotest 3/3 + SSH both directions + live checkupdate + the new sse_test/pf_test + tcptest/fork_test/ping/pmmrace). Then WP-AUDIT-01-p1fix2 fixed P1 items 32..62 (BUG-0073..0103): the SSH host-key TOFU anchor (/etc/ssh_known_hosts persistence with hard-fail on key change) and per-installation RSA-2048 host key generation (embedded universal private key removed; client identity via /etc/ssh_client_key; CSPRNG consumption verified at all call sites); TLS per-cipher key lengths (ChaCha20/AES-256-GCM 32B), CertificateEntry extension-data skip, a 32KiB lazily-allocated transcript with hard overflow failure, full ServerHello bounds checks, a zero-length record stall cap, KEY_UPDATE/NEW_SESSION_TICKET recursion converted to a loop, and the host_a401_entry_test proving the X509 negative-length fix at all four TLS entry points (ASAN-clean); shell capture stack (4 levels), relative-wildcard self-overwrite, edit relative paths + truncation protection, cp full-file copy, bounded ls/tree/du joins; ush unalias NULL-arg, O_TRUNC for nano/cp/mv, and the three redirect fd-lifecycle defects (leak / double close / permanent stdin rebind) fixed together with kernel console fd dup/dup2 and a sys_read(0) keyboard line mode (^D EOF); the L1 console-hook suppression contract, WP-08cd registration genuinely wired into the oc> dispatch and sys_execve with a symmetric unregister API; docs corrected from ota_update_url to update_url and tools/update_server.py; new fdref_test/select_zero_test user tests.",
    },
  ],
};

export function pickAbout<T>(bi: Bi<T>, locale: Locale): T {
  return bi[locale];
}
