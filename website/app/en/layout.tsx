// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 cubestudio-dev <cubestudio@qq.com>

import type { Metadata } from "next";
import Chrome from "@/app/_components/Chrome";

export const metadata: Metadata = {
  title: "Open Cube OS - Open-Source x86_64 L0 Kernel",
  description:
    "Open Cube OS: an open-source x86_64 kernel (L0) that can be extended into anything. WP-AUDIT-01 completed an 18-route line-by-line audit (607 findings): all 41 P0 fixes landed (p0fix1 first 20 + p0fix2 remaining 21), and WP-AUDIT-01-p1fix1 fixed the first 31 P1s (BUG-0042..0072) and p1fix2 items 32..62 (BUG-0073..0103: SSH TOFU anchor + per-installation host key, TLS key lengths/transcript/bounds, shell capture stack + wildcards, ush redirect fd lifecycle, L1 hook contract + real registration): per-task FPU/SSE context + CR4.OSFXSR + fork inheritance, PMM bitmap atomicity, page-fault semantics, XHCI/EHCI/OHCI/MSC/CDC-ACM/SS EP0, FAT32/exFAT/ext4 bounds and lifecycle, e1000, ld_so, nine network-stack hardenings), each verified with QEMU/ASAN/construction-level evidence; WP-10-wp08fix1 shell completion (oc>/ush line editing + 15 tools + the nano/vi editor); four USB controllers, six sound-card families, in-system A/B updates, nine NIC families and four storage drivers; SSH + TLS 1.3 / TLS 1.2 + HTTPS; BIOS + UEFI dual boot, 93,923 lines of source, 138 L1 extension interfaces, 44 system calls, 173 shell commands, 41/41 P0 fix tests. Apache 2.0.",
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
