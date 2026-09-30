// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 cubestudio-dev <cubestudio@qq.com>

import type { Metadata } from "next";
import DocsView from "@/app/_components/DocsView";

export const metadata: Metadata = {
  title: "Docs - Open Cube OS",
  description:
    "Open Cube OS WP-09 docs: README (positioning and stats), BUILD (build guide), INTERFACES (57 L1 interfaces + WP-09 transport API index). Original English text from the repository.",
};

export default function EnDocsPage() {
  return <DocsView locale="en" />;
}
