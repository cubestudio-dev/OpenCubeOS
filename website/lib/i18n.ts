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
  badge: "WP-10c-selfhost · Apache 2.0",
  brandSub: "WP-10c-selfhost · Apache 2.0",
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
    "。价值不在自带什么，而在向上层暴露的接口：L0 = 完整内核，L1 = 上层扩展，L0 不内置任何 L1。WP-10c 声卡驱动（Intel HDA / AC'97 / SB16 / ES1370 / virtio-snd / USB 音频，真 DMA 真中断）已落地，第 ⑨ 条用户角度可用性审计完成（TRY-IT / UPDATE-HOWTO 指南、一键 A/B 磁盘、mkfs/fsck 修复、play/volume 反馈），系统内自动更新、网卡/存储驱动与安全传输保持主流化：",
  heroSubBold2: "SSH（curve25519、主机密钥验证、公钥认证）、TLS 1.3 / TLS 1.2（CA 链验证）、crypto 核心",
  heroSubEnd: "。",
  downloadIso: (mb: string) => `下载 ISO（${mb} MB）`,
  readDocs: "阅读文档",
  githubRepo: "GitHub 仓库",
  dlSectionTitle: "下载 WP-10c-selfhost",
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
    "用户角度可用性（第 ⑨ 条审计）、声卡驱动（WP-10c：Intel HDA / AC'97 / SB16 / ES1370 / virtio-snd / USB 音频）、系统内自动更新（WP-10u）、网卡驱动（WP-10b）、存储驱动（WP-10a）与安全传输（WP-06 基础 + WP-09 增强），全部在 QEMU 实测中验证。",
  baseSectionTitle: "内核基础（WP-01 ~ WP-08）",
  baseSectionDesc: "从裸机引导到用户态动态链接，每一层都可扩展。",
  wpSectionTitle: "工作包",
  wpSectionDesc: "13 个工作包，WP-01 到 WP-10c，全部完成（done）；另已完成第 ⑨ 条用户角度可用性审计（WP-10c-selfhost）。",
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
    " = 上层扩展，构建在 L0 暴露的 111 个扩展接口之上，L0 不内置任何 L1。设计原则：一切皆可扩展——每个功能都有扩展接口，每个接口都有文档、默认实现与示例。",
  aboutQuote:
    "It is not \u201ca system you can use daily\u201d. It is \u201ca kernel that can be extended into anything\u201d. — README.md",
  aboutHistory: "项目历史（13 个工作包）",
  aboutMethod: "工程方法",
  aboutMethodBody:
    "每个工作包以真实可复现的证据收尾：截至 WP-08 累计修复 120 个审计 bug；WP-09 完成 SSH/TLS 主流化；WP-10a 落地四类存储驱动并根治 fork #PF 潜伏缺陷；WP-10b 落地九族网卡驱动与 nic_* 框架；WP-10u 落地 A/B 分区系统内自动更新（内核 gzip/DEFLATE + ustar + SHA256 + 自动回滚）；WP-10c 落地六族声卡驱动与 snd_* 框架（Intel HDA CORB/RIRB + codec/widget 枚举、AC'97、SB16 ISA DMA、ES1370、virtio-snd、USB Audio Class 1.0 + 新 UHCI 主机栈，真 DMA 真中断）；WP-10c-selfhost 按第 ⑨ 条（测试通过 ≠ 用户能用）完成全系统用户角度审计（五要素：命令/工具/文档/示例/反馈；五能力：创建/使用/回滚/查看/理解）。最终验证为 18/18 QEMU 全量回归（挂四类盘）+ WP-10a 八项存储测试 + WP-10b 网卡测试 + WP-10u 十一项更新测试（含真实重启进入 slot B 的端到端）+ WP-10c 九项声卡测试（QEMU 五卡实测播放、44.1/48kHz）+ dhtest 5/5 + HTTPS 真实站点 E2E。完整验证记录：",
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
  badge: "WP-10c-selfhost · Apache 2.0",
  brandSub: "WP-10c-selfhost · Apache 2.0",
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
    ". Its value is not what it ships with, but the interfaces it exposes to upper layers: L0 = the complete kernel, L1 = upper-layer extensions, L0 ships without any L1. WP-10c sound card drivers (Intel HDA / AC'97 / SB16 / ES1370 / virtio-snd / USB audio, real DMA + real interrupts) have landed, the rule-9 user-angle usability audit is complete (TRY-IT / UPDATE-HOWTO guides, one-command A/B disks, mkfs/fsck fixes, play/volume feedback), and in-system updates, NIC/storage drivers and the secure-transport layer stay mainstreamed: ",
  heroSubBold2: "SSH (curve25519, host-key verification, publickey auth), TLS 1.3 / TLS 1.2 (CA chain verification), crypto core",
  heroSubEnd: ".",
  downloadIso: (mb: string) => `Download ISO (${mb} MB)`,
  readDocs: "Read the docs",
  githubRepo: "GitHub repository",
  dlSectionTitle: "Download WP-10c-selfhost",
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
    "User-angle usability (the rule-9 audit), sound card drivers (WP-10c: Intel HDA / AC'97 / SB16 / ES1370 / virtio-snd / USB audio), in-system updates (WP-10u), NIC drivers (WP-10b), storage drivers (WP-10a) plus the network stack and secure transport (WP-06 base + WP-09 additions), all verified in real QEMU runs.",
  baseSectionTitle: "Kernel base (WP-01 ~ WP-08)",
  baseSectionDesc:
    "From bare-metal boot to user-space dynamic linking — every layer is extensible.",
  wpSectionTitle: "Work packages",
  wpSectionDesc: "13 work packages, WP-01 through WP-10c, all done; the rule-9 user-angle usability audit (WP-10c-selfhost) is complete as well.",
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
    " = upper-layer extensions, built on the 111 extension interfaces L0 exposes — L0 ships without any L1. Design principle: everything is extensible — every feature has an extension interface, and every interface has docs, a default implementation and examples.",
  aboutQuote:
    "It is not \u201ca system you can use daily\u201d. It is \u201ca kernel that can be extended into anything\u201d. — README.md",
  aboutHistory: "Project history (13 work packages)",
  aboutMethod: "Engineering method",
  aboutMethodBody:
    "Every work package closes with real, reproducible evidence: 120 audit bugs fixed cumulatively as of WP-08; WP-09 added the SSH/TLS mainstreaming; WP-10a landed the four storage drivers and root-fixed a latent fork #PF; WP-10b landed the nine NIC driver families and the nic_* framework; WP-10u landed A/B-partition in-system updates (kernel-side gzip/DEFLATE + ustar + SHA256 + automatic rollback); WP-10c landed the six sound-driver families and the snd_* framework (Intel HDA CORB/RIRB + codec/widget enumeration, AC'97, SB16 ISA DMA, ES1370, virtio-snd, USB Audio Class 1.0 + a new UHCI host stack, real DMA + real interrupts); WP-10c-selfhost audited the whole system from the user angle per rule 9 (passing tests ≠ usable by users; five elements: command/tool/docs/examples/feedback; five abilities: create/use/roll back/inspect/understand). Final verification: 18/18 full QEMU regression (all four disk types attached) + the eight WP-10a storage tests + the WP-10b NIC tests + the eleven WP-10u update tests (including an end-to-end with a real reboot into slot B) + the nine WP-10c sound tests (five cards playing live in QEMU, 44.1/48 kHz) + dhtest 5/5 + HTTPS E2E against real sites. Full verification record: ",
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
