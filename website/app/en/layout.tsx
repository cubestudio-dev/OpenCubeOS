// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 cubestudio-dev <cubestudio@qq.com>

import type { Metadata } from "next";
import Chrome from "@/app/_components/Chrome";

export const metadata: Metadata = {
  title: "Open Cube OS - Open-Source x86_64 L0 Kernel",
  description:
    "Open Cube OS: an open-source x86_64 kernel (L0) that can be extended into anything. WP-10u in-system updates have landed: A/B partitions, tar.gz packages (kernel-side gzip/DEFLATE + ustar + SHA256), boot-flag automatic rollback and offline updates; NIC/storage drivers plus SSH + TLS 1.3 / TLS 1.2 + HTTPS remain; BIOS + UEFI dual boot, 63,644 lines of source, 85 L1 extension interfaces, 129 shell commands, 18/18 QEMU regression. Apache 2.0.",
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
