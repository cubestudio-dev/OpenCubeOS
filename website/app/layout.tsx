// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 cubestudio-dev <cubestudio@qq.com>

import type { Metadata } from "next";
import { SITE_URL } from "@/lib/site";
import "./globals.css";

export const metadata: Metadata = {
  title: "Open Cube OS - 开源 x86_64 L0 内核",
  description:
    "Open Cube OS：开源 x86_64 内核（L0），可被扩展成任何东西。WP-10c 声卡驱动已落地：Intel HDA（CORB/RIRB、codec/widget 枚举、BDL DMA + IOC 中断）、AC'97、SB16（ISA DMA）、ES1370、virtio-snd、USB 音频（新 UHCI 主机栈），snd_* 扩展接口，真 DMA 真中断；系统内更新、网卡/存储驱动保持；SSH + TLS 1.3 / TLS 1.2 + HTTPS 保持主流化。BIOS + UEFI 双引导，68,975 行源码，111 个 L1 扩展接口，146 条 shell 命令，18/18 QEMU 回归。Apache 2.0。",
  metadataBase: new URL("https://cubestudio-dev.github.io"),
  openGraph: {
    title: "Open Cube OS - 开源 x86_64 L0 内核",
    description:
      "开源 x86_64 内核（L0），可被扩展成任何东西。WP-10c 声卡驱动：Intel HDA / AC'97 / SB16 / ES1370 / virtio-snd / USB 音频 + snd_* 扩展接口，真 DMA 真中断；系统内更新（A/B 分区 + tar.gz 包 + 自动回滚）；网卡九族驱动 + nic_*；存储四驱动 + blk_*；SSH + TLS 1.3 / TLS 1.2 + HTTPS + crypto 核心 + TCP 可靠性 + netfilter。Apache 2.0。",
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
