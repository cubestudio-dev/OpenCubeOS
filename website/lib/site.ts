// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 cubestudio-dev <cubestudio@qq.com>
//
// Central site data. Every number is a real WP-AUDIT-01-p1fix2 value
// taken from the repository docs (README.md, docs/INTERFACES.md,
// docs/EXTENSIONS_WP10-wp08fix1.md, docs/TRY-IT.md, docs/UPDATE-HOWTO.md)
// and real sha256sum/build outputs.
// 3-way identical: local build + GitHub wp-audit-01-p1fix2 tag + site /downloads/ + this site /downloads/

export const BASE = "/OpenCubeOS";

export const GITHUB_REPO = "https://github.com/cubestudio-dev/OpenCubeOS";
export const RELEASE_LATEST =
  "https://github.com/cubestudio-dev/OpenCubeOS/releases/tag/wp-audit-01-p1fix2";
export const RELEASES = "https://github.com/cubestudio-dev/OpenCubeOS/releases";
export const SITE_URL = "https://cubestudio-dev.github.io/OpenCubeOS/";

// Real assets (build/ + GitHub wp-audit-01-p1fix2 tag + site /downloads/ + this site /downloads/, 3-way identical)
export const ISO_FILE = "opencube-wp-audit-01-p1fix2.iso";
export const ISO_SIZE_B = 11372544;
export const ISO_SIZE_MB = "10.84";
export const ISO_SHA256 =
  "960af5549d7ae7408ccd0f5240d878435c96c14d60c68faec68120fe14ef1ff6";
export const ISO_URL = `${BASE}/downloads/${ISO_FILE}`;

export const SRC_FILE = "opencube-wp-audit-01-p1fix2-src.zip";
export const SRC_SIZE_B = 2679430;
export const SRC_SIZE_MB = "2.55";
export const SRC_SHA256 =
  "3bb6ce648d17e2d91b63296ef0d8baf26dbe322f3893b0e9683e0bec1a18746b";
export const SRC_URL = `${BASE}/downloads/${SRC_FILE}`;

// WP-AUDIT-01-p1fix2 stats. Source lines: find kernel boot userprogs fs net shell l1 drivers libs \( -name '*.c' -o -name '*.h' -o -name '*.S' \) | xargs wc -l = 93,923.
// Shell commands: live boot count = 173 (help -a prints "Total: 173 commands"; p1fix1 added the pmmrace command).
// L1 extension interfaces: 138 items (unchanged; p1fix2 added no l1/ headers).
export const STATS = [
  { value: "93,923", label: "行源码" },
  { value: "138", label: "L1 扩展接口" },
  { value: "44", label: "系统调用" },
  { value: "173", label: "shell 命令" },
  { value: "16", label: "工作包" },
  { value: "41/41", label: "P0 修复测试" },
];

