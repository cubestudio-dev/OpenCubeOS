// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 cubestudio-dev <cubestudio@qq.com>

import type { Metadata } from "next";
import AboutView from "@/app/_components/AboutView";

export const metadata: Metadata = {
  title: "关于 - Open Cube OS",
  description:
    "Open Cube OS 项目历史：WP-01 到 WP-10u 共 12 个工作包、120 个审计修复、18/18 回归验证、AI 披露与许可证。",
};

export default function ZhAboutPage() {
  return <AboutView locale="zh" />;
}
