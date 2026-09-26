import type { Metadata } from "next";
import { Geist, Geist_Mono } from "next/font/google";
import "./globals.css";
import { Toaster } from "@/components/ui/toaster";

const geistSans = Geist({
  variable: "--font-geist-sans",
  subsets: ["latin"],
});

const geistMono = Geist_Mono({
  variable: "--font-geist-mono",
  subsets: ["latin"],
});

export const metadata: Metadata = {
  title: "Open Cube OS - WP-08a 完整系统调用集",
  description: "Open Cube OS - 一个能被扩展成任何东西的内核。WP-08a: 真 POSIX fork、exec、wait、pipe、dup、signal、mmap、brk、chdir、ioctl、select、poll。23 个系统调用，8 个 L1 扩展接口。",
  keywords: ["Open Cube OS", "kernel", "WP-08a", "fork", "exec", "syscall", "pipe", "signal", "mmap", "select", "操作系统内核"],
  authors: [{ name: "cubestudio" }],
  icons: {
    icon: "/downloads/oc-icon.svg",
  },
  openGraph: {
    title: "Open Cube OS - WP-08a",
    description: "一个能被扩展成任何东西的内核。WP-08a 完整系统调用集。",
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
      <body
        className={`${geistSans.variable} ${geistMono.variable} antialiased bg-background text-foreground`}
      >
        {children}
        <Toaster />
      </body>
    </html>
  );
}
