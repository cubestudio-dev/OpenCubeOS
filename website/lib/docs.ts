// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 cubestudio-dev <cubestudio@qq.com>
//
// Build-time helpers for the /docs pages.
//  - locale "en": read the real repository documents verbatim
//    (<repo>/README.md, <repo>/docs/BUILD.md, <repo>/docs/INTERFACES.md)
//  - locale "zh": read the website-provided Chinese translations
//    (<website>/docs-zh/...) — the repository originals are authoritative.
//
// The build worker's process.cwd() is not guaranteed to be website/, so the
// website root is derived: if cwd already ends with "website" use it,
// otherwise assume cwd is the repo root and append "website".

import { readFileSync } from "fs";
import path from "path";
import type { Locale } from "@/lib/i18n";

export interface DocSection {
  id: string;
  title: string;
  source: string;
  body: string;
}

function readDoc(locale: Locale, repoRel: string, siteRel: string, fallback: string): string {
  // locale "en" reads ONLY the repository originals (repoRel, from repo root);
  // locale "zh" reads ONLY the website translations (siteRel, from website/).
  // The two bases below handle both cwd cases (cwd=website or cwd=repo root).
  const cwd = process.cwd();
  const isWebsiteCwd = path.basename(cwd) === "website";
  const repoBase = isWebsiteCwd ? path.join(cwd, "..") : cwd;
  const siteBase = isWebsiteCwd ? cwd : path.join(cwd, "website");
  const file =
    locale === "zh"
      ? path.join(siteBase, siteRel)
      : path.join(repoBase, repoRel);
  try {
    return readFileSync(file, "utf8").trimEnd();
  } catch {
    return fallback;
  }
}

export function getDocSections(locale: Locale): DocSection[] {
  const ghRepo = "https://github.com/cubestudio-dev/OpenCubeOS";
  const zh = locale === "zh";

  return [
    {
      id: "readme",
      title: "README",
      source: zh
        ? "README.md 中文翻译（原文件：仓库根 README.md）"
        : "README.md（仓库根）",
      body: readDoc(
        locale,
        "README.md",
        "docs-zh/README.md",
        zh
          ? `文档读取失败。请到仓库查看英文原文：${ghRepo}/blob/main/README.md`
          : `Failed to read the document. See the repository: ${ghRepo}/blob/main/README.md`,
      ),
    },
    {
      id: "build",
      title: "BUILD",
      source: zh ? "BUILD.md 中文翻译（原文件：docs/BUILD.md）" : "docs/BUILD.md",
      body: readDoc(
        locale,
        "docs/BUILD.md",
        "docs-zh/BUILD.md",
        zh
          ? `文档读取失败。请到仓库查看英文原文：${ghRepo}/blob/main/docs/BUILD.md`
          : `Failed to read the document. See the repository: ${ghRepo}/blob/main/docs/BUILD.md`,
      ),
    },
    {
      id: "interfaces",
      title: "INTERFACES",
      source: zh
        ? "INTERFACES.md 中文翻译（原文件：docs/INTERFACES.md）"
        : "docs/INTERFACES.md",
      body: readDoc(
        locale,
        "docs/INTERFACES.md",
        "docs-zh/INTERFACES.md",
        zh
          ? `文档读取失败。请到仓库查看英文原文：${ghRepo}/blob/main/docs/INTERFACES.md`
          : `Failed to read the document. See the repository: ${ghRepo}/blob/main/docs/INTERFACES.md`,
      ),
    },
  ];
}
