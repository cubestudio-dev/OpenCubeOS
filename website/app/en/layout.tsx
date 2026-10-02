// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 cubestudio-dev <cubestudio@qq.com>

import type { Metadata } from "next";
import Chrome from "@/app/_components/Chrome";

export const metadata: Metadata = {
  title: "Open Cube OS - Open-Source x86_64 L0 Kernel",
  description:
    "Open Cube OS: an open-source x86_64 kernel (L0) that can be extended into anything. WP-10b NIC drivers have landed: e1000e / igb / RTL8139 verified live in QEMU (DHCP, ping, HTTPS full chain), with ixgbe / RTL8168 / RTL8125 / RTL810x / BCM57xx plus 3c59x, nForce, AR81xx, Yukon detection; the four storage drivers AHCI/NVMe/ATA DMA/virtio-blk remain; SSH + TLS 1.3 / TLS 1.2 + HTTPS, TCP reliability (CUBIC/SACK) and netfilter, in-kernel configuration and update check. BIOS + UEFI dual boot, 59,662 lines of source, 78 L1 extension interfaces, 116 shell commands, 18/18 QEMU regression. Apache 2.0.",
  alternates: { canonical: "/OpenCubeOS/en/" },
};

export default function EnLayout({
  children,
}: Readonly<{ children: React.ReactNode }>) {
  return (
    <>
      {/* set <html lang> during parse (root layout is shared and defaults
          to zh-CN) */}
      <script
        dangerouslySetInnerHTML={{
          __html: `document.documentElement.lang="en"`,
        }}
      />
      <Chrome locale="en">{children}</Chrome>
    </>
  );
}
