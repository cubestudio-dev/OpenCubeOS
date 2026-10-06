// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 cubestudio-dev <cubestudio@qq.com>

import type { Metadata } from "next";
import Chrome from "@/app/_components/Chrome";

export const metadata: Metadata = {
  title: "Open Cube OS - 开源 x86_64 L0 内核",
  description:
    "Open Cube OS：开源 x86_64 内核（L0），可被扩展成任何东西。WP-AUDIT-01 完成 18 路逐行完整审查（607 条发现）：41 个 P0 全部修复（p0fix1 前 20 + p0fix2 后 21），WP-AUDIT-01-p1fix1 修复前 31 个 P1（BUG-0042..0072）+ p1fix2 修复第 32~62 条（BUG-0073..0103：SSH TOFU 主机密钥锚点与按机密钥、TLS 套件密钥长度/transcript/边界、shell capture 栈与通配符、ush 重定向 fd 生命周期、L1 hook 契约与真实注册：每任务 FPU/SSE 上下文 + CR4.OSFXSR + fork 继承、PMM 位图原子性、页错误语义、XHCI/EHCI/OHCI/MSC/CDC-ACM/SS EP0、FAT32/exFAT/ext4 越界与生命周期、e1000、ld_so、网络栈九项加固），逐条有 QEMU/ASAN/构造级验证；WP-10-wp08fix1 Shell 补全（oc>/ush 双端行编辑 + 15 工具 + nano/vi 编辑器）；USB 四控制器、声卡六族、系统内 A/B 更新、网卡九族/存储四驱动；SSH + TLS 1.3 / TLS 1.2 + HTTPS；BIOS + UEFI 双引导，93,923 行源码，138 个 L1 扩展接口，44 个系统调用，173 条 shell 命令，41/41 P0 修复测试。Apache 2.0。",
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
