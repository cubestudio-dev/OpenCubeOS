// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 cubestudio-dev <cubestudio@qq.com>
//
// UI chrome copy for both locales. zh strings are taken verbatim from the
// previous single-locale site (app/layout.tsx, app/docs/page.tsx,
// app/about/page.tsx); en strings are their translations. Page data lives
// in lib/home.ts / lib/about.ts; document translations in lib/docs-zh.ts.

import { BASE } from "@/lib/site";

export type Locale = "zh" | "en";

export function isLocale(v: string): v is Locale {
  return v === "zh" || v === "en";
}

export function localePath(locale: Locale, sub: "" | "docs" | "about" = ""): string {
  return sub ? `${BASE}/${locale}/${sub}/` : `${BASE}/${locale}/`;
}

const zh = {
  htmlLang: "zh-CN",
  badge: "WP-10-AUDIT_P2-fix2b · Apache 2.0",
  brandSub: "WP-10-AUDIT_P2-fix2b · Apache 2.0",
  navAria: "站点导航",
  navHome: "首页",
  navDownloads: "下载",
  navDocs: "文档",
  navAbout: "关于",
  navGithub: "GitHub",
  langAria: "切换语言",
  heroTitleLine1: "Open Cube OS",
  heroTitleLine2: "开源 x86_64 L0 内核",
  heroSubPre: "它不是",
  heroSubQuoted: "\u201c一个能日常使用的系统\u201d",
  heroSubMid: "，而是",
  heroSubBold: "\u201c一个能被扩展成任何东西的内核\u201d",
  heroSubPost:
    "。价值不在自带什么，而在向上层暴露的接口：L0 = 完整内核，L1 = 上层扩展，L0 不内置任何 L1。WP-AUDIT-01 完成 18 路逐行完整审查（607 条发现），41 个 P0 已全部修复（p0fix1 前 20 个：文件系统、网络栈与 SSH/sshd 的预认证内存破坏；p0fix2 后 21 个：TLS 记录/消息边界与 TLS 1.3 CertVerify DER、shell glob/fsck/mv、sys_poll 回绕与 SYS_EXIT 缓冲、execve CR3 次序、sys_ps ktab、X509 DER 溢出、config 写边界、OTA changes/request/路径遍历、NVMe 非 512 LBA 拒绝、嵌套 #PF 防护），逐条有验证证据（QEMU 复现修复前 FAIL / 修复后 PASS、宿主 ASAN 或构造级路径证据）；WP-AUDIT-01-p1fix2 修复前 31 个 P1（BUG-0042..0072）与第 32~62 条（BUG-0073..0103）：每任务 FPU/SSE fxsave 上下文 + CR4.OSFXSR + fork 继承、PMM 位图 cli 原子性、页错误语义（P=1 拒绝、8MiB 栈下限、U/S 环、内核在用户地址空间拒绝）、XHCI 事件环 LINK 与轮询闸锁、EHCI CONFIGFLAG、OHCI 中断表/TD_R/NPS、MSC residue 与 sector_size、CDC-ACM 协议、SS EP0 mps9、FAT32 rmdir 点项 + UAF + 簇环越界 + unlink 保护、exFAT 位图生命周期、ext4 extent 偏移与恶意卷越界、e1000 strcat、ld_so 边界、网络 IP 帧校验 + RX 校验和 + 序号回绕 + RST 校验 + SYN_RCVD 回收 + SYN 选项 + udp_bind 去重 + 窗口缩放 + RTO 临界区。WP-10-wp08fix1 补全 WP-08 Shell 规格：oc> 与 ush 双端全键位行编辑（上下键历史翻页、左右/Home/End 光标、Tab 补全、Ctrl+A/E/U/K/W、Delete、Ctrl+C）、ush 补齐 15 个工具（ln/ln -s、chmod/chown、sed、awk、ping/wget/netstat/ifconfig、ps/kill/top/du、stat/env）、nano 风格编辑器双端可用（nano/vi，^O 保存 ^X 退出）；USB 主机栈（四控制器）、声卡（六族）、系统内 A/B 更新、网卡/存储驱动与安全传输保持主流化：",
  heroSubBold2: "SSH（curve25519、主机密钥验证、公钥认证）、TLS 1.3 / TLS 1.2（CA 链验证）、crypto 核心",
  heroSubEnd: "。",
  downloadIso: (mb: string) => `下载 ISO（${mb} MB）`,
  readDocs: "阅读文档",
  githubRepo: "GitHub 仓库",
  dlSectionTitle: "下载 WP-10-AUDIT_P2-fix2b",
  dlSectionDesc:
    "BIOS + UEFI 双引导 ISO 与完整源码包。文件在本站、GitHub Release 与本地构建三处 SHA256 字节级一致（核对方法见下方命令）。",
  isoMeta: (bytes: string, mb: string) =>
    `可引导 ISO · ${bytes} 字节（${mb} MB）· GRUB multiboot2 · SeaBIOS / OVMF 双引导`,
  srcMeta: (bytes: string, mb: string) =>
    `完整源码（git archive）· ${bytes} 字节（${mb} MB）· 内核 + 引导 + 用户程序 + 文档 + 工具`,
  siteDlIso: "本站下载 ISO",
  siteDlSrc: "本站下载源码包",
  ghRelease: "GitHub Release",
  dlNotePre: "核对：",
  dlNotePost: "，结果应与上方及 GitHub Release 资产一致。",
  featSectionTitle: "功能",
  featSectionDesc:
    "Shell 完全体（WP-10-wp08fix1：oc>/ush 双端全键位行编辑 + ush 15 个工具 + nano/vi 编辑器）、用户角度可用性（第 ⑨ 条审计）、USB 主机栈（WP-10d）、声卡驱动（WP-10c：Intel HDA / AC'97 / SB16 / ES1370 / virtio-snd / USB 音频）、系统内自动更新（WP-10u）、网卡驱动（WP-10b）、存储驱动（WP-10a）与安全传输（WP-06 基础 + WP-09 增强），全部在 QEMU 实测中验证。",
  baseSectionTitle: "内核基础（WP-01 ~ WP-08）",
  baseSectionDesc: "从裸机引导到用户态动态链接，每一层都可扩展。",
  wpSectionTitle: "工作包",
  wpSectionDesc: "16 个工作包，WP-01 到 WP-10d、项目结构重构与 WP-10-wp08fix1，全部完成（done）；WP-AUDIT-01 完成 18 路完整审查（607 条），落地全部 41 个 P0 修复（p0fix1 前 20 个 + p0fix2 后 21 个），P1 全部 94 条修复完成（p1fix1 前 31 条 BUG-0042..0072 + p1fix2 第 32~62 条 BUG-0073..0103 + p1fix3 剩余 31 条 BUG-0104..0135），p1fix4 修复 HTTPS E2E 中途断连（IP 字面量参考身份，RFC 6125）；WP-10-AUDIT_P2-fix1 修复 P2 第一批 53 条（BUG-0136..0188）：内存（VMM 页表所有权、kmalloc 回绕、碎片化/池槽、畸形 mbi）、arch（SysV 对齐、IST2 退出栈）、USB 核心/四控制器与类驱动 33 条（DMA ≥4GiB 拒绝、TD 退休竞态、mps0 容量、C_PORT_RESET、TRT/EP 上下文/scratchpad/BAR、SENSE 窗口、实例命名、多端口、FS 速度、SETUP DATA0、UAC set_rate 等）、VFS/FAT 11 条（只读按位、大小写敏感缓存、稀疏洞清零、权限生效、FAT#1 镜像、FSINFO、LFN 别名/校验/跨簇删除）；发现并修复堆块头 40 字节潜伏缺陷（负载对齐漂移致 ATA-DMA 偏移 8 字节、/etc 无法挂载）；fstest 新增 P2 双向断言块；新增 irqabitest/heapbounds/vmkernelpt/int3_user 测试；WP-10-AUDIT_P2-fix2 修复 P2 第二批 53 条（BUG-0189..0241，覆盖 fs 12 条 / net_core 13 条 / ld_so 4 条 / ssh 10 条 / tls 8 条 / shell 6 条）并完成发布链四项加固（git describe 版本串、HTTPS E2E 双向 close_notify 断言、INTERFACES.md 176 命令、docs/verification/ 转公开）与嵌入链 Makefile 规则（make userprogs / userprogs-check）；WP-10-AUDIT_P2-fix2b 实测复核 fix2 全量载荷（53/53 条在树 + 4/4 findings）并修复嵌入链三缺陷（libfoo.c 独立 solib 链、六个 dyn 程序 ET_DYN/PIE 配方 + 防静默降级硬校验、libs/ld_so.c 纳入源清单），make userprogs 端到端跑通，六个 dyn 程序 QEMU 全 PASS。",
  wpThNo: "编号",
  wpThContent: "内容",
  wpThStatus: "状态",
  verifySectionTitle: "验证",
  verifySectionPre: "完整验证记录：",
  verifySectionPost: "，含真实输出与验证方法。",
  guidesPre: "上手指南（文档页内嵌，仓库 docs/ 同步提供）：",
  guidesMid: " 与 ",
  guidesPost: "。",
  docsTitle: "文档",
  docsDescPre:
    "以下内容在构建时直接读取自仓库真实文档文件，原样展示、一字未改。如需查看最新版本，请访问 ",
  docsDescPost: "。",
  docsNavAria: "文档目录",
  docsBack: "← 返回首页",
  docsSourcePrefix: "来源：",
  docsLangNote:
    "本页为中文翻译版（由本站提供，便于阅读）。事实与数字以英文原文为准；英文原文见 English 版文档页。",
  aboutTitle: "关于",
  aboutDesc:
    "Open Cube OS 的定位、历史与工程方法。所有数字来自仓库公开文档（README.md、docs/INTERFACES.md、docs/TRY-IT.md、docs/UPDATE-HOWTO.md）。",
  aboutPositioning: "定位",
  aboutPositioningBodyPre:
    "Open Cube OS 是一个开源操作系统内核，两层架构：",
  aboutPositioningL0: "L0",
  aboutPositioningL0Body: " = 完整内核（本项目，Apache 2.0）；",
  aboutPositioningL1: "L1",
  aboutPositioningL1Body:
    " = 上层扩展，构建在 L0 暴露的 138 个扩展接口之上，L0 不内置任何 L1。设计原则：一切皆可扩展——每个功能都有扩展接口，每个接口都有文档、默认实现与示例。",
  aboutQuote:
    "It is not \u201ca system you can use daily\u201d. It is \u201ca kernel that can be extended into anything\u201d. — README.md",
  aboutHistory: "项目历史（15 个工作包 + 项目结构重构）",
  aboutMethod: "工程方法",
  aboutMethodBody:
    "每个工作包以真实可复现的证据收尾：截至 WP-08 累计修复 120 个审计 bug；WP-09 完成 SSH/TLS 主流化；WP-10a 落地四类存储驱动并根治 fork #PF 潜伏缺陷；WP-10b 落地九族网卡驱动与 nic_* 框架；WP-10u 落地 A/B 分区系统内自动更新（内核 gzip/DEFLATE + ustar + SHA256 + 自动回滚）；WP-10c 落地六族声卡驱动与 snd_* 框架（Intel HDA CORB/RIRB + codec/widget 枚举、AC'97、SB16 ISA DMA、ES1370、virtio-snd、USB Audio Class 1.0 + 新 UHCI 主机栈，真 DMA 真中断）；WP-10c-selfhost 按第 ⑨ 条（测试通过 ≠ 用户能用）完成全系统用户角度审计（五要素：命令/工具/文档/示例/反馈；五能力：创建/使用/回滚/查看/理解）；WP-10d 落地 USB 主机栈四控制器（UHCI/OHCI/EHCI/XHCI 设备级枚举 + HID/MSC/串口/音频类驱动 + Hub/热插拔；OHCI 独立验证全 PASS，XHCI EPID 编码按 spec 修正至 bits 20:16）；项目结构重构落地新文件体系（drivers/、fs/、net/、shell/、l1/ 等）与 [大类]_[具体]_[更小一级] 接口命名规范；WP-10-wp08fix1 按 WP-08 规格补全 Shell：oc> 与 ush 双端全键位行编辑（上下键历史翻页、左右/Home/End 光标、Tab 命令+路径补全、Ctrl+C/A/E/U/K/W、Delete）、ush 补齐 15 个工具（ln/ln -s、chmod/chown、sed、awk、ping/wget/netstat/ifconfig 对接内核协议栈、ps/kill/top/du 对接 sys_proc_*、stat/env 补实 help 承诺）、nano 风格编辑器双端可用（^O 保存 ^X 退出，真 VFS 落盘），新增 9 个 L1 扩展接口（130-138，L1 总数增至 138）。最终验证为 18/18 QEMU 全量回归（挂四类盘）+ WP-10-wp08fix1 端到端 24/24（真实按键驱动：行编辑/工具/编辑器）+ WP-10a 八项存储测试 + WP-10b 网卡测试 + WP-10u 十一项更新测试（含真实重启进入 slot B 的端到端）+ WP-10c 九项声卡测试（QEMU 五卡实测播放、44.1/48kHz）+ WP-10d 九项 USB 测试（四控制器实测、BIOS+UEFI 启动矩阵）+ dhtest 5/5 + HTTPS 真实站点 E2E。完整验证记录：",
  aboutMethodMid: "。各工作包逐项记录：",
  aboutMethodEnd: "。",
  aboutAi: "AI 披露",
  aboutAiBody:
    "本项目由 cubestudio-dev 在 AI 工具协助下开发。全部设计决策、架构、规格、项目管理、代码评审、质量保证与验收测试由 cubestudio-dev 完成；AI 工具仅作为实现辅助。",
  aboutLicense: "许可与联系",
  aboutLicenseBody: "Apache License 2.0（",
  aboutLicenseMid: " · ",
  aboutLicenseEnd: "）。",
  aboutCopyright:
    "Copyright 2026 cubestudio-dev <cubestudio@qq.com>。仓库：",
  aboutRepoMid: "，历史版本见 ",
  aboutRepoEnd: "。",
  aboutBack: "← 返回首页",
  footer: "© 2026 cubestudio-dev <cubestudio@qq.com> · Apache License 2.0",
  footerDlIso: "下载 ISO",
  redirecting: "正在进入 Open Cube OS 网站…",
  redirectNoscript: "未启用 JavaScript，请选择语言：",
  redirectZh: "中文",
  redirectEn: "English",
};

