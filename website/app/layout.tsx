// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 cubestudio-dev <cubestudio@qq.com>

import type { Metadata } from "next";
import { SITE_URL } from "@/lib/site";
import "./globals.css";

export const metadata: Metadata = {
  title: "Open Cube OS - 开源 x86_64 L0 内核",
  description:
    "Open Cube OS：开源 x86_64 内核（L0），可被扩展成任何东西。WP-09 新增 SSH + TLS 1.2 / HTTPS 安全传输层。BIOS + UEFI 双引导，46,058 行源码，57 个 L1 扩展接口，68 条 shell 命令，18/18 QEMU 回归。Apache 2.0。",
  metadataBase: new URL("https://cubestudio-dev.github.io"),
  openGraph: {
    title: "Open Cube OS - 开源 x86_64 L0 内核",
    description:
      "开源 x86_64 内核（L0），可被扩展成任何东西。WP-09：SSH + TLS 1.2/HTTPS + crypto 核心。Apache 2.0。",
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
