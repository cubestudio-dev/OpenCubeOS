// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 cubestudio-dev <cubestudio@qq.com>

import type { Metadata } from "next";
import Chrome from "@/app/_components/Chrome";

export const metadata: Metadata = {
  title: "Open Cube OS - 开源 x86_64 L0 内核",
  description:
    "Open Cube OS：开源 x86_64 内核（L0），可被扩展成任何东西。WP-10a 存储驱动已落地：AHCI SATA、NVMe（双 I/O 队列）、ATA Bus-Master DMA、virtio-blk，FAT32 可挂载全部四类块设备（MBR + GPT）；SSH + TLS 1.3 / TLS 1.2 + HTTPS 主流化，TCP 可靠性（CUBIC/SACK）与 netfilter，系统内配置与检查更新。BIOS + UEFI 双引导，56,019 行源码，65 个 L1 扩展接口，98 条 shell 命令，18/18 QEMU 回归。Apache 2.0。",
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
