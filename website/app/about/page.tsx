import type { Metadata } from "next";
import { BASE, GITHUB_REPO, RELEASES } from "@/lib/site";

export const metadata: Metadata = {
  title: "关于 - Open Cube OS",
  description:
    "Open Cube OS 项目历史：WP-01 到 WP-09 九个工作包、120 个审计修复、18/18 回归验证、AI 披露与许可证。",
};

const TIMELINE = [
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
    title: "安全传输：SSH + TLS 1.2 / HTTPS + crypto 核心",
    desc: "crypto 核心（AES-128、SHA-256、HMAC-SHA256、任意长度 DH modexp、crypto_random）；SSH 客户端 + 服务端（group14-sha256、aes128-cbc、hmac-sha2-256、rsa-sha2-256、密码认证、session exec，与 paramiko 双向互操作、K 字节级一致）；TLS 1.2 客户端（DHE_RSA_WITH_AES_128_CBC_SHA256、RFC 3526 1024-bit MODP、记录层双向加解密）；HTTPS 下载入 VFS；route/arp/firewall/tcpstats/dns 运维命令；TCP 选项（MSS、Window Scale、SACK-Permitted、Timestamps）。",
  },
];

export default function AboutPage() {
  return (
    <div className="container">
      <div className="page-head">
        <h1>关于</h1>
        <p>
          Open Cube OS 的定位、历史与工程方法。所有数字来自仓库公开文档
          （README.md、docs/VERIFICATION_BATCH_B.md、docs/WORK_LOG.md）。
        </p>
      </div>

      <div className="about-card">
        <h2>定位</h2>
        <div className="quote">
          It is not &quot;a system you can use daily&quot;. It is &quot;a
          kernel that can be extended into anything&quot;. — README.md
        </div>
        <p>
          Open Cube OS 是一个开源操作系统内核，两层架构：
          <b>L0</b> = 完整内核（本项目，Apache 2.0）；<b>L1</b> =
          上层扩展，构建在 L0 暴露的 57 个扩展接口之上，L0 不内置任何 L1。
          设计原则：一切皆可扩展——每个功能都有扩展接口，每个接口都有文档、默认实现与示例。
        </p>
      </div>

      <div className="about-card">
        <h2>项目历史（九个工作包）</h2>
        <div className="timeline">
          {TIMELINE.map((t) => (
            <div className="tl-item" key={t.no}>
              <h3>
                <span className="wp-no">{t.no}</span>
                {t.title}
              </h3>
              <p>{t.desc}</p>
            </div>
          ))}
        </div>
      </div>

      <div className="about-card">
        <h2>工程方法</h2>
        <p>
          每个工作包以真实可复现的证据收尾：截至 WP-08 累计修复 120 个审计
          bug；WP-09 又完成 SSH/TLS 专项修复。最终验证为 18/18 QEMU
          全量回归 + dhtest 5/5 + HTTPS 双侧 E2E + SSH 双向互操作
          （paramiko K 字节级一致）。完整证据与原始日志：
          <a
            href={`${GITHUB_REPO}/blob/main/docs/VERIFICATION_BATCH_B.md`}
            target="_blank"
            rel="noopener noreferrer"
          >
            docs/VERIFICATION_BATCH_B.md
          </a>{" "}
          与 docs/verification/。历史记录：
          <a
            href={`${GITHUB_REPO}/blob/main/docs/WORK_LOG.md`}
            target="_blank"
            rel="noopener noreferrer"
          >
            docs/WORK_LOG.md
          </a>
          。
        </p>
      </div>

      <div className="about-card">
        <h2>AI 披露</h2>
        <p>
          本项目由 cubestudio-dev 在 AI 工具协助下开发。全部设计决策、架构、
          规格、项目管理、代码评审、质量保证与验收测试由 cubestudio-dev
          完成；AI 工具仅作为实现辅助。
        </p>
      </div>

      <div className="about-card">
        <h2>许可与联系</h2>
        <p>
          Apache License 2.0（
          <a
            href={`${GITHUB_REPO}/blob/main/LICENSE`}
            target="_blank"
            rel="noopener noreferrer"
          >
            LICENSE
          </a>{" "}
          ·{" "}
          <a
            href={`${GITHUB_REPO}/blob/main/NOTICE`}
            target="_blank"
            rel="noopener noreferrer"
          >
            NOTICE
          </a>
          ）。
        </p>
        <p>
          Copyright 2026 cubestudio-dev &lt;cubestudio@qq.com&gt;。
          仓库：
          <a href={GITHUB_REPO} target="_blank" rel="noopener noreferrer">
            github.com/cubestudio-dev/OpenCubeOS
          </a>
          ，历史版本见{" "}
          <a href={RELEASES} target="_blank" rel="noopener noreferrer">
            Releases
          </a>
          。
        </p>
        <p>
          <a href={`${BASE}/`} className="btn">
            ← 返回首页
          </a>
        </p>
      </div>
    </div>
  );
}
