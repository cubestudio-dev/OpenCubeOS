import type { Metadata } from "next";
import "./globals.css";

export const metadata: Metadata = {
  title: "Open Cube OS - 开源 x86_64 L0 内核",
  description:
    "Open Cube OS - 一个能被扩展成任何东西的开源 x86_64 内核。L0 完整内核 + 恢复/管理 Shell，BIOS + UEFI 双引导，57 个 L1 扩展接口，37 个系统调用。WP-09 新增 SSH + TLS 1.2/HTTPS 安全传输层。Apache 2.0 协议。",
  keywords: ["Open Cube OS", "kernel", "x86_64", "L0", "SSH", "TLS", "HTTPS", "开源", "Apache 2.0"],
  authors: [{ name: "cubestudio-dev" }],
  openGraph: {
    title: "Open Cube OS - 开源 x86_64 L0 内核",
    description: "一个能被扩展成任何东西的开源 x86_64 内核。WP-09: SSH + TLS/HTTPS。Apache 2.0。",
    siteName: "Open Cube OS",
    type: "website",
  },
};

export default function RootLayout({
  children,
}: Readonly<{
  children: React.ReactNode;
}>) {
  return (
    <html lang="zh-CN" suppressHydrationWarning>
      <body className="antialiased">{children}</body>
    </html>
  );
}
