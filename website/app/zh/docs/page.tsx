// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 cubestudio-dev <cubestudio@qq.com>

import type { Metadata } from "next";
import DocsView from "@/app/_components/DocsView";

export const metadata: Metadata = {
  title: "文档 - Open Cube OS",
  description:
    "Open Cube OS 文档：README（项目定位与统计，WP-10-AUDIT_P2-fix3）、BUILD（构建指南）、INTERFACES（L1 接口索引 + shell 命令表面，176 条命令）。中文翻译版。",
};

export default function ZhDocsPage() {
  return <DocsView locale="zh" />;
}
