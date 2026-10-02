// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 cubestudio-dev <cubestudio@qq.com>

import type { Metadata } from "next";
import AboutView from "@/app/_components/AboutView";

export const metadata: Metadata = {
  title: "About - Open Cube OS",
  description:
    "Open Cube OS project history: 12 work packages WP-01 through WP-10u, 120 audit bugs fixed, 18/18 regression verification, AI disclosure and license.",
};

export default function EnAboutPage() {
  return <AboutView locale="en" />;
}
