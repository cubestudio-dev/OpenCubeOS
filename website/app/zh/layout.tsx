// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 cubestudio-dev <cubestudio@qq.com>

import type { Metadata } from "next";
import Chrome from "@/app/_components/Chrome";

export const metadata: Metadata = {
  title: "Open Cube OS - 开源 x86_64 L0 内核",
  description:
    "Open Cube OS：开源 x86_64 内核（L0），可被扩展成任何东西。WP-10u 系统内自动更新已落地：A/B 双分区、tar.gz 更新包（内核自带 gzip/DEFLATE + ustar + SHA256）、boot 标志自动回滚、离线更新；网卡/存储驱动与 SSH + TLS 1.3 / TLS 1.2 + HTTPS 保持；BIOS + UEFI 双引导，63,644 行源码，85 个 L1 扩展接口，129 条 shell 命令，18/18 QEMU 回归。Apache 2.0。",
  alternates: { canonical: "/OpenCubeOS/zh/" },
};

export default function ZhLayout({
  children,
}: Readonly<{ children: React.ReactNode }>) {
  return (
    <>
      {/* root <html lang> is already zh-CN; kept explicit for clarity */}
      <script
        dangerouslySetInnerHTML={{
          __html: `document.documentElement.lang="zh-CN"`,
        }}
      />
      <Chrome locale="zh">{children}</Chrome>
    </>
  );
}
