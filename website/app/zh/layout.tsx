// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 cubestudio-dev <cubestudio@qq.com>

import type { Metadata } from "next";
import Chrome from "@/app/_components/Chrome";

export const metadata: Metadata = {
  title: "Open Cube OS - 开源 x86_64 L0 内核",
  description:
    "Open Cube OS：开源 x86_64 内核（L0），可被扩展成任何东西。WP-09 安全传输层已主流化：SSH（curve25519、主机密钥验证、公钥认证）+ TLS 1.3 / TLS 1.2（ECDHE-GCM、CA 链验证）+ HTTPS + crypto 核心，TCP 可靠性（CUBIC/SACK）与 netfilter，系统内配置与检查更新。BIOS + UEFI 双引导，54,621 行源码，57 个 L1 扩展接口，87 条 shell 命令，18/18 QEMU 回归。Apache 2.0。",
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
