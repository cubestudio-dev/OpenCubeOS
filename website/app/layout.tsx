// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 cubestudio-dev <cubestudio@qq.com>

import type { Metadata } from "next";
import { SITE_URL } from "@/lib/site";
import "./globals.css";

export const metadata: Metadata = {
  title: "Open Cube OS - 开源 x86_64 L0 内核",
  description:
    "Open Cube OS：开源 x86_64 内核（L0），可被扩展成任何东西。WP-AUDIT-01 完成 18 路逐行完整审查（607 条发现）：41 个 P0 全部修复（p0fix1 前 20 + p0fix2 后 21），P1 全部 94 条修复完成（p1fix1 前 31 条 + p1fix2 第 32~62 条 + p1fix3 剩余 31 条：A/B 更新目标槽修正、OTA 整数/栈加固、恒定时间 EC 标量乘与 modexp、GPT/NVMe/ATA 边界、回写缓存不再静默丢数据、umount / 防护、PCI BAR 校验、网卡探测/假发送/描述符布局/EOR/回收门、virtio-snd 能力解析、HDA 音量真生效、sshd 阻塞 accept 可 ^C 取消、调度器 100ms 交互上隀 + kill 就绪 tid、真正的双模式 vi）；p1fix4 修复 HTTPS E2E 中途断连（IP 字面量参考身份，RFC 6125，OpenSSL 真对端实测握手/GET/保存/关闭全通），每条修复均有 QEMU/宿主/构造级验证；WP-10-wp08fix1 Shell 补全（oc>/ush 双端行编辑 + 15 工具 + nano/vi 编辑器）；USB 四控制器、声卡六族、系统内 A/B 更新、网卡九族/存储四驱动；SSH + TLS 1.3/1.2 + HTTPS。P2 第一批 53 条（WP-10-AUDIT_P2-fix1，BUG-0136..0188）已全部修复：内存/arch/USB 33 条/VFS-FAT 11 条，并发现修复堆块头 40 字节潜伏缺陷（负载对齐漂移致 ATA-DMA 偏移 8 字节、/etc 无法挂载）；BIOS + UEFI 双引导，P2 第二批 53 条（WP-10-AUDIT_P2-fix2，BUG-0189..0241）已全部修复：fs 12 条 / net_core 13 条 / ld_so 4 条 / ssh 10 条 / tls 8 条 / shell 6 条，并完成发布链四项加固（git describe 版本串、HTTPS E2E 双向 close_notify 断言、INTERFACES.md 176 命令、docs/verification/ 转公开）与嵌入链 Makefile 规则；100,230 行源码，138 个 L1 扩展接口，44 个系统调用，176 条 shell 命令，41/41 P0 修复测试。Apache 2.0。",
  metadataBase: new URL("https://cubestudio-dev.github.io"),
  openGraph: {
    title: "Open Cube OS - 开源 x86_64 L0 内核",
    description:
      "开源 x86_64 内核（L0），可被扩展成任何东西。WP-AUDIT-01：18 路完整审查（607 条）+ 41 个 P0 全部修复（p0fix1 前 20 + p0fix2 后 21）+ P1 全部 94 条修复完成（p1fix1 前 31 条 + p1fix2 第 32~62 条 + p1fix3 剩余 31 条），逐条验证；WP-10-wp08fix1：Shell 完全体——oc>/ush 双端全键位行编辑、ush 15 个工具、nano/vi 编辑器、9 个新 L1 接口（130-138，总数 138）、173 条命令、24/24 端到端；USB 主机栈四控制器、声卡六族 + snd_*、系统内 A/B 更新、网卡九族 + nic_*、存储四驱动 + blk_*；SSH + TLS 1.3 / TLS 1.2 + HTTPS + crypto 核心 + TCP 可靠性 + netfilter。Apache 2.0。",
    url: SITE_URL,
    siteName: "Open Cube OS",
    type: "website",
  },
};

// Single root layout (App Router requirement). Static export has exactly
// one <html>, so per-locale lang is set by a tiny inline script in each
// locale layout (no hydration flash: it runs during parse).
export default function RootLayout({
  children,
}: Readonly<{ children: React.ReactNode }>) {
  return (
    <html lang="zh-CN">
      <body>{children}</body>
    </html>
  );
}
