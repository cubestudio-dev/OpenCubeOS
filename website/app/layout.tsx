// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 cubestudio-dev <cubestudio@qq.com>

import type { Metadata } from "next";
import { SITE_URL } from "@/lib/site";
import "./globals.css";

export const metadata: Metadata = {
  title: "Open Cube OS - 开源 x86_64 L0 内核",
  description:
    "Open Cube OS：开源 x86_64 内核（L0），可被扩展成任何东西。WP-10b 网卡驱动已落地：e1000e / igb / RTL8139 QEMU 实测 DHCP、ping、HTTPS 全链路，ixgbe / RTL8168 / RTL8125 / RTL810x / BCM57xx 及 3c59x、nForce、AR81xx、Yukon 识别；存储四驱动保持；SSH + TLS 1.3 / TLS 1.2 + HTTPS 保持主流化。BIOS + UEFI 双引导，59,662 行源码，78 个 L1 扩展接口，116 条 shell 命令，18/18 QEMU 回归。Apache 2.0。",
  metadataBase: new URL("https://cubestudio-dev.github.io"),
  openGraph: {
    title: "Open Cube OS - 开源 x86_64 L0 内核",
    description:
      "开源 x86_64 内核（L0），可被扩展成任何东西。WP-10b 网卡驱动：e1000e / igb / RTL8139 / ixgbe / RTL8168 / RTL8125 / RTL810x / BCM57xx + nic_* 扩展接口；存储四驱动 AHCI / NVMe / ATA DMA / virtio-blk + blk_*；SSH + TLS 1.3 / TLS 1.2 + HTTPS + crypto 核心 + TCP 可靠性 + netfilter。Apache 2.0。",
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
