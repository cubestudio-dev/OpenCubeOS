// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 cubestudio-dev <cubestudio@qq.com>

import type { Metadata } from "next";
import AboutView from "@/app/_components/AboutView";

export const metadata: Metadata = {
  title: "关于 - Open Cube OS",
  description:
    "Open Cube OS 项目历史：15 个工作包（WP-01 到 WP-10-wp08fix1）+ 辅助审计批次：18 路完整审查 607 条发现、41 个 P0 全部修复、94 个 P1 全部修复、P2 两批 106 条全部修复（fix1 53 条 + fix2 53 条，fix2b 全量复核 + 嵌入链修复），逐条验证，AI 披露与许可证。",
};

export default function ZhAboutPage() {
  return <AboutView locale="zh" />;
}
