import type { Metadata } from "next";
import { BASE } from "@/lib/site";
import { getDocSections } from "@/lib/docs";

export const metadata: Metadata = {
  title: "文档 - Open Cube OS",
  description:
    "Open Cube OS WP-09 文档：README（项目定位与统计）、BUILD（构建指南）、INTERFACES（57 个 L1 接口 + WP-09 传输 API 索引）。",
};

export default function DocsPage() {
  const sections = getDocSections();
  return (
    <div className="container">
      <div className="page-head">
        <h1>文档</h1>
        <p>
          以下内容在构建时直接读取自仓库真实文档文件，原样展示、一字未改。
          如需查看最新版本，请访问{" "}
          <a
            href="https://github.com/cubestudio-dev/OpenCubeOS"
            target="_blank"
            rel="noopener noreferrer"
          >
            GitHub 仓库
          </a>
          。
        </p>
      </div>
      <nav className="docs-nav" aria-label="文档目录">
        {sections.map((s) => (
          <a href={`#${s.id}`} key={s.id}>
            {s.title}
          </a>
        ))}
        <a href={`${BASE}/`}>← 返回首页</a>
      </nav>
      {sections.map((s) => (
        <section className="doc-block" id={s.id} key={s.id}>
          <h2>{s.title}</h2>
          <div className="doc-src">来源：{s.source}</div>
          <pre className="doc-pre">{s.body}</pre>
        </section>
      ))}
    </div>
  );
}
