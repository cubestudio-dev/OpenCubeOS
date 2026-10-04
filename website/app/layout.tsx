// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 cubestudio-dev <cubestudio@qq.com>

import type { Metadata } from "next";
import { SITE_URL } from "@/lib/site";
import "./globals.css";

export const metadata: Metadata = {
  title: "Open Cube OS - 开源 x86_64 L0 内核",
  description:
    "Open Cube OS：开源 x86_64 内核（L0），可被扩展成任何东西。WP-10-wp08fix1 Shell 补全已落地：oc> 与 ush 双端全键位行编辑（上下键历史翻页、左右/Home/End 光标、Tab 补全、Ctrl+A/E/U/K/W、Delete、Ctrl+C）、ush 补齐 15 个工具（ln/chmod/chown/sed/awk/ping/wget/netstat/ifconfig/ps/kill/top/du + stat/env）、nano 风格编辑器双端可用（nano/vi，^O 保存 ^X 退出）；USB 主机栈（四控制器）、声卡（六族）、系统内 A/B 更新、网卡/存储驱动保持；SSH + TLS 1.3/1.2 + HTTPS 保持主流化。BIOS + UEFI 双引导，90,705 行源码，138 个 L1 扩展接口，172 条 shell 命令，24/24 端到端测试。Apache 2.0。",
  metadataBase: new URL("https://cubestudio-dev.github.io"),
  openGraph: {
    title: "Open Cube OS - 开源 x86_64 L0 内核",
    description:
      "开源 x86_64 内核（L0），可被扩展成任何东西。WP-10-wp08fix1：Shell 完全体——oc>/ush 双端全键位行编辑、ush 15 个工具（对接内核协议栈与 sys_proc_*）、nano/vi 编辑器、9 个新 L1 接口（130-138，总数 138）、172 条命令、24/24 端到端；USB 主机栈四控制器、声卡六族 + snd_*、系统内 A/B 更新、网卡九族 + nic_*、存储四驱动 + blk_*；SSH + TLS 1.3 / TLS 1.2 + HTTPS + crypto 核心 + TCP 可靠性 + netfilter。Apache 2.0。",
    url: SITE_URL,
    siteName: "Open Cube OS",
    type: "website",
  },
};

// Single root layout (App Router requirement). Static export has exactly
// one <html>, so per-locale lang is set by a tiny inline script in each
// locale layout (no hydration flash: it runs during parse).
export default function RootLayout({
  children,
}: Readonly<{ children: React.ReactNode }>) {
  return (
    <html lang="zh-CN">
      <body>{children}</body>
    </html>
  );
}
