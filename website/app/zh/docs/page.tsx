// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 cubestudio-dev <cubestudio@qq.com>

import type { Metadata } from "next";
import DocsView from "@/app/_components/DocsView";

export const metadata: Metadata = {
  title: "文档 - Open Cube OS",
  description:
    "Open Cube OS WP-09 文档：README（项目定位与统计）、BUILD（构建指南）、INTERFACES（57 个 L1 接口 + WP-09 传输 API 索引）。中文翻译版。",
};

export default function ZhDocsPage() {
  return <DocsView locale="zh" />;
}
