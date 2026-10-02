// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 cubestudio-dev <cubestudio@qq.com>

import type { Metadata } from "next";
import Chrome from "@/app/_components/Chrome";

export const metadata: Metadata = {
  title: "Open Cube OS - Open-Source x86_64 L0 Kernel",
  description:
    "Open Cube OS: an open-source x86_64 kernel (L0) that can be extended into anything. WP-10a storage drivers have landed: AHCI SATA, NVMe (two I/O queue pairs), ATA Bus-Master DMA and virtio-blk, with FAT32 mounting on all four block-device types (MBR + GPT); SSH + TLS 1.3 / TLS 1.2 + HTTPS remain mainstreamed. BIOS + UEFI dual boot, 56,019 lines of source, 65 L1 extension interfaces, 98 shell commands, 18/18 QEMU regression. Apache 2.0.",
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
