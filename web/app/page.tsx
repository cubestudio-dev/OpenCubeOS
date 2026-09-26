'use client';

import { useState } from 'react';

const ISO_SHA256 = '37c9ad6c2b0375c5d98693675c2fb2998e545d325f0da78a95b7967a16b34cff';
const ISO_SIZE_BYTES = 10510336;
const ISO_SIZE_MB = (ISO_SIZE_BYTES / (1024 * 1024)).toFixed(2);
const SRC_ZIP_SHA256 = '5b79b0aaa4716d121c9ebd2d996c7ee36820237b19522fa5d6ba320f552b5c99';
const SRC_ZIP_SIZE_BYTES = 3675487;
const SRC_ZIP_SIZE_MB = (SRC_ZIP_SIZE_BYTES / (1024 * 1024)).toFixed(2);

const WP08a_FEATURES: { title: string; desc: string; badge?: string }[] = [
  { title: '真 POSIX fork', desc: '子进程从 fork() 调用点继续执行，地址空间完整复制，父返回子 PID，子返回 0。纯汇编 enter_ring3_fork。', badge: 'NEW' },
  { title: '23 个系统调用', desc: 'fork / exec / wait / exit / getpid / getppid / pipe / dup / dup2 / kill / signal / sigaction / sigreturn / mmap / munmap / mprotect / brk / chdir / getcwd / ioctl / read / write / select / poll。', badge: 'NEW' },
  { title: 'IPC 管道', desc: 'pipe / dup / dup2 — 真实文件描述符，跨进程通信，阻塞 I/O，读者/写者计数。', badge: 'NEW' },
  { title: '信号机制', desc: 'kill / signal / sigaction / sigreturn — SIGUSR1 / SIGKILL / SIGSEGV 等，跨进程信号发送。', badge: 'NEW' },
  { title: 'I/O 多路复用', desc: 'select / poll — 监听多个管道 fd，阻塞唤醒机制，超时返回。', badge: 'NEW' },
  { title: '内存管理', desc: 'mmap / munmap / mprotect / brk — 用户态内存映射与堆扩展。', badge: 'NEW' },
  { title: '8 个 L1 扩展接口', desc: 'proc_fork / pipe_create / signal_register / sys_mmap / sys_select 等（编号 33-40），总 L1 接口数 40。', badge: 'NEW' },
  { title: '7 个测试程序全部 PASS', desc: 'fork_test（真 fork）/ exec_test（fork+exec+wait）/ pipe_test（跨进程管道）/ signal_test（fork+kill）/ select_test（fork+select）/ mmap_test / hello。', badge: 'NEW' },
];

const WP07_FEATURES: { title: string; desc: string; badge?: string }[] = [
  { title: '块设备框架', desc: 'blk_register_device / read_sectors / write_sectors，统一 ATA / virtio / NVMe 接口。' },
  { title: '磁盘缓存', desc: '64 槽 LRU 写回缓存，blk_cache_stats / blk_cache_flush。' },
  { title: 'FAT32 读写', desc: '创建 / 删除 / 写入 / 读取文件，LFN 读取，簇链扩展。' },
  { title: 'exFAT 读写', desc: '创建 / 删除 / 写入 / 读取，目录项解析，簇堆布局。', badge: 'NEW' },
  { title: 'ext4 只读', desc: '超级块 / inode / extent / 目录项读取。', badge: 'NEW' },
  { title: 'virtio-blk 驱动', desc: 'PCI legacy 传输，virtqueue，3 描述符请求（hdr/data/status）。', badge: 'NEW' },
  { title: 'NVMe 驱动', desc: 'PCI MMIO，管理队列 + I/O 队列，Identify Controller / Namespace。', badge: 'NEW' },
  { title: 'GPT 分区', desc: '解析 GPT 头 + 分区项，最多 16 个分区。', badge: 'NEW' },
  { title: 'MBR 分区', desc: '解析 MBR 分区表，与 GPT 共存。', badge: 'NEW' },
  { title: 'Shell 命令', desc: 'lsblk / parted / mkfs.fat32 / sync / df / du / mount / umount。', badge: 'NEW' },
];

