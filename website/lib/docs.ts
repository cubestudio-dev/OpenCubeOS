// Build-time helpers: read real repository documents for the /docs page.
// Paths resolve relative to website/ (this app lives inside the oc-os repo,
// so ../README.md is the repo root README).

import { readFileSync } from "fs";
import path from "path";

export interface DocSection {
  id: string;
  title: string;
  source: string;
  body: string;
}

function readRepoDoc(relFromWebsite: string, fallback: string): string {
  try {
    return readFileSync(
      path.join(process.cwd(), relFromWebsite),
      "utf8",
    ).trimEnd();
  } catch {
    return fallback;
  }
}

export function getDocSections(): DocSection[] {
  const ghRepo = "https://github.com/cubestudio-dev/OpenCubeOS";
  return [
    {
      id: "readme",
      title: "README",
      source: "README.md（仓库根）",
      body: readRepoDoc(
        "../README.md",
        `文档读取失败。请到仓库查看：${ghRepo}/blob/main/README.md`,
      ),
    },
    {
      id: "build",
      title: "BUILD",
      source: "docs/BUILD.md",
      body: readRepoDoc(
        "../docs/BUILD.md",
        `文档读取失败。请到仓库查看：${ghRepo}/blob/main/docs/BUILD.md`,
      ),
    },
    {
      id: "interfaces",
      title: "INTERFACES",
      source: "docs/INTERFACES.md",
      body: readRepoDoc(
        "../docs/INTERFACES.md",
        `文档读取失败。请到仓库查看：${ghRepo}/blob/main/docs/INTERFACES.md`,
      ),
    },
  ];
}
