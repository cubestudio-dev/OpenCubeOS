'use client';

import { useState } from 'react';

// WP-09 official release assets (GitHub Release WP-09, SHA256 verified 3-way:
// local build == GitHub asset == website /downloads/ copy)
const ISO_SHA256 = '5e94d00d87f53a38ecb3dfdd31a8885205797876bf0b8714a152438e24491d85';
const ISO_SIZE_BYTES = 10760192;
const ISO_SIZE_MB = (ISO_SIZE_BYTES / (1024 * 1024)).toFixed(2);
const SRC_ZIP_SHA256 = '19109ba9ac2cc7d8983df65975e0ec4a3d58bf2e5c605e7485097c0d00285f49';
const SRC_ZIP_SIZE_BYTES = 1688019;
const SRC_ZIP_SIZE_MB = (SRC_ZIP_SIZE_BYTES / (1024 * 1024)).toFixed(2);

// GitHub links
const GITHUB_REPO = 'https://github.com/cubestudio-dev/OpenCubeOS';
const GITHUB_RELEASES = 'https://github.com/cubestudio-dev/OpenCubeOS/releases';
const GITHUB_RELEASE_LATEST = 'https://github.com/cubestudio-dev/OpenCubeOS/releases/tag/WP-09';
const GITHUB_ISO_URL = 'https://github.com/cubestudio-dev/OpenCubeOS/releases/download/WP-09/opencube-wp09.iso';

// WP-08a archive (downloaded from GitHub Release)
const WP08A_ISO_SHA256 = 'c42861749aeeafe4e00176c6ba09932b5978db410c2eb8276fdd7b80fa9d4ebc';
const WP08A_ISO_SIZE_MB = (10510336 / (1024 * 1024)).toFixed(2);
const WP08A_SRC_SHA256 = '5b79b0aaa4716d121c9ebd2d996c7ee36820237b19522fa5d6ba320f552b5c99';
const WP08A_SRC_SIZE_MB = (3675487 / (1024 * 1024)).toFixed(2);

// WP-08b archive (downloaded from GitHub Release)
const WP08B_ISO_SHA256 = '90eb8d80ddfc042ce4560d02d1b01e35d2ce9ffdf130d646415cdcf35a70b31a';
const WP08B_ISO_SIZE_MB = (10635264 / (1024 * 1024)).toFixed(2);
const WP08B_SRC_SHA256 = '58b58c5c2c6ca830cfeb9a57fee45c0af2e0ec6d9b379b1ce308c13865c45336';
const WP08B_SRC_SIZE_MB = (382440 / (1024 * 1024)).toFixed(2);

const CORE_FEATURES = [
  { title: 'L0 内核', desc: '完整 x86_64 内核 + 用户态恢复/管理 Shell，定位 L0。', icon: '◈' },
  { title: '二级架构', desc: 'L0 = Open Cube OS（内核 + 基础 Shell），L1 = 上层扩展（L0 不自带）。', icon: '◺' },
  { title: '一切皆可扩展', desc: '每个功能必须有扩展接口，每个接口必须有文档、默认实现、示例。', icon: '✦' },
  { title: 'Apache 2.0', desc: '开源协议，允许商用、修改、分发，只需保留版权声明。', icon: 'Ⓐ' },
];

const WORK_PACKAGES = [
  { wp: 'WP-01', title: '引导 + framebuffer + 文本渲染' },
  { wp: 'WP-02', title: '中断 + 定时器 + 键盘' },
  { wp: 'WP-03', title: '物理内存 + 虚拟内存 + 内核堆' },
  { wp: 'WP-04', title: '调度器 + 同步原语 + 用户态' },
  { wp: 'WP-05', title: 'Shell 增强 + 文件系统' },
  { wp: 'WP-06', title: '网络协议栈' },
  { wp: 'WP-07', title: '磁盘完整支持' },
  { wp: 'WP-08', title: '完整 syscall + 动态链接 + 用户态 Shell + 工具集 + Audit 全修复' },
  { wp: 'WP-09', title: '安全传输层：SSH 客户端/服务端 + TLS 1.2/HTTPS + crypto 核心' },
];

