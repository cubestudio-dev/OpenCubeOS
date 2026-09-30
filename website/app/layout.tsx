import type { Metadata } from "next";
import { BASE, GITHUB_REPO, SITE_URL } from "@/lib/site";
import "./globals.css";

export const metadata: Metadata = {
  title: "Open Cube OS - 开源 x86_64 L0 内核",
  description:
    "Open Cube OS：开源 x86_64 内核（L0），可被扩展成任何东西。WP-09 新增 SSH + TLS 1.2 / HTTPS 安全传输层。BIOS + UEFI 双引导，46,058 行源码，57 个 L1 扩展接口，68 条 shell 命令，18/18 QEMU 回归。Apache 2.0。",
  metadataBase: new URL("https://cubestudio-dev.github.io"),
  openGraph: {
    title: "Open Cube OS - 开源 x86_64 L0 内核",
    description:
      "开源 x86_64 内核（L0），可被扩展成任何东西。WP-09：SSH + TLS 1.2/HTTPS + crypto 核心。Apache 2.0。",
    url: SITE_URL,
    siteName: "Open Cube OS",
    type: "website",
  },
};

export default function RootLayout({
  children,
}: Readonly<{ children: React.ReactNode }>) {
  return (
    <html lang="zh-CN">
      <body>
        <header className="site-header">
          <div className="container header-inner">
            <a href={`${BASE}/`} className="brand">
              <span className="brand-mark">OC</span>
              <span>
                Open Cube OS{" "}
                <small>
                  WP-09 · Apache 2.0
                </small>
              </span>
            </a>
            <nav className="nav" aria-label="站点导航">
              <a href={`${BASE}/`}>首页</a>
              <a href={`${BASE}/#downloads`}>下载</a>
              <a href={`${BASE}/docs/`}>文档</a>
              <a href={`${BASE}/about/`}>关于</a>
              <a href={GITHUB_REPO} target="_blank" rel="noopener noreferrer">
                GitHub
              </a>
            </nav>
          </div>
        </header>
        <main>{children}</main>
        <footer className="site-footer">
          <div className="container footer-inner">
            <span>
              © 2026 cubestudio-dev &lt;cubestudio@qq.com&gt; · Apache License
              2.0
            </span>
            <span className="footer-links">
              <a href={GITHUB_REPO} target="_blank" rel="noopener noreferrer">
                GitHub
              </a>
              <a href={`${BASE}/docs/`}>文档</a>
              <a href={`${BASE}/about/`}>关于</a>
              <a href={`${BASE}/downloads/opencube-wp09.iso`}>下载 ISO</a>
            </span>
          </div>
        </footer>
      </body>
    </html>
  );
}
