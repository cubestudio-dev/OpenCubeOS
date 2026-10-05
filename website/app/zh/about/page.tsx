// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 cubestudio-dev <cubestudio@qq.com>

import type { Metadata } from "next";
import AboutView from "@/app/_components/AboutView";

export const metadata: Metadata = {
  title: "关于 - Open Cube OS",
  description:
    "Open Cube OS 项目历史：WP-01 到 WP-AUDIT-01 共 16 个工作包，完整审查 607 条发现并落地全部 41 个 P0 修复（p0fix1 前 20 + p0fix2 后 21）、逐条验证，AI 披露与许可证。",
};

export default function ZhAboutPage() {
  return <AboutView locale="zh" />;
}
