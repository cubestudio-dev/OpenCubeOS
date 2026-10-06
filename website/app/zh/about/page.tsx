// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 cubestudio-dev <cubestudio@qq.com>

import type { Metadata } from "next";
import AboutView from "@/app/_components/AboutView";

export const metadata: Metadata = {
  title: "关于 - Open Cube OS",
  description:
    "Open Cube OS 项目历史：WP-01 到 WP-AUDIT-01 共 16 个工作包，完整审查 607 条发现、41 个 P0 全部修复、P1 修复推进中（p1fix1 前 31 条，BUG-0042..0072）、逐条验证，AI 披露与许可证。",
};

export default function ZhAboutPage() {
  return <AboutView locale="zh" />;
}
