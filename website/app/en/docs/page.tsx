// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 cubestudio-dev <cubestudio@qq.com>

import type { Metadata } from "next";
import DocsView from "@/app/_components/DocsView";

export const metadata: Metadata = {
  title: "Docs - Open Cube OS",
  description:
    "Open Cube OS docs: README (positioning and stats, WP-AUDIT-01-p1fix1), BUILD (build guide), INTERFACES (the L1 interface index + the shell command surface, 173 commands). Original English text from the repository.",
};

export default function EnDocsPage() {
  return <DocsView locale="en" />;
}
