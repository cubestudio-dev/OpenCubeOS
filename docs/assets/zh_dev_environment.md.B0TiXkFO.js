import{_ as a,o as e,c as n,a2 as i}from"./chunks/framework.BS0bAV7E.js";const u=JSON.parse('{"title":"开发环境","description":"","frontmatter":{},"headers":[],"relativePath":"zh/dev/environment.md","filePath":"zh/dev/environment.md"}'),o={name:"zh/dev/environment.md"};function p(l,s,t,c,d,r){return e(),n("div",null,[...s[0]||(s[0]=[i(`<h1 id="开发环境" tabindex="-1">开发环境 <a class="header-anchor" href="#开发环境" aria-label="Permalink to &quot;开发环境&quot;">​</a></h1><p>如何从一台全新机器开始进行内核开发。</p><h2 id="必要条件" tabindex="-1">必要条件 <a class="header-anchor" href="#必要条件" aria-label="Permalink to &quot;必要条件&quot;">​</a></h2><ul><li>Linux 主机（已测试 Debian/Ubuntu；安装了以下任何包的发行版均可）。Windows：WSL2 可提供完全相同的环境。</li><li><code>gcc</code>（测试 14.x）、<code>binutils</code>（ld）、<code>nasm</code> 2.16+、<code>xorriso</code>、<code>mtools</code>、<code>grub-common</code> + <code>grub-pc-bin</code> + <code>grub-efi-amd64-bin</code>、<code>python3</code>（3.9+）、<code>zip</code>。</li><li>用于测试：<code>qemu-system-x86</code>、<code>ovmf</code>、<code>seabios</code>、<code>ipxe-qemu</code>。</li><li>用于端到端测试套件：Python <code>pexpect</code>（回归测试、看门狗 E2E）和 <code>paramiko</code>（SSH E2E）。</li></ul><p><code>tools/build.sh</code> 会验证构建工具集并为缺失项打印确切的 Debian 包名；当你以 root 身份运行时，<code>./build.sh --install</code> 会安装这些包。无 root 权限的替代方案是 <code>tools/install-toolchain.sh</code>（解压一个可重定位的工具树并导出 <code>OC_TOOLS</code>）。</p><h2 id="获取源代码树" tabindex="-1">获取源代码树 <a class="header-anchor" href="#获取源代码树" aria-label="Permalink to &quot;获取源代码树&quot;">​</a></h2><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>git clone https://github.com/cubestudio-dev/OpenCubeOS.git</span></span>
<span class="line"><span>cd OpenCubeOS</span></span></code></pre></div><h2 id="基础构建验证" tabindex="-1">基础构建验证 <a class="header-anchor" href="#基础构建验证" aria-label="Permalink to &quot;基础构建验证&quot;">​</a></h2><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>./build.sh iso        # kernel + ISO</span></span>
<span class="line"><span>make test             # host unit tests</span></span>
<span class="line"><span>make run-bios         # boot it</span></span></code></pre></div><p>如果这三项全部通过，你的环境就配置正确了。完整的变更标准请参见 <a href="./../root/CONTRIBUTING.html">CONTRIBUTING.md</a>。</p><h2 id="仓库布局" tabindex="-1">仓库布局 <a class="header-anchor" href="#仓库布局" aria-label="Permalink to &quot;仓库布局&quot;">​</a></h2><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>kernel/    core (sched/syscalls/...), mem (PMM/VMM/heap), lib, crypto, ota</span></span>
<span class="line"><span>boot/      boot.S - the 32-&gt;64-bit entry</span></span>
<span class="line"><span>arch/      arm64/ riscv/ cross-compilation bring-up shells</span></span>
<span class="line"><span>drivers/   16 families: block, nic, usb, snd, input, display, pci,</span></span>
<span class="line"><span>           acpi, i2c, time, wireless, bluetooth, sd, camera, misc, scsi</span></span>
<span class="line"><span>fs/        VFS + ramfs + fat32 + exfat + ext4</span></span>
<span class="line"><span>net/       net_core (stack), ssh, sshd, tls, tcp_cc</span></span>
<span class="line"><span>shell/     the oc&gt; shell + all command modules</span></span>
<span class="line"><span>l1/        the L1 extension surface headers + solib support</span></span>
<span class="line"><span>tui/       the terminal UI engine and apps</span></span>
<span class="line"><span>chinese/   CN console layer (font/encoding/IME)</span></span>
<span class="line"><span>libs/      ld_so.c - the in-tree shared-object loader</span></span>
<span class="line"><span>userprogs/ ring-3 test programs (fork/exec/pipe/... )</span></span>
<span class="line"><span>tools/     build + test + docs generators</span></span>
<span class="line"><span>tests/     host-side unit tests (make test)</span></span>
<span class="line"><span>docs/      this documentation tree</span></span></code></pre></div><h2 id="重要约定" tabindex="-1">重要约定 <a class="header-anchor" href="#重要约定" aria-label="Permalink to &quot;重要约定&quot;">​</a></h2><ul><li><code>-Wall -Wextra -Werror</code> - 零警告，无例外。</li><li>每个文件都带有 SPDX 头。</li><li>内核可见的字符串为 ASCII 编码。</li><li>命令接口只增不减（参见 CONTRIBUTING）。</li></ul><h2 id="qemu-使用技巧" tabindex="-1">QEMU 使用技巧 <a class="header-anchor" href="#qemu-使用技巧" aria-label="Permalink to &quot;QEMU 使用技巧&quot;">​</a></h2><p>记住这些单行命令：</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span># quick BIOS boot, serial on stdio</span></span>
<span class="line"><span>qemu-system-x86_64 -m 512 -cdrom build/opencube.iso \\</span></span>
<span class="line"><span>    -netdev user,id=n1 -device e1000,netdev=n1 -serial mon:stdio</span></span>
<span class="line"><span></span></span>
<span class="line"><span># UEFI</span></span>
<span class="line"><span>qemu-system-x86_64 -m 512 -machine q35 -bios /usr/share/ovmf/OVMF.fd \\</span></span>
<span class="line"><span>    -cdrom build/opencube.iso -display none -serial mon:stdio</span></span>
<span class="line"><span></span></span>
<span class="line"><span># with a scratch disk + TUI visible</span></span>
<span class="line"><span>qemu-system-x86_64 -m 512 -cdrom build/opencube.iso \\</span></span>
<span class="line"><span>    -drive file=scratch.qcow2,if=ide -netdev user,id=n1 \\</span></span>
<span class="line"><span>    -device e1000,netdev=n1</span></span>
<span class="line"><span></span></span>
<span class="line"><span># boot log to a file</span></span>
<span class="line"><span>... -serial file:boot.log</span></span></code></pre></div><p><code>make run-bios</code>、<code>make run-uefi</code>、<code>make run-bios-gui</code>、<code>make shot-bios</code> 封装了常用的启动方式（包括 /etc 卷设置）。</p>`,18)])])}const m=a(o,[["render",p]]);export{u as __pageData,m as default};
