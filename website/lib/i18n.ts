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
  badge: "WP-09-fix4 · 正式版 · Apache 2.0",
  brandSub: "WP-09-fix4 · Apache 2.0",
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
    "。价值不在自带什么，而在向上层暴露的接口：L0 = 完整内核，L1 = 上层扩展，L0 不内置任何 L1。WP-09 新增安全传输层：",
  heroSubBold2: "SSH（客户端 + 服务端）、TLS 1.2 / HTTPS、crypto 核心",
  heroSubEnd: "。",
  downloadIso: (mb: string) => `下载 ISO（${mb} MB）`,
  readDocs: "阅读文档",
  githubRepo: "GitHub 仓库",
  dlSectionTitle: "下载 WP-09-fix4",
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
    "网络栈与安全传输（WP-06 基础 + WP-09 增强），全部在 QEMU 真机回归中验证。",
  baseSectionTitle: "内核基础（WP-01 ~ WP-08）",
  baseSectionDesc: "从裸机引导到用户态动态链接，每一层都可扩展。",
  wpSectionTitle: "工作包",
  wpSectionDesc: "9 个工作包，WP-01 到 WP-09，全部完成（done）。",
  wpThNo: "编号",
  wpThContent: "内容",
  wpThStatus: "状态",
  verifySectionTitle: "验证",
  verifySectionPre: "完整证据（真实输出 + 双侧日志）：",
  verifySectionPost: "，原始日志在 docs/verification/。",
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
    "Open Cube OS 的定位、历史与工程方法。所有数字来自仓库公开文档（README.md、docs/VERIFICATION_BATCH_B.md、docs/WORK_LOG.md）。",
  aboutPositioning: "定位",
  aboutPositioningBodyPre:
    "Open Cube OS 是一个开源操作系统内核，两层架构：",
  aboutPositioningL0: "L0",
  aboutPositioningL0Body: " = 完整内核（本项目，Apache 2.0）；",
  aboutPositioningL1: "L1",
  aboutPositioningL1Body:
    " = 上层扩展，构建在 L0 暴露的 57 个扩展接口之上，L0 不内置任何 L1。设计原则：一切皆可扩展——每个功能都有扩展接口，每个接口都有文档、默认实现与示例。",
  aboutQuote:
    "It is not \u201ca system you can use daily\u201d. It is \u201ca kernel that can be extended into anything\u201d. — README.md",
  aboutHistory: "项目历史（九个工作包）",
  aboutMethod: "工程方法",
  aboutMethodBody:
    "每个工作包以真实可复现的证据收尾：截至 WP-08 累计修复 120 个审计 bug；WP-09 又完成 SSH/TLS 专项修复。最终验证为 18/18 QEMU 全量回归 + dhtest 5/5 + HTTPS 双侧 E2E + SSH 双向互操作（paramiko K 字节级一致）。完整证据与原始日志：",
  aboutMethodMid: " 与 docs/verification/。历史记录：",
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
  badge: "WP-09-fix4 · Release · Apache 2.0",
  brandSub: "WP-09-fix4 · Apache 2.0",
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
    ". Its value is not what it ships with, but the interfaces it exposes to upper layers: L0 = the complete kernel, L1 = upper-layer extensions, L0 ships without any L1. WP-09 adds a secure-transport layer: ",
  heroSubBold2: "SSH (client + server), TLS 1.2 / HTTPS, crypto core",
  heroSubEnd: ".",
  downloadIso: (mb: string) => `Download ISO (${mb} MB)`,
  readDocs: "Read the docs",
  githubRepo: "GitHub repository",
  dlSectionTitle: "Download WP-09-fix4",
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
    "Network stack and secure transport (WP-06 base + WP-09 additions), all verified in real QEMU regression runs.",
  baseSectionTitle: "Kernel base (WP-01 ~ WP-08)",
  baseSectionDesc:
    "From bare-metal boot to user-space dynamic linking — every layer is extensible.",
  wpSectionTitle: "Work packages",
  wpSectionDesc: "9 work packages, WP-01 through WP-09, all done.",
  wpThNo: "No.",
  wpThContent: "Content",
  wpThStatus: "Status",
  verifySectionTitle: "Verification",
  verifySectionPre: "Full evidence (real outputs + both-side logs): ",
  verifySectionPost: "; raw logs in docs/verification/.",
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
    "Open Cube OS: positioning, history and engineering method. All numbers come from the public repository documents (README.md, docs/VERIFICATION_BATCH_B.md, docs/WORK_LOG.md).",
  aboutPositioning: "Positioning",
  aboutPositioningBodyPre:
    "Open Cube OS is an open-source operating-system kernel with a two-tier architecture: ",
  aboutPositioningL0: "L0",
  aboutPositioningL0Body: " = the complete kernel (this project, Apache 2.0); ",
  aboutPositioningL1: "L1",
  aboutPositioningL1Body:
    " = upper-layer extensions, built on the 57 extension interfaces L0 exposes — L0 ships without any L1. Design principle: everything is extensible — every feature has an extension interface, and every interface has docs, a default implementation and examples.",
  aboutQuote:
    "It is not \u201ca system you can use daily\u201d. It is \u201ca kernel that can be extended into anything\u201d. — README.md",
  aboutHistory: "Project history (nine work packages)",
  aboutMethod: "Engineering method",
  aboutMethodBody:
    "Every work package closes with real, reproducible evidence: 120 audit bugs fixed cumulatively as of WP-08; WP-09 added further SSH/TLS fixes. Final verification: 18/18 full QEMU regression + dhtest 5/5 + HTTPS both-side E2E + SSH both-direction interop (paramiko, byte-identical K). Full evidence and raw logs: ",
  aboutMethodMid: " and docs/verification/. History: ",
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
