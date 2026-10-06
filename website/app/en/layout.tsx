// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 cubestudio-dev <cubestudio@qq.com>

import type { Metadata } from "next";
import Chrome from "@/app/_components/Chrome";

export const metadata: Metadata = {
  title: "Open Cube OS - Open-Source x86_64 L0 Kernel",
  description:
    "Open Cube OS: an open-source x86_64 kernel (L0) that can be extended into anything. WP-AUDIT-01 completed an 18-route line-by-line audit (607 findings): all 41 P0 fixes landed (p0fix1 first 20 + p0fix2 remaining 21), and all 94 P1s are now fixed (p1fix1 items 1..31 + p1fix2 items 32..62 + p1fix3 items 63..94: A/B update targets the non-booted slot, OTA integer/stack hardening, constant-time EC scalar mult and modexp, GPT/NVMe/ATA bounds, write-back cache no longer drops data silently, umount / protection, PCI BAR validation, NIC probe/fake-send/descriptor-layout/EOR/reclaim fixes per datasheet, virtio-snd capability parsing, HDA volume actually works, Ctrl+C-cancellable sshd accept, 100 ms scheduler interactive ceiling + kill-ready tid, a real modal vi), each verified with QEMU/host/construction-level evidence; WP-10-wp08fix1 shell completion (oc>/ush line editing + 15 tools + the nano/vi editor); four USB controllers, six sound-card families, in-system A/B updates, nine NIC families and four storage drivers; SSH + TLS 1.3 / TLS 1.2 + HTTPS; BIOS + UEFI dual boot, 94,883 lines of source, 138 L1 extension interfaces, 44 system calls, 173 shell commands, 41/41 P0 fix tests. Apache 2.0.",
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