const WP06_FEATURES: { title: string; desc: string }[] = [
  { title: 'PCI 总线驱动', desc: '扫描所有总线/设备/功能，读写配置空间。lspci 命令打印完整设备树。e1000 通过 PCI BAR0 自动识别。' },
  { title: 'e1000 网卡驱动', desc: 'MMIO 寄存器访问，RX/TX 描述符环，MAC 地址读取与配置。支持 Intel 82540EM (QEMU 默认网卡)。' },
  { title: '以太网层', desc: '以太网帧收发，MAC 地址管理，ethertype 多路分发（ARP/IP）。支持广播帧。' },
  { title: 'ARP 协议', desc: 'IP→MAC 地址解析，ARP 请求/应答，ARP 缓存表。arp 命令查看缓存。' },
  { title: 'IP/ICMP 协议', desc: 'IP 数据包收发，校验和计算，TTL 递减。ICMP echo request/reply 实现 ping。' },
  { title: 'UDP 协议', desc: '无连接数据报，端口绑定，数据报收发，校验和计算。DNS/DHCP 基于 UDP。' },
  { title: 'TCP 协议', desc: '三次握手建立连接，四次挥手关闭，TCP 状态机。序列号/确认号管理。' },
  { title: 'DHCP 客户端', desc: 'DISCOVER→OFFER→REQUEST→ACK 四步获取 IP/掩码/网关/DNS。' },
  { title: 'DNS 客户端', desc: '通过 UDP 向 DNS 服务器发送查询，解析 A 记录（域名→IP）。' },
  { title: 'Socket API + 网络命令', desc: 'socket/bind/connect/send/recv/close。7 个命令：ifconfig/ping/dhcp/dns/netstat/wget/lspci。' },
];

const WP05_FEATURES: { title: string; desc: string }[] = [
  { title: 'VFS 抽象层', desc: 'vfs_register_fs/mount/umount, open/read/write/seek/close/stat, mkdir/rmdir/readdir。' },
  { title: 'ramfs 内存文件系统', desc: '自动挂载到 /，默认创建 /etc /tmp /dev /bin。支持文件创建/删除/读写。' },
  { title: 'FAT32 文件系统', desc: 'BPB 解析，FAT 表缓存，目录项读取，8.3 短文件名。只读挂载。' },
  { title: 'ATA/IDE 磁盘驱动', desc: 'LBA28 PIO 模式，主/从通道，IDENTIFY 检测，4 个驱动器扫描。' },
  { title: 'Shell 管道/重定向', desc: 'cmd1 | cmd2, > >> <, 环境变量 $VAR, alias, && || ;, 通配符 * ?。' },
  { title: '16 个文件命令', desc: 'ls/cd/pwd/cat/mkdir/rmdir/touch/rm/mv/cp/echo/tree/df/du/mount/umount。' },
];

const WP04_FEATURES: { title: string; desc: string }[] = [
  { title: '抢占式调度器', desc: '基于优先级的轮转调度，PIT 100 Hz 驱动，时间片 20ms。' },
  { title: '同步原语', desc: '自旋锁/信号量/互斥锁/条件变量，带优先级继承。' },
  { title: '用户态 (ring-3)', desc: '真正进入 ring-3 执行，syscall 注册，ELF 加载器。' },
  { title: '3 个新 L1 接口', desc: '任务管理、同步原语、用户态/syscall。' },
];

const WP03_FEATURES: { title: string; desc: string }[] = [
  { title: 'PMM + VMM + 内核堆', desc: '物理内存位图分配器，四级页表，kmalloc/kfree。' },
  { title: 'Shell 命令注册', desc: 'L1 能注册自定义 Shell 命令。13 个内置命令。' },
  { title: '4 个新 L1 接口', desc: 'PMM/VMM/Heap/Shell 接口。' },
  { title: 'em dash 修复', desc: '全部源文件非 ASCII 字符替换为 ASCII。' },
];

