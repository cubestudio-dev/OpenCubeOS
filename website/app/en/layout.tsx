// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 cubestudio-dev <cubestudio@qq.com>

import type { Metadata } from "next";
import Chrome from "@/app/_components/Chrome";

export const metadata: Metadata = {
  title: "Open Cube OS - Open-Source x86_64 L0 Kernel",
  description:
    "Open Cube OS: an open-source x86_64 kernel (L0) that can be extended into anything. The WP-09 secure-transport layer has been mainstreamed: SSH (curve25519, host-key verification, publickey auth) + TLS 1.3 / TLS 1.2 (ECDHE-GCM, CA chain verification) + HTTPS + crypto core, TCP reliability (CUBIC/SACK) and netfilter, in-system configuration and update check. BIOS + UEFI dual boot, 54,621 lines of source, 57 L1 extension interfaces, 87 shell commands, 18/18 QEMU regression. Apache 2.0.",
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