export default function Home() {
  const [copied, setCopied] = useState<string | null>(null);

  const copySha = (sha: string, label: string) => {
    navigator.clipboard.writeText(sha).then(() => {
      setCopied(label);
      setTimeout(() => setCopied(null), 2000);
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
              <div className="text-xs text-[#707080]">开源 x86_64 L0 内核 · Apache 2.0</div>
            </div>
          </div>
          <div className="flex items-center gap-3">
            <a href={GITHUB_REPO} target="_blank" rel="noopener noreferrer" className="text-xs px-3 py-1.5 rounded border border-[#2a2a35] hover:border-[#00e0d0] hover:text-[#00e0d0] transition">GitHub</a>
            <a href="https://www.apache.org/licenses/LICENSE-2.0" target="_blank" rel="noopener noreferrer" className="text-xs px-3 py-1.5 rounded border border-[#2a2a35] hover:border-[#00e0d0] hover:text-[#00e0d0] transition">Apache 2.0</a>
          </div>
        </div>
      </header>

      <main className="flex-1 max-w-5xl mx-auto w-full px-6 py-10">
        {/* Hero */}
        <section className="text-center py-12">
          <div className="inline-block px-3 py-1 mb-4 text-xs rounded-full border border-[#00e0d0]/40 text-[#00e0d0]">最新版本：WP-09</div>
          <h1 className="text-4xl md:text-5xl font-bold mb-4 bg-gradient-to-r from-[#00e0d0] via-[#80f0f0] to-[#0070c0] bg-clip-text text-transparent">Open Cube OS</h1>
          <p className="text-base text-[#a0a0b0] max-w-2xl mx-auto">
            一个能被扩展成任何东西的开源 x86_64 内核。L0 完整内核 + 用户态恢复 Shell，BIOS + UEFI 双引导，57 个 L1 扩展接口，37 个系统调用。WP-09 新增安全传输层：SSH 客户端/服务端（group14-sha256）+ TLS 1.2/HTTPS + crypto 核心（AES/SHA/HMAC/DH）。18/18 回归 + dhtest 5/5 + HTTPS E2E + SSH 双向（K 字节级一致）全部真实通过。
          </p>
          <div className="mt-6 flex items-center justify-center gap-6 md:gap-10 flex-wrap text-sm">
            <div className="flex items-baseline gap-1.5">
              <span className="text-2xl font-bold text-[#00e0d0] font-mono">46058</span>
              <span className="text-xs text-[#707080]">行源代码（不含文档）</span>
            </div>
            <div className="flex items-baseline gap-1.5">
              <span className="text-2xl font-bold text-[#00e0d0] font-mono">30062</span>
              <span className="text-xs text-[#707080]">行（含文档）</span>
            </div>
            <div className="flex items-baseline gap-1.5">
              <span className="text-2xl font-bold text-[#00e0d0] font-mono">9</span>
              <span className="text-xs text-[#707080]">个工作包 (WP-01 ~ WP-09)</span>
            </div>
            <div className="flex items-baseline gap-1.5">
              <span className="text-2xl font-bold text-[#00e0d0] font-mono">57</span>
              <span className="text-xs text-[#707080]">个 L1 扩展接口</span>
            </div>
            <div className="flex items-baseline gap-1.5">
              <span className="text-2xl font-bold text-[#00e0d0] font-mono">37</span>
              <span className="text-xs text-[#707080]">个系统调用</span>
            </div>
          </div>
        </section>

        {/* Downloads */}
        <section className="bg-[#0f0f17] border border-[#1f1f2a] rounded-xl p-6 md:p-8 my-8">
          <h2 className="text-xl font-semibold mb-1">下载</h2>
          <p className="text-sm text-[#707080] mb-6">三层下载渠道，任选其一。最新版 WP-09 含 ISO + 源码包，SHA256 三方核对。</p>

          {/* Layer 1: 本站直接下载 */}
          <div className="mb-6">
            <h3 className="text-sm font-semibold mb-3 text-[#a0a0b0]">第一层：本站直接下载</h3>
            <div className="space-y-3">
              <div className="border border-[#1f1f2a] rounded-lg p-4 bg-[#0a0a0f] hover:border-[#00e0d0]/40 transition">
                <div className="flex items-center justify-between flex-wrap gap-3">
                  <div>
                    <div className="font-semibold text-sm flex items-center gap-2">
                      <span className="text-[#00e0d0]">⬇</span>
                      opencube-wp09.iso
                    </div>
                    <div className="text-xs text-[#707080] mt-1">BIOS + UEFI · {ISO_SIZE_MB} MB · 沙箱重建</div>
                  </div>
                  <a href="/OpenCubeOS/downloads/opencube-wp09.iso" className="text-sm px-4 py-2 rounded bg-[#00e0d0] text-[#0a0a0f] font-semibold hover:bg-[#80f0f0] transition shrink-0">下载 ISO</a>
                </div>
                <div className="mt-2 flex items-center gap-2 text-xs">
                  <span className="text-[#707080] shrink-0">SHA256:</span>
                  <code className="font-mono text-[#80f0f0] break-all">{ISO_SHA256}</code>
                  <button onClick={() => copySha(ISO_SHA256, 'iso1')} className="text-xs px-2 py-0.5 rounded border border-[#2a2a35] hover:border-[#00e0d0] hover:text-[#00e0d0] transition shrink-0">{copied === 'iso1' ? '已复制' : '复制'}</button>
                </div>
              </div>
              <div className="border border-[#1f1f2a] rounded-lg p-4 bg-[#0a0a0f] hover:border-[#00e0d0]/40 transition">
                <div className="flex items-center justify-between flex-wrap gap-3">
                  <div>
                    <div className="font-semibold text-sm flex items-center gap-2">
                      <span className="text-[#00e0d0]">⬇</span>
                      opencube-wp09-src.zip
                    </div>
                    <div className="text-xs text-[#707080] mt-1">源码包 · {SRC_ZIP_SIZE_MB} MB · 沙箱重建</div>
                  </div>
                  <a href="/OpenCubeOS/downloads/opencube-wp09-src.zip" className="text-sm px-4 py-2 rounded bg-[#00e0d0] text-[#0a0a0f] font-semibold hover:bg-[#80f0f0] transition shrink-0">下载 src</a>
                </div>
                <div className="mt-2 flex items-center gap-2 text-xs">
                  <span className="text-[#707080] shrink-0">SHA256:</span>
                  <code className="font-mono text-[#80f0f0] break-all">{SRC_ZIP_SHA256}</code>
                  <button onClick={() => copySha(SRC_ZIP_SHA256, 'src1')} className="text-xs px-2 py-0.5 rounded border border-[#2a2a35] hover:border-[#00e0d0] hover:text-[#00e0d0] transition shrink-0">{copied === 'src1' ? '已复制' : '复制'}</button>
                </div>
              </div>
            </div>
          </div>

          {/* Layer 2: GitHub Release */}
          <div className="mb-6 pt-4 border-t border-[#1f1f2a]">
            <h3 className="text-sm font-semibold mb-3 text-[#a0a0b0]">第二层：GitHub Release</h3>
            <div className="space-y-3">
              <a href={GITHUB_ISO_URL} target="_blank" rel="noopener noreferrer" className="text-xs px-3 py-2 rounded border border-[#2a2a35] hover:border-[#00e0d0]/40 hover:bg-[#0f0f17] transition flex items-center justify-between">
                <span>GitHub: opencube-wp09.iso</span>
                <span className="text-[#00e0d0]">下载</span>
              </a>
            </div>
            <div className="mt-2 text-xs text-[#707080]">按策略，最新版只放 ISO；源码请从第三层 GitHub 仓库或第一层本站下载。</div>
            <div className="mt-1">
              <a href={GITHUB_RELEASE_LATEST} target="_blank" rel="noopener noreferrer" className="text-xs text-[#707080] hover:text-[#00e0d0] transition">查看 Release 页面 →</a>
            </div>
          </div>

          {/* Layer 3: GitHub 仓库 */}
          <div className="pt-4 border-t border-[#1f1f2a]">
            <h3 className="text-sm font-semibold mb-3 text-[#a0a0b0]">第三层：GitHub 仓库</h3>
            <a href={GITHUB_REPO} target="_blank" rel="noopener noreferrer" className="text-xs px-3 py-2 rounded border border-[#2a2a35] hover:border-[#00e0d0]/40 hover:bg-[#0f0f17] transition flex items-center justify-between">
              <span>github.com/cubestudio-dev/OpenCubeOS</span>
              <span className="text-[#00e0d0]">查看源码 →</span>
            </a>
          </div>

          {/* WP-08b archive */}
          <div className="mt-6 pt-6 border-t border-[#1f1f2a]">
            <h3 className="text-sm font-semibold mb-3 text-[#a0a0b0]">归档：WP-08b（含 47 个 bug 审计全修复的稳定快照）</h3>
            <div className="grid grid-cols-1 sm:grid-cols-2 gap-3">
              <a href="/OpenCubeOS/downloads/opencube-wp08b.iso" className="text-xs px-3 py-2 rounded border border-[#2a2a35] hover:border-[#00e0d0]/40 hover:bg-[#0f0f17] transition flex items-center justify-between">
                <span>本站: opencube-wp08b.iso ({WP08B_ISO_SIZE_MB} MB)</span>
                <span className="text-[#00e0d0]">下载</span>
              </a>
              <a href="https://github.com/cubestudio-dev/OpenCubeOS/releases/download/WP-08b/opencube-wp08b.iso" target="_blank" rel="noopener noreferrer" className="text-xs px-3 py-2 rounded border border-[#2a2a35] hover:border-[#00e0d0]/40 hover:bg-[#0f0f17] transition flex items-center justify-between">
                <span>GitHub: opencube-wp08b.iso ({WP08B_ISO_SIZE_MB} MB)</span>
                <span className="text-[#00e0d0]">下载</span>
              </a>
              <a href="/OpenCubeOS/downloads/opencube-wp08b-src.zip" className="text-xs px-3 py-2 rounded border border-[#2a2a35] hover:border-[#00e0d0]/40 hover:bg-[#0f0f17] transition flex items-center justify-between">
                <span>本站: opencube-wp08b-src.zip ({WP08B_SRC_SIZE_MB} MB)</span>
                <span className="text-[#00e0d0]">下载</span>
              </a>
              <a href="https://github.com/cubestudio-dev/OpenCubeOS/releases/download/WP-08b/opencube-wp08b-src.zip" target="_blank" rel="noopener noreferrer" className="text-xs px-3 py-2 rounded border border-[#2a2a35] hover:border-[#00e0d0]/40 hover:bg-[#0f0f17] transition flex items-center justify-between">
                <span>GitHub: opencube-wp08b-src.zip ({WP08B_SRC_SIZE_MB} MB)</span>
                <span className="text-[#00e0d0]">下载</span>
              </a>
            </div>
            <div className="mt-3 text-xs text-[#707080] space-y-1">
              <div>WP-08b ISO SHA256: <code className="font-mono text-[#80f0f0]">{WP08B_ISO_SHA256}</code></div>
              <div>WP-08b src zip SHA256: <code className="font-mono text-[#80f0f0]">{WP08B_SRC_SHA256}</code></div>
            </div>
          </div>

          {/* WP-08a archive */}
          <div className="mt-6 pt-6 border-t border-[#1f1f2a]">
            <h3 className="text-sm font-semibold mb-3 text-[#a0a0b0]">归档：WP-08a（早期稳定版：完整 syscall 集 + 7 个用户态测试）</h3>
            <div className="grid grid-cols-1 sm:grid-cols-2 gap-3">
              <a href="/OpenCubeOS/downloads/opencube-wp08a.iso" className="text-xs px-3 py-2 rounded border border-[#2a2a35] hover:border-[#00e0d0]/40 hover:bg-[#0f0f17] transition flex items-center justify-between">
                <span>本站: opencube-wp08a.iso ({WP08A_ISO_SIZE_MB} MB)</span>
                <span className="text-[#00e0d0]">下载</span>
              </a>
              <a href="https://github.com/cubestudio-dev/OpenCubeOS/releases/download/WP-08a/opencube-wp08a.iso" target="_blank" rel="noopener noreferrer" className="text-xs px-3 py-2 rounded border border-[#2a2a35] hover:border-[#00e0d0]/40 hover:bg-[#0f0f17] transition flex items-center justify-between">
                <span>GitHub: opencube-wp08a.iso ({WP08A_ISO_SIZE_MB} MB)</span>
                <span className="text-[#00e0d0]">下载</span>
              </a>
              <a href="/OpenCubeOS/downloads/opencube-wp08a-src.zip" className="text-xs px-3 py-2 rounded border border-[#2a2a35] hover:border-[#00e0d0]/40 hover:bg-[#0f0f17] transition flex items-center justify-between">
                <span>本站: opencube-wp08a-src.zip ({WP08A_SRC_SIZE_MB} MB)</span>
                <span className="text-[#00e0d0]">下载</span>
              </a>
              <a href="https://github.com/cubestudio-dev/OpenCubeOS/releases/download/WP-08a/opencube-wp08a-src.zip" target="_blank" rel="noopener noreferrer" className="text-xs px-3 py-2 rounded border border-[#2a2a35] hover:border-[#00e0d0]/40 hover:bg-[#0f0f17] transition flex items-center justify-between">
                <span>GitHub: opencube-wp08a-src.zip ({WP08A_SRC_SIZE_MB} MB)</span>
                <span className="text-[#00e0d0]">下载</span>
              </a>
            </div>
            <div className="mt-3">
              <a href={GITHUB_RELEASES} target="_blank" rel="noopener noreferrer" className="text-xs text-[#707080] hover:text-[#00e0d0] transition">GitHub Releases 页面 →</a>
            </div>
            <div className="mt-3 text-xs text-[#707080] space-y-1">
              <div>WP-08a ISO SHA256: <code className="font-mono text-[#80f0f0]">{WP08A_ISO_SHA256}</code></div>
              <div>WP-08a src zip SHA256: <code className="font-mono text-[#80f0f0]">{WP08A_SRC_SHA256}</code></div>
            </div>
          </div>
        </section>

        {/* Core features */}
        <section className="bg-[#0f0f17] border border-[#1f1f2a] rounded-xl p-6 md:p-8 my-8">
          <h2 className="text-xl font-semibold mb-1">核心特性</h2>
          <p className="text-sm text-[#707080] mb-6">四个核心设计原则定义了 Open Cube OS。</p>
          <div className="grid grid-cols-1 sm:grid-cols-2 gap-4">
            {CORE_FEATURES.map(f => (
              <div key={f.title} className="border border-[#1f1f2a] rounded-lg p-4 bg-[#0a0a0f]">
                <div className="flex items-center gap-3 mb-2">
                  <span className="text-2xl text-[#00e0d0]">{f.icon}</span>
                  <span className="font-semibold text-sm">{f.title}</span>
                </div>
                <p className="text-xs text-[#909098] leading-relaxed">{f.desc}</p>
              </div>
            ))}
          </div>
        </section>

        {/* Work packages */}
        <section className="bg-[#0f0f17] border border-[#1f1f2a] rounded-xl p-6 md:p-8 my-8">
          <h2 className="text-xl font-semibold mb-1">已完成的工作包</h2>
          <p className="text-sm text-[#707080] mb-6">从 WP-01 到 WP-09，每个 WP 只做一件事。</p>
          <div className="grid grid-cols-1 md:grid-cols-2 gap-2">
            {WORK_PACKAGES.map(w => (
              <div key={w.wp} className="flex items-center gap-3 py-2 px-3 border border-[#1a1a24] rounded bg-[#0a0a0f]">
                <span className="text-xs px-2 py-0.5 rounded bg-[#00e0d0]/10 text-[#00e0d0] font-mono shrink-0">{w.wp}</span>
                <span className="text-sm text-[#c0c0c8]">{w.title}</span>
              </div>
            ))}
          </div>
        </section>

        {/* Quick start */}
        <section className="bg-[#0f0f17] border border-[#1f1f2a] rounded-xl p-6 md:p-8 my-8">
          <h2 className="text-xl font-semibold mb-1">快速启动</h2>
          <p className="text-sm text-[#707080] mb-4">下载 ISO 后用 QEMU 启动。BIOS + UEFI 双引导，37 个系统调用 + 15 个用户态测试程序 + 用户态 Shell (ush)。</p>
          <pre className="bg-[#0a0a0f] border border-[#1f1f2a] rounded-lg p-4 text-xs font-mono text-[#80f0f0] overflow-x-auto">
{`qemu-system-x86_64 \\
  -m 256M -cdrom opencube-wp09.iso \\
  -boot d -no-reboot -display none \\
  -serial stdio -monitor none -vga std -snapshot

oc> run hello
oc> run ush         # launch user-space shell (ush)
ush> uname         # → Open Cube OS WP-09 x86_64
oc> help`}
          </pre>
        </section>
      </main>

      <footer className="border-t border-[#1f1f2a] bg-[#0d0d14] mt-auto">
        <div className="max-w-5xl mx-auto px-6 py-4 flex items-center justify-between text-xs text-[#707080] flex-wrap gap-2">
          <div>
            <span className="text-[#a0a0b0]">Open Cube OS</span> · WP-09 · Apache 2.0 · cubestudio-dev
          </div>
          <div className="flex items-center gap-3">
            <a href={GITHUB_REPO} target="_blank" rel="noopener noreferrer" className="hover:text-[#00e0d0] transition">GitHub</a>
            <span>·</span>
            <a href={GITHUB_RELEASES} target="_blank" rel="noopener noreferrer" className="hover:text-[#00e0d0] transition">Releases</a>
            <span>·</span>
            <a href="/OpenCubeOS/downloads/opencube-wp09.iso" className="hover:text-[#00e0d0] transition">下载 ISO</a>
            <span>·</span>
            <a href="https://www.apache.org/licenses/LICENSE-2.0" target="_blank" rel="noopener noreferrer" className="hover:text-[#00e0d0] transition">License</a>
          </div>
        </div>
      </footer>
    </div>
  );
}
