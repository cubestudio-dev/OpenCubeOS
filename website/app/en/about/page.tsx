// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 cubestudio-dev <cubestudio@qq.com>

import type { Metadata } from "next";
import AboutView from "@/app/_components/AboutView";

export const metadata: Metadata = {
  title: "About - Open Cube OS",
  description:
    "Open Cube OS project history: 15 work packages (WP-01 through WP-10-wp08fix1) plus the auxiliary audit batches: a 607-finding full audit with all 41 P0 fixes, all 94 P1 fixes and both 53-item P2 batches (fix1 + fix2, with the fix2b full payload re-verification + embed-chain repair) landed, each individually verified, AI disclosure and license.",
};

export default function EnAboutPage() {
  return <AboutView locale="en" />;
}