const WP02_FEATURES: { title: string; desc: string }[] = [
  { title: '中断系统', desc: 'IDT + GDT + TSS + PIC 重映射。' },
  { title: 'CPU 异常 + 定时器', desc: '#DE/#UD/#PF/#GP/#DF 捕获，PIT 100 Hz。' },
  { title: 'PS/2 键盘 + 串口', desc: '扫描码转 ASCII，COM1 RX。' },
  { title: '4 个新 L1 接口', desc: '中断/定时器/键盘/异常接口。' },
];

const WP01_FEATURES: { title: string; desc: string }[] = [
  { title: 'BIOS + UEFI 双引导', desc: '一份 ISO 同时支持 BIOS 和 UEFI 启动。' },
  { title: '64 位长模式 + 帧缓冲', desc: '4 GiB 恒等映射，800x600x32 framebuffer。' },
  { title: '8x16 位图字体', desc: '内置 256 字符 IBM VGA 风格字体。' },
  { title: '4 个 L1 接口', desc: '帧缓冲/渲染器/字体/控制台钩子。' },
];

export default function Home() {
  const [copied, setCopied] = useState(false);

  const copySha = () => {
    navigator.clipboard.writeText(ISO_SHA256).then(() => {
      setCopied(true);
      setTimeout(() => setCopied(false), 2000);
    });
  };

  return (
    <div className="min-h-screen flex flex-col bg-[#0a0a0f] text-[#e0e0e0]">
      <header className="border-b border-[#1f1f2a] bg-[#0d0d14]">
        <div className="max-w-5xl mx-auto px-6 py-4 flex items-center justify-between">
          <div className="flex items-center gap-3">
            <div className="w-10 h-10 rounded-md bg-gradient-to-br from-[#00e0d0] to-[#0070c0] flex items-center justify-center text-[#0a0a0f] font-bold text-lg">OC</div>
            <div>
              <div className="font-semibold text-base">Open Cube OS</div>
              <div className="text-xs text-[#707080]">工作包 WP-08a · 完整系统调用集</div>
            </div>
          </div>
          <a href="https://www.apache.org/licenses/LICENSE-2.0" target="_blank" rel="noopener noreferrer" className="text-xs px-3 py-1.5 rounded border border-[#2a2a35] hover:border-[#00e0d0] hover:text-[#00e0d0] transition">Apache 2.0</a>
        </div>
      </header>

      <main className="flex-1 max-w-5xl mx-auto w-full px-6 py-10">
        <section className="text-center py-10">
          <div className="inline-block px-3 py-1 mb-4 text-xs rounded-full border border-[#00e0d0]/40 text-[#00e0d0]">WP-08a 已完成</div>
          <h1 className="text-4xl md:text-5xl font-bold mb-4 bg-gradient-to-r from-[#00e0d0] via-[#80f0f0] to-[#0070c0] bg-clip-text text-transparent">Open Cube OS</h1>
          <p className="text-base text-[#a0a0b0] max-w-2xl mx-auto">一个能被扩展成任何东西的内核。WP-08a 新增了完整系统调用集：真 POSIX fork（子进程从 fork 调用点返回）、exec、wait、pipe、dup/dup2、signal/sigaction/sigreturn、kill、mmap/munmap/mprotect/brk、chdir/getcwd、ioctl、select/poll。23 个系统调用，8 个新 L1 扩展接口，7 个测试程序全部 PASS。</p>
        </section>

        <section className="bg-[#0f0f17] border border-[#1f1f2a] rounded-xl p-6 md:p-8 my-8">
          <h2 className="text-lg font-semibold mb-1">下载</h2>
          <p className="text-sm text-[#707080] mb-6">WP-08a 版本 · 构建于 2026-09-25</p>
          <div className="grid md:grid-cols-2 gap-4">
            <a href="/downloads/opencube-wp08a.iso" download className="block p-5 rounded-lg bg-gradient-to-br from-[#00e0d0]/10 to-[#0070c0]/10 border border-[#00e0d0]/30 hover:border-[#00e0d0] transition group">
              <div className="flex items-start justify-between mb-2">
                <div>
                  <div className="font-medium text-[#00e0d0] group-hover:text-[#80f0f0]">opencube-wp08a.iso</div>
                  <div className="text-xs text-[#707080] mt-1">可启动 ISO · BIOS + UEFI 双引导</div>
                </div>
              </div>
              <div className="text-xs text-[#a0a0b0] font-mono">{ISO_SIZE_MB} MB · {ISO_SIZE_BYTES.toLocaleString()} 字节</div>
            </a>
            <a href="/downloads/opencube-wp08a-src.zip" download className="block p-5 rounded-lg bg-[#14141c] border border-[#2a2a35] hover:border-[#707080] transition group">
              <div className="flex items-start justify-between mb-2">
                <div>
                  <div className="font-medium">opencube-wp08a-src.zip</div>
                  <div className="text-xs text-[#707080] mt-1">源码包 · 含构建脚本、文档、字体生成器</div>
                </div>
              </div>
              <div className="text-xs text-[#a0a0b0] font-mono">{SRC_ZIP_SIZE_MB} MB · {SRC_ZIP_SIZE_BYTES.toLocaleString()} 字节</div>
            </a>
          </div>
          <div className="mt-6">
            <img src="/downloads/shot-wp07.png" alt="WP-07 启动截图 · lsblk 与 FAT32 读写验证" className="rounded-lg border border-[#1f1f2a] w-full" />
            <div className="text-xs text-[#707080] mt-2 text-center">WP-07 运行截图 · lsblk 列出 hda [ATA]，fatmount + cat 验证 FAT32 读写</div>
          </div>
          <div className="mt-6 pt-6 border-t border-[#1f1f2a]">
            <div className="flex items-center justify-between mb-2">
              <div className="text-xs text-[#707080] uppercase tracking-wide">ISO SHA256</div>
              <button onClick={copySha} className="text-xs px-2 py-1 rounded border border-[#2a2a35] hover:border-[#00e0d0] hover:text-[#00e0d0] transition">{copied ? '已复制' : '复制'}</button>
            </div>
            <code className="block text-xs font-mono text-[#a0e0e0] break-all">{ISO_SHA256}</code>
          </div>
        </section>

        <section className="my-10">
          <h2 className="text-xl font-semibold mb-4">WP-08a 新增功能：完整系统调用集</h2>
          <div className="grid md:grid-cols-2 lg:grid-cols-3 gap-4">
            {WP08a_FEATURES.map((f) => (
              <div key={f.title} className="p-5 rounded-lg bg-[#0f0f17] border border-[#00e0d0]/20 hover:border-[#00e0d0]/40 transition">
                <div className="flex items-center gap-2 mb-2">
                  <div className="font-medium text-sm text-[#00e0d0]">{f.title}</div>
                  {f.badge && <span className="text-[10px] px-1.5 py-0.5 rounded bg-[#00e0d0]/20 text-[#00e0d0] font-mono">{f.badge}</span>}
                </div>
                <div className="text-xs text-[#a0a0b0] leading-relaxed">{f.desc}</div>
              </div>
            ))}
          </div>
        </section>

        <section className="my-10">
          <h2 className="text-xl font-semibold mb-4">WP-06 已有功能</h2>
          <div className="grid md:grid-cols-2 lg:grid-cols-3 gap-4">
            {WP06_FEATURES.map((f) => (
              <div key={f.title} className="p-5 rounded-lg bg-[#0f0f17] border border-[#1f1f2a] hover:border-[#2a2a35] transition">
                <div className="font-medium text-sm mb-2 text-[#80c0c0]">{f.title}</div>
                <div className="text-xs text-[#a0a0b0] leading-relaxed">{f.desc}</div>
              </div>
            ))}
          </div>
        </section>

        <section className="my-10">
          <h2 className="text-xl font-semibold mb-4">WP-05 已有功能</h2>
          <div className="grid md:grid-cols-2 lg:grid-cols-3 gap-4">
            {WP05_FEATURES.map((f) => (
              <div key={f.title} className="p-5 rounded-lg bg-[#0f0f17] border border-[#1f1f2a] hover:border-[#2a2a35] transition">
                <div className="font-medium text-sm mb-2 text-[#80a0a0]">{f.title}</div>
                <div className="text-xs text-[#a0a0b0] leading-relaxed">{f.desc}</div>
              </div>
            ))}
          </div>
        </section>

        <section className="my-10">
          <h2 className="text-xl font-semibold mb-4">WP-04 已有功能</h2>
          <div className="grid md:grid-cols-2 lg:grid-cols-3 gap-4">
            {WP04_FEATURES.map((f) => (
              <div key={f.title} className="p-5 rounded-lg bg-[#0f0f17] border border-[#1f1f2a] hover:border-[#2a2a35] transition">
                <div className="font-medium text-sm mb-2 text-[#808080]">{f.title}</div>
                <div className="text-xs text-[#a0a0b0] leading-relaxed">{f.desc}</div>
              </div>
            ))}
          </div>
        </section>

        <section className="my-10">
          <h2 className="text-xl font-semibold mb-4">WP-03 已有功能</h2>
          <div className="grid md:grid-cols-2 lg:grid-cols-3 gap-4">
            {WP03_FEATURES.map((f) => (
              <div key={f.title} className="p-5 rounded-lg bg-[#0f0f17] border border-[#1f1f2a] hover:border-[#2a2a35] transition">
                <div className="font-medium text-sm mb-2 text-[#808080]">{f.title}</div>
                <div className="text-xs text-[#a0a0b0] leading-relaxed">{f.desc}</div>
              </div>
            ))}
          </div>
        </section>

        <section className="my-10">
          <h2 className="text-xl font-semibold mb-4">WP-02 已有功能</h2>
          <div className="grid md:grid-cols-2 lg:grid-cols-3 gap-4">
            {WP02_FEATURES.map((f) => (
              <div key={f.title} className="p-5 rounded-lg bg-[#0f0f17] border border-[#1f1f2a] hover:border-[#2a2a35] transition">
                <div className="font-medium text-sm mb-2 text-[#808080]">{f.title}</div>
                <div className="text-xs text-[#a0a0b0] leading-relaxed">{f.desc}</div>
              </div>
            ))}
          </div>
        </section>

        <section className="my-10">
          <h2 className="text-xl font-semibold mb-4">WP-01 已有功能</h2>
          <div className="grid md:grid-cols-2 lg:grid-cols-3 gap-4">
            {WP01_FEATURES.map((f) => (
              <div key={f.title} className="p-5 rounded-lg bg-[#0f0f17] border border-[#1f1f2a] hover:border-[#2a2a35] transition">
                <div className="font-medium text-sm mb-2 text-[#808080]">{f.title}</div>
                <div className="text-xs text-[#a0a0b0] leading-relaxed">{f.desc}</div>
              </div>
            ))}
          </div>
        </section>

        <section className="my-10 p-6 rounded-xl bg-[#0f0f17] border border-[#00e0d0]/20">
          <h2 className="text-xl font-semibold mb-2">WP-07 新增 L1 扩展接口</h2>
          <p className="text-sm text-[#a0a0b0] mb-4">WP-07 在 WP-01~06 基础上新增 6 个扩展接口。全部 32 个接口保持向下兼容（26 个来自 WP-01~06 + 6 个新增）。</p>
          <ul className="space-y-2 text-sm">
            <li><span className="text-[#00e0d0] font-mono text-xs">27.</span> <code className="text-[#80f0f0] font-mono text-xs">blk_register_device() · blk_unregister_device()</code> <span className="text-[#a0a0b0]">— 块设备注册 / 注销</span></li>
            <li><span className="text-[#00e0d0] font-mono text-xs">28.</span> <code className="text-[#80f0f0] font-mono text-xs">blk_read_sectors() · blk_write_sectors()</code> <span className="text-[#a0a0b0]">— 扇区级读写（统一 ATA / virtio / NVMe）</span></li>
            <li><span className="text-[#00e0d0] font-mono text-xs">29.</span> <code className="text-[#80f0f0] font-mono text-xs">part_parse_mbr() · part_parse_gpt()</code> <span className="text-[#a0a0b0]">— 分区表解析</span></li>
            <li><span className="text-[#00e0d0] font-mono text-xs">30.</span> <code className="text-[#80f0f0] font-mono text-xs">part_get_partition()</code> <span className="text-[#a0a0b0]">— 分区查询</span></li>
            <li><span className="text-[#00e0d0] font-mono text-xs">31.</span> <code className="text-[#80f0f0] font-mono text-xs">blk_cache_stats()</code> <span className="text-[#a0a0b0]">— 缓存命中 / 未命中统计</span></li>
            <li><span className="text-[#00e0d0] font-mono text-xs">32.</span> <code className="text-[#80f0f0] font-mono text-xs">blk_cache_flush()</code> <span className="text-[#a0a0b0]">— 写回缓存刷新</span></li>
          </ul>
        </section>

        <section className="my-10 p-6 rounded-xl bg-[#0f0f17] border border-[#00e0d0]/20">
          <h2 className="text-xl font-semibold mb-3">磁盘验证结果</h2>
          <p className="text-sm text-[#a0a0b0] mb-4">WP-07 磁盘子系统经过端到端验证，ATA 路径全部通过：</p>
          <div className="space-y-3">
            <div className="p-4 rounded-lg bg-[#0a0a0f] border border-[#00e0d0]/30">
              <div className="text-sm font-medium text-[#00e0d0] mb-1">✓ FAT32 读写</div>
              <p className="text-xs text-[#a0a0b0]"><code className="text-[#80f0f0]">fatmount ata0 /mnt</code> → <code className="text-[#80f0f0]">cat /mnt/HELLO.TXT</code> → "Hello, FAT32!" · <code className="text-[#80f0f0]">echo WP07 &gt; /mnt/test.txt</code> → <code className="text-[#80f0f0]">cat /mnt/test.txt</code> → "WP07"（创建 + 写入 + 读取验证通过）。</p>
            </div>
            <div className="p-4 rounded-lg bg-[#0a0a0f] border border-[#00e0d0]/30">
              <div className="text-sm font-medium text-[#00e0d0] mb-1">✓ ramfs 读写</div>
              <p className="text-xs text-[#a0a0b0]"><code className="text-[#80f0f0]">fstest</code> → mkdir / write / read / ls 全部 OK。</p>
            </div>
            <div className="p-4 rounded-lg bg-[#0a0a0f] border border-[#00e0d0]/30">
              <div className="text-sm font-medium text-[#00e0d0] mb-1">✓ 块设备</div>
              <p className="text-xs text-[#a0a0b0]"><code className="text-[#80f0f0]">lsblk</code> → hda [ATA] 8192 sectors；磁盘缓存 hits / misses 统计正常输出。</p>
            </div>
            <div className="p-4 rounded-lg bg-[#0a0a0f] border border-[#00e0d0]/30">
              <div className="text-sm font-medium text-[#00e0d0] mb-1">✓ 磁盘缓存</div>
              <p className="text-xs text-[#a0a0b0]">64 槽 LRU 写回缓存工作正常；<code className="text-[#80f0f0]">sync</code> 命令刷新缓存到磁盘。</p>
            </div>
            <div className="p-4 rounded-lg bg-[#0a0a0f] border border-[#ffa040]/30">
              <div className="text-sm font-medium text-[#ffa040] mb-1">⚠ virtio-blk / NVMe</div>
              <p className="text-xs text-[#a0a0b0]">驱动已编译注册，QEMU 检测待调优（PCI 设备 ID 匹配问题）。ATA 路径完全可用，可作为主磁盘验证。</p>
            </div>
          </div>
        </section>

        <section className="my-10 p-6 rounded-xl border border-[#2a2a35] bg-[#0a0a0f]">
          <h2 className="text-base font-semibold mb-3 text-[#ffa040]">诚实申报</h2>
          <ul className="space-y-2 text-xs text-[#a0a0b0] list-disc list-inside">
            <li>exFAT 格式化未实现（mkfs.exfat 返回提示），仅支持挂载已有 exFAT 文件系统读写。</li>
            <li>ext4 是只读的（写入留到后续 WP）。</li>
            <li>virtio-blk / NVMe 驱动已编译注册，QEMU 设备检测待调优（PCI 设备 ID 匹配问题），目前主要测试路径为 ATA (IDE)。</li>
            <li>GPT 分区解析已实现，需要 virtio-blk 或 NVMe 设备实际测试。</li>
            <li>磁盘缓存是单扇区粒度（不是簇粒度）；簇级缓存由文件系统层自行管理。</li>
            <li>FAT32 写入不支持 LFN（仅 8.3 短文件名写入），LFN 读取已支持。</li>
            <li>e1000 网卡驱动使用轮询模式（无 IRQ），10ms 定时器轮询 RX/TX 描述符环，CPU 占用较高。</li>
            <li>TCP 实现无重传、无流控、无拥塞控制。丢包后不重传，仅适合局域网稳定环境。无滑动窗口，固定窗口大小。</li>
            <li>DHCP/DNS 使用阻塞等待（带超时，DHCP 5 秒 / DNS 3 秒）。获取 IP 或解析域名时整个内核停止响应其他输入。</li>
            <li>无真实路由表（仅默认网关）。所有非本地 IP 都走默认网关，无法手动添加静态路由。</li>
            <li>网络收发依赖定时器轮询（10ms 一次），不是中断驱动。高负载下可能丢包。</li>
            <li>ARP 缓存条目固定 TTL（60 秒），无主动刷新机制。</li>
            <li>TCP 状态机不完整：不处理 TIME_WAIT 2MSL 等待；不支持半关闭 (half-close)。</li>
            <li>仅支持 IPv4，不支持 IPv6；不支持 IP 分片/重组。</li>
            <li>不支持 TCP 选项（MSS / SACK / Window Scale / Timestamps），固定 1460 MSS。</li>
            <li>Socket API 是阻塞式的（无 select / poll / epoll），单线程服务器无法并发处理多连接。</li>
            <li>DNS 仅支持 A 记录查询，不支持 CNAME 链解析，不支持 IPv6 AAAA 记录。</li>
            <li>wget 仅支持 HTTP（无 HTTPS / TLS），不支持 chunked encoding，不跟随重定向。</li>
            <li>仅支持 Intel 82540EM (e1000) 一款网卡；其他型号未实现。</li>
            <li>ATA 驱动使用 PIO 轮询模式（无 DMA，无 IRQ）。CPU 占用较高。</li>
            <li>Shell 管道使用内存缓冲区传递数据，不是真正的文件描述符。</li>
            <li>没有真正的进程管理；后台 &amp; 是空操作（语法接受但不并发执行）。</li>
            <li>通配符 * ? 仅在当前目录展开，不递归子目录。</li>
            <li>VFS 不支持相对路径。所有路径必须是绝对路径。</li>
            <li>O_CREAT 在 vfs_open 中只对 ramfs 有效；FAT32 的 mkdir 返回 -1。</li>
            <li>挂载表固定 16 条，文件描述符表固定 64 条，文件系统类型表固定 8 条。</li>
            <li>调度器上下文切换只保存通用寄存器 + RIP/RFLAGS/RSP/CR3，不保存 FPU/SSE 状态。</li>
            <li>每个内核线程固定 8 KiB 栈，没有栈溢出检测。</li>
            <li>自旋锁默认不关中断；如需在 IRQ 上下文使用，调用方必须显式 cli/sti。</li>
            <li>互斥锁的优先级继承是单跳（直接持有者提升），暂不支持传递性 PI 链。</li>
            <li>仅支持静态 ELF64 (ET_EXEC)，不支持动态链接 / PIE / PT_INTERP。</li>
            <li>不支持 fork()/exec()，只支持"从 ELF 直接派生"一种进程创建方式。</li>
            <li>用户进程尚无信号机制（SIGINT/SIGSEGV）。ring-3 缺页会直接终止进程并打印诊断。</li>
            <li>syscall 表是全局的，所有用户进程共享同一组处理函数。</li>
            <li>单 CPU（仅 BSP），SMP 支持远期计划。</li>
            <li>WP-01 / WP-02 / WP-03 / WP-04 / WP-05 / WP-06 的全部 26 个扩展接口完全保持兼容。</li>
          </ul>
        </section>

        <section className="my-10 p-6 rounded-xl bg-[#0f0f17] border border-[#1f1f2a]">
          <h2 className="text-xl font-semibold mb-2">启动命令</h2>
          <p className="text-sm text-[#a0a0b0] mb-4">在 QEMU 中启动 Open Cube OS（挂载磁盘映像以验证 FAT32 读写）：</p>
          <pre className="text-xs font-mono text-[#a0e0e0] bg-[#0a0a0f] p-4 rounded-lg overflow-x-auto"><code>qemu-system-x86_64 -m 256M -cdrom opencube-wp08a.iso -boot d \
  -serial stdio -vga std -netdev user,id=n0 -device e1000,netdev=n0 \
  -drive file=disk.img,format=raw,if=ide,index=0</code></pre>
          <p className="text-xs text-[#707080] mt-3">启动后在 oc&gt; 提示符输入命令：<code className="text-[#80f0f0]">lsblk</code> / <code className="text-[#80f0f0]">fatmount ata0 /mnt</code> / <code className="text-[#80f0f0]">ls /mnt</code> / <code className="text-[#80f0f0]">cat /mnt/HELLO.TXT</code> / <code className="text-[#80f0f0]">echo test &gt; /mnt/test.txt</code> / <code className="text-[#80f0f0]">sync</code></p>
          <p className="text-xs text-[#707080] mt-2">需要键盘输入时加 <code className="text-[#80f0f0]">-display gtk</code>（打开窗口捕获键盘）；上面的 <code className="text-[#80f0f0]">-serial stdio</code> 同时支持串口输入。键盘（IRQ1）与串口（IRQ4）共用同一个输入队列。</p>
          <p className="text-xs text-[#707080] mt-2">源码 Makefile 提供 <code className="text-[#80f0f0]">make run-bios</code>（无显示，仅串口）/ <code className="text-[#80f0f0]">make run-bios-gui</code>（GTK 窗口 + 键盘）/ <code className="text-[#80f0f0]">make run-uefi</code> / <code className="text-[#80f0f0]">make run-uefi-gui</code> 四种启动方式。</p>
        </section>
      </main>

      <footer className="border-t border-[#1f1f2a] bg-[#0d0d14] mt-12">
        <div className="max-w-5xl mx-auto px-6 py-6 flex flex-col md:flex-row items-center justify-between gap-3">
          <div className="text-xs text-[#707080]">Copyright 2026 Open Cube OS by cubestudio · Apache License 2.0</div>
          <div className="text-xs text-[#707080]">WP-08a · 完整系统调用集</div>
        </div>
      </footer>
    </div>
  );
}