const en: typeof zh = {
  htmlLang: "en",
  badge: "WP-10-AUDIT_P2-fix2b · Apache 2.0",
  brandSub: "WP-10-AUDIT_P2-fix2b · Apache 2.0",
  navAria: "Site navigation",
  navHome: "Home",
  navDownloads: "Downloads",
  navDocs: "Docs",
  navAbout: "About",
  navGithub: "GitHub",
  langAria: "Switch language",
  heroTitleLine1: "Open Cube OS",
  heroTitleLine2: "Open-Source x86_64 L0 Kernel",
  heroSubPre: "It is not ",
  heroSubQuoted: "\u201ca system you can use daily\u201d",
  heroSubMid: ". It is ",
  heroSubBold: "\u201ca kernel that can be extended into anything\u201d",
  heroSubPost:
    ". Its value is not what it ships with, but the interfaces it exposes to upper layers: L0 = the complete kernel, L1 = upper-layer extensions, L0 ships without any L1. WP-AUDIT-01 completed an 18-route line-by-line audit (607 findings); all 41 P0s are fixed (p0fix1 fixed the first 20: pre-auth memory corruption across the filesystem, network stack, SSH client and sshd; p0fix2 fixed the remaining 21: TLS record/message bounds and the TLS 1.3 CertVerify DER, shell glob/fsck/mv, sys_poll wrap and SYS_EXIT buffer, execve CR3 ordering, sys_ps ktab, X509 DER overflow, config write bounds, OTA changes/request/path traversal, NVMe non-512-byte LBA refusal, nested-#PF guard), each with verification evidence (QEMU FAIL-before/PASS-after, host ASAN, or construction-level path evidence); WP-AUDIT-01-p1fix2 fixed the first 31 P1s (BUG-0042..0072): per-task FPU/SSE fxsave context + CR4.OSFXSR + fork inheritance, PMM bitmap cli atomicity, page-fault semantics (P=1 refusal, 8MiB stack floor, U/S ring, kernel-on-user-address-space refusal), the XHCI event-ring LINK wrap and poll latch, EHCI CONFIGFLAG, the OHCI interrupt table / TD_R / NPS, MSC residue and sector_size, the CDC-ACM protocol, SS EP0 mps9, FAT32 rmdir dot entries + UAF + cluster-cycle bounds + the unlink guard, the exFAT bitmap lifecycle, ext4 extent offsets and crafted-volume bounds, the e1000 strcat, ld_so bounds, and the network stack (IP frame checks + RX checksums + sequence wrap + RST validation + SYN_RCVD reaping + SYN options + udp_bind dedup + window scaling + RTO critical sections). WP-10-wp08fix1 completes the WP-08 shell specification: full-featured line editing on both oc> and ush (Up/Down history paging, Left/Right/Home/End cursor, Tab completion, Ctrl+A/E/U/K/W, Delete, Ctrl+C), 15 missing ush tools (ln/ln -s, chmod/chown, sed, awk, ping/wget/netstat/ifconfig, ps/kill/top/du, stat/env) and a nano-style editor on both shells (nano/vi, ^O save ^X exit); the USB host stack (four controllers), sound cards (six families), in-system A/B updates and NIC/storage drivers stay mainstreamed, as does the secure-transport layer: ",
  heroSubBold2: "SSH (curve25519, host-key verification, publickey auth), TLS 1.3 / TLS 1.2 (CA chain verification), crypto core",
  heroSubEnd: ".",
  downloadIso: (mb: string) => `Download ISO (${mb} MB)`,
  readDocs: "Read the docs",
  githubRepo: "GitHub repository",
  dlSectionTitle: "Download WP-10-AUDIT_P2-fix2b",
  dlSectionDesc:
    "BIOS + UEFI dual-boot ISO and the full source archive. The files are byte-identical (SHA256) across this site, the GitHub Release and the local build (see the command below).",
  isoMeta: (bytes: string, mb: string) =>
    `Bootable ISO · ${bytes} bytes (${mb} MB) · GRUB multiboot2 · SeaBIOS / OVMF dual boot`,
  srcMeta: (bytes: string, mb: string) =>
    `Full source (git archive) · ${bytes} bytes (${mb} MB) · kernel + boot + user programs + docs + tools`,
  siteDlIso: "Download ISO from this site",
  siteDlSrc: "Download the source archive",
  ghRelease: "GitHub Release",
  dlNotePre: "Verify: ",
  dlNotePost:
    " — the result must match the value above and the GitHub Release asset.",
  featSectionTitle: "Features",
  featSectionDesc:
    "The complete shell (WP-10-wp08fix1: full line editing on oc>/ush + 15 ush tools + the nano/vi editor), user-angle usability (the rule-9 audit), the USB host stack (WP-10d), sound card drivers (WP-10c: Intel HDA / AC'97 / SB16 / ES1370 / virtio-snd / USB audio), in-system updates (WP-10u), NIC drivers (WP-10b), storage drivers (WP-10a) plus the network stack and secure transport (WP-06 base + WP-09 additions), all verified in real QEMU runs.",
  baseSectionTitle: "Kernel base (WP-01 ~ WP-08)",
  baseSectionDesc:
    "From bare-metal boot to user-space dynamic linking — every layer is extensible.",
  wpSectionTitle: "Work packages",
  wpSectionDesc: "16 work packages, WP-01 through WP-10d, the project restructure and WP-10-wp08fix1, all done; WP-AUDIT-01 completed the 18-route full audit (607 findings), landed all 41 P0 fixes (p0fix1 first 20 + p0fix2 remaining 21), and all 94 P1 fixes are done (p1fix1 the first 31, BUG-0042..0072 + p1fix2 items 32..62, BUG-0073..0103 + p1fix3 items 63..94, BUG-0104..0135), with the p1fix4 HTTPS E2E mid-handshake disconnect fix (IP-literal reference identity, RFC 6125); WP-10-AUDIT_P2-fix1 fixed the first 53 P2 items (BUG-0136..0188): memory (VMM PT ownership, kmalloc wrap, fragmentation/pool slots, malformed-mbi hardening), arch (SysV stack alignment, the IST2 exit-stack kill path), 33 USB core/controller/class fixes (>=4GiB DMA refusal, TD-retirement races, mps0 capacity, C_PORT_RESET, TRT/EP-context/scratchpad/BAR-64, the SENSE window, instance naming, multi-port, FS speed, SETUP DATA0, real UAC set_rate), and 11 VFS/FAT fixes (bitwise read-only checks, per-fs cache case rules, sparse-hole zeroing, enforced permission bits, the FAT#1 mirror, FSINFO, LFN alias/checksum/cross-cluster deletion); the batch also found and fixed a latent 40-byte heap block header defect (payload alignment drifted with allocation parity, shifting ATA-DMA transfers by 8 bytes and breaking the /etc mount); fstest gained a P2 dual-sided assertion block; new irqabitest/heapbounds/vmkernelpt/int3_user tests; WP-10-AUDIT_P2-fix2b re-verified the full fix2 payload against the tree (53/53 items + 4/4 findings) and fixed the three embed-chain defects (a dedicated libfoo solib leg, ET_DYN/PIE recipes for the six dyn programs with hard anti-downgrade gates, libs/ld_so.c added to the source list), with make userprogs green end-to-end and all six dyn programs passing under QEMU.",
  wpThNo: "No.",
  wpThContent: "Content",
  wpThStatus: "Status",
  verifySectionTitle: "Verification",
  verifySectionPre: "Full verification record: ",
  verifySectionPost: ", with real outputs and verification methods.",
  guidesPre: "Hands-on guides (embedded in the docs page, also in the repo under docs/): ",
  guidesMid: " and ",
  guidesPost: ".",
  docsTitle: "Docs",
  docsDescPre:
    "The content below is read verbatim from the real repository documents at build time. For the latest version, visit the ",
  docsDescPost: ".",
  docsNavAria: "Docs table of contents",
  docsBack: "← Back to home",
  docsSourcePrefix: "Source: ",
  docsLangNote:
    "This page shows the original English documents, read verbatim from the repository at build time. A Chinese translation is available on the 中文 docs page.",
  aboutTitle: "About",
  aboutDesc:
    "Open Cube OS: positioning, history and engineering method. All numbers come from the public repository documents (README.md, docs/INTERFACES.md, docs/TRY-IT.md, docs/UPDATE-HOWTO.md).",
  aboutPositioning: "Positioning",
  aboutPositioningBodyPre:
    "Open Cube OS is an open-source operating-system kernel with a two-tier architecture: ",
  aboutPositioningL0: "L0",
  aboutPositioningL0Body: " = the complete kernel (this project, Apache 2.0); ",
  aboutPositioningL1: "L1",
  aboutPositioningL1Body:
    " = upper-layer extensions, built on the 138 extension interfaces L0 exposes — L0 ships without any L1. Design principle: everything is extensible — every feature has an extension interface, and every interface has docs, a default implementation and examples.",
  aboutQuote:
    "It is not \u201ca system you can use daily\u201d. It is \u201ca kernel that can be extended into anything\u201d. — README.md",
  aboutHistory: "Project history (15 work packages + the project restructure)",
  aboutMethod: "Engineering method",
  aboutMethodBody:
    "Every work package closes with real, reproducible evidence: 120 audit bugs fixed cumulatively as of WP-08; WP-09 added the SSH/TLS mainstreaming; WP-10a landed the four storage drivers and root-fixed a latent fork #PF; WP-10b landed the nine NIC driver families and the nic_* framework; WP-10u landed A/B-partition in-system updates (kernel-side gzip/DEFLATE + ustar + SHA256 + automatic rollback); WP-10c landed the six sound-driver families and the snd_* framework (Intel HDA CORB/RIRB + codec/widget enumeration, AC'97, SB16 ISA DMA, ES1370, virtio-snd, USB Audio Class 1.0 + a new UHCI host stack, real DMA + real interrupts); WP-10c-selfhost audited the whole system from the user angle per rule 9 (passing tests ≠ usable by users; five elements: command/tool/docs/examples/feedback; five abilities: create/use/roll back/inspect/understand); WP-10d landed the USB host stack on four controllers (UHCI/OHCI/EHCI/XHCI device-level enumeration + HID/MSC/serial/audio class drivers + hub and hot-plug; OHCI independent session ALL PASS, XHCI endpoint-ID encoding corrected to spec bits 20:16); the project restructure landed the new file tree (drivers/, fs/, net/, shell/, l1/, ...) and the [category]_[specific]_[smaller] interface naming convention; WP-10-wp08fix1 completed the WP-08 shell specification: full-featured line editing on both oc> and ush (Up/Down history paging, Left/Right/Home/End cursor, Tab command+path completion, Ctrl+C/A/E/U/K/W, Delete), 15 missing ush tools (ln/ln -s, chmod/chown, sed, awk, ping/wget/netstat/ifconfig wired into the kernel network stack, ps/kill/top/du wired into sys_proc_*, stat/env delivering what help always promised) and a nano-style editor usable on both shells (^O save, ^X exit, real VFS persistence), plus 9 new L1 extension interfaces (items 130-138, 138 in total). Final verification: 18/18 full QEMU regression (all four disk types attached) + the WP-10-wp08fix1 end-to-end 24/24 (driven by real keystrokes: line editing/tools/editor) + the eight WP-10a storage tests + the WP-10b NIC tests + the eleven WP-10u update tests (including an end-to-end with a real reboot into slot B) + the nine WP-10c sound tests (five cards playing live in QEMU, 44.1/48 kHz) + the nine WP-10d USB tests (four controllers live, BIOS+UEFI boot matrix) + dhtest 5/5 + HTTPS E2E against real sites. Full verification record: ",
  aboutMethodMid: ". Per-work-package records: ",
  aboutMethodEnd: ".",
  aboutAi: "AI disclosure",
  aboutAiBody:
    "This project was developed by cubestudio-dev with the assistance of AI tools. All design decisions, architecture, specifications, project management, code review, quality assurance, and acceptance testing were performed by cubestudio-dev; AI tools were used as implementation assistants.",
  aboutLicense: "License & contact",
  aboutLicenseBody: "Apache License 2.0 (",
  aboutLicenseMid: " · ",
  aboutLicenseEnd: ").",
  aboutCopyright:
    "Copyright 2026 cubestudio-dev <cubestudio@qq.com>. Repository: ",
  aboutRepoMid: ", historical versions in ",
  aboutRepoEnd: ".",
  aboutBack: "← Back to home",
  footer: "© 2026 cubestudio-dev <cubestudio@qq.com> · Apache License 2.0",
  footerDlIso: "Download ISO",
  redirecting: "Entering the Open Cube OS site…",
  redirectNoscript: "JavaScript is disabled — please pick a language:",
  redirectZh: "中文",
  redirectEn: "English",
};

export const T: Record<Locale, typeof zh> = { zh, en };