// WP-AUDIT-01 (607-finding audit + all 41 P0 fixes + P1 fixes in progress: p1fix1 first 31 + p1fix2 items 32..62) leads the user-facing list
export const FEATURES = [
  {
    name: "完整安全审查 + P0 全部修复 + P1 修复推进",
    tag: "WP-AUDIT-01-p1fix2",
    desc: "18 路逐行审查 607 条发现（P0 41 / P1 94 / P2 214 / P3 258）；41 个 P0 全部修复（p0fix1 前 20 + p0fix2 后 21，逐条有 QEMU FAIL-before/PASS-after、宿主 ASAN 或构造级路径证据）；WP-AUDIT-01-p1fix1 修复前 31 个 P1（BUG-0042..0072）：每任务 FPU/SSE fxsave 上下文 + CR4.OSFXSR + fork 继承、PMM 位图 cli 原子性、页错误语义（P=1 拒绝、8MiB 栈下限、U/S 特权环、内核在用户地址空间拒绝）、XHCI 事件环 LINK 与轮询闸锁、EHCI CONFIGFLAG、OHCI 中断表/TD_R/NPS、MSC residue 与 sector_size、CDC-ACM 协议、SS EP0 mps9、FAT32 rmdir 点项 + UAF + 簇环越界 + unlink 保护、exFAT 位图生命周期、ext4 extent 偏移与恶意卷越界、e1000 strcat、ld_so 边界、网络 IP 帧校验 + RX 校验和 + 序号回绕 + RST 校验 + SYN_RCVD 回收 + SYN 选项 + udp_bind 去重 + 窗口缩放 + RTO 临界区；新增 sse_test/pf_test 用户测试，回归 tcptest/fork_test/ping/pmmrace 全绿。p1fix2 修复 P1 第 32~62 条（BUG-0073..0103）：SSH 主机密钥 TOFU 锚点（/etc/ssh_known_hosts 持久化，换钥硬失败拒连，MITM 检测）与按机生成 RSA-2048 主机密钥（移除镜像内嵌万能私钥，客户端身份改 /etc/ssh_client_key，全协议调用点核验消费 rdrand CSPRNG）；TLS 按套件密钥长度（ChaCha20/AES-256-GCM 32B，修复选中即失败）、CertificateEntry 扩展数据跳过、32KiB transcript 懒分配+溢出硬失败、ServerHello 全边界校验、零长记录卡死上限、KeyUpdate/NST 递归改循环、X509 负长度四入口 host_a401_entry_test（ASAN 全拒）；shell capture 栈化（4 层嵌套不串数据）、相对路径通配符自覆盖修复、edit 相对路径解析+超容量警告+拒绝缩短保存、cp 全量复制不再静默截断、ls/tree/du 有界路径拼接；ush unalias 空参崩溃修复、nano 保存/cp/mv 补 O_TRUNC（短内容覆盖长文件不再残留旧尾字节）、重定向 fd 生命周期三缺陷（泄漏/双重 close/stdin 永久改绑）连同内核 console fd dup/dup2 支持与 sys_read(0) 键盘行模式（^D EOF）一并修复；L1 console hook 抑制契约落地、WP-08cd 注册真实接入 oc> 派发与 sys_execve 并补对称注销接口；文档 ota_update_url→update_url 与 tools/update_server.py 名称修正；新增 fdref_test/select_zero_test 用户测试，回归全绿。",
  },
  {
    name: "Shell 完全体（行编辑 + 工具 + 编辑器）",
    tag: "WP-10-wp08fix1",
    desc: "oc> 与 ush 双端全键位行编辑：上下键历史翻页（32 条，草稿保留）、左右/Home/End 光标、Ctrl+A/E/U/K/W、Delete、Ctrl+C、Tab 补全（命令表 + VFS 路径）。ush 补齐 15 个工具：ln（真实硬链接共享 inode + nlink）、ln -s（路径解析跟随，8 跳防环）、chmod/chown/sed/awk/ping/wget/netstat/ifconfig/ps/kill/top/du + 补实 help 承诺过的 stat/env。双端 nano 风格编辑器（nano/vi），^O 保存 ^X 退出；9 个新扩展接口（130-138）。",
  },
  {
    name: "系统内自足（第 ⑨ 条自宿主）",
    tag: "rule-9 self-host",
    desc: "A/B 磁盘创建、系统安装到硬盘、GRUB 引导器安装全部在 oc> 内完成（abdisk / install / grub-install / abcfg），不需要宿主机脚本：内核经 multiboot2 module 随启动介质自带副本，GRUB 引导数据构建期内嵌；启动时自动注册所有分区（<盘>pN）；FAT32 按微软簇表选簇。QEMU 实测：abdisk 后拔掉 ISO 独立引导、OTA 升级重启进 slot B、install 后独立引导并可再次 install（自举）。",
  },
  {
    name: "声卡驱动（六族）",
    tag: "WP-10c",
    desc: "Intel HDA（CORB/RIRB、codec/widget 枚举、BDL DMA、IOC 中断）、AC'97、Sound Blaster 16（ISA DMA）、ES1370、virtio-snd、USB Audio Class 1.0（新 UHCI 主机栈）：snd_register/play/stop/set_rate/set_volume/get_caps 扩展接口，44.1/48kHz，真 DMA 真中断，QEMU 五卡实测播放。",
  },
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
  {
    no: "WP-10c",
    title: "声卡驱动：Intel HDA / AC'97 / SB16 / ES1370 / virtio-snd / USB 音频 + snd_* 扩展接口",
  },
  {
    no: "WP-10-project_restructure",
    title: "项目结构重构：一模块一目录 + [大类]_[具体] 命名规范 + libs/",
  },
  {
    no: "WP-AUDIT-01",
    title: "完整审查（18 路，607 条）+ P0 全部 41 个修复 + P1 修复推进（p1fix1 前 31 条 + p1fix2 第 32~62 条）",
  },
];

// Verification (README.md "Tests" + docs/EXTENSIONS_WP10a/b/u/c.md + docs/EXTENSIONS_WP10-wp08fix1.md)
export const VERIFY = [
  {
    name: "WP-08 补全端到端 24/24",
    desc: "tools/wp10_wp08fix1_test.py 用真实按键（串口直注 + sendkey 方向键）驱动：oc> 历史/光标/Tab/Ctrl+C/导航键、ush 同套键位、ln 硬链接+软链接、chmod/chown+stat、sed、awk、ps/top/du、ifconfig/netstat/ping、双端 nano 编辑器写文件回读，24/24 全 PASS。",
  },
  {
    name: "18/18 QEMU 全量回归",
    desc: "boot 横幅 + uname + 12 个用户程序 + p3_test + heaptest + l1test + crashlog，单次 QEMU 会话完成（挂全部四类盘）。",
  },
  {
    name: "WP-10c 声卡测试 9 项",
    desc: "hda_test / ac97_test / sb16_test / es1370_test / usb_audio_test 在 QEMU 实测初始化、能力、DMA 播放与中断计数全 PASS；audio_rw_test 五卡逐个播放回读 PASS；sample_rate_test 44.1/48kHz 配置 PASS；virtio_snd_test 无 QEMU 设备模型如实 SKIPPED；real_hw_test 如实 NOT RUN。",
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
