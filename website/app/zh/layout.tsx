// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 cubestudio-dev <cubestudio@qq.com>

import type { Metadata } from "next";
import Chrome from "@/app/_components/Chrome";

export const metadata: Metadata = {
  title: "Open Cube OS - 开源 x86_64 L0 内核",
  description:
    "Open Cube OS：开源 x86_64 内核（L0），可被扩展成任何东西。WP-10b 网卡驱动已落地：e1000e / igb / RTL8139 QEMU 实测 DHCP、ping、HTTPS 全链路，ixgbe / RTL8168 / RTL8125 / RTL810x / BCM57xx 及 3c59x、nForce、AR81xx、Yukon 识别；存储四驱动 AHCI/NVMe/ATA DMA/virtio-blk 保持；SSH + TLS 1.3 / TLS 1.2 + HTTPS，TCP 可靠性（CUBIC/SACK）与 netfilter，系统内配置与检查更新。BIOS + UEFI 双引导，59,662 行源码，78 个 L1 扩展接口，116 条 shell 命令，18/18 QEMU 回归。Apache 2.0。",
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
