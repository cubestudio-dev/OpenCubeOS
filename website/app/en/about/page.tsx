// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 cubestudio-dev <cubestudio@qq.com>

import type { Metadata } from "next";
import AboutView from "@/app/_components/AboutView";

export const metadata: Metadata = {
  title: "About - Open Cube OS",
  description:
    "Open Cube OS project history: 16 work packages WP-01 through WP-AUDIT-01, a 607-finding full audit with all 41 P0 fixes landed (p0fix1 first 20 + p0fix2 remaining 21) each individually verified, AI disclosure and license.",
};

export default function EnAboutPage() {
  return <AboutView locale="en" />;
}
