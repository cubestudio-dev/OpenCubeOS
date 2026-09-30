"use client";

// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 cubestudio-dev <cubestudio@qq.com>

import { usePathname } from "next/navigation";
import { BASE } from "@/lib/site";
import type { Locale } from "@/lib/i18n";

const LS_KEY = "oc-lang";

function currentLocale(pathname: string): Locale {
  return pathname.includes(`/${"zh"}/`) ? "zh" : "en";
}

function swapLocale(pathname: string, to: Locale): string {
  // /OpenCubeOS/zh/docs/ -> /OpenCubeOS/en/docs/  (same depth, same page)
  const parts = pathname.split("/"); // ["", "OpenCubeOS", "zh", "docs", ""]
  const i = parts.findIndex((p) => p === "zh" || p === "en");
  if (i >= 0) {
    parts[i] = to;
    return parts.join("/") || "/";
  }
  // Path without a locale segment (e.g. legacy /OpenCubeOS/docs/): go to
  // the target-locale home. Keep a sub-path hint if recognisable.
  const sub = pathname.includes("/docs")
    ? "docs"
    : pathname.includes("/about")
      ? "about"
      : "";
  const base = pathname.replace(/\/(docs|about)\/?$/, "").replace(/\/$/, "");
  const next = `${base}/${to}${sub ? `/${sub}` : ""}/`;
  return next.startsWith("//") ? next.slice(1) : next;
}

export default function LangSwitch() {
  const pathname = usePathname() || "/";
  const from = currentLocale(pathname);
  const to: Locale = from === "zh" ? "en" : "zh";

  function go() {
    try {
      localStorage.setItem(LS_KEY, to);
    } catch {
      /* storage unavailable — still navigate */
    }
    // usePathname() strips the basePath (/OpenCubeOS), so the swapped path
    // must be re-prefixed before assigning — otherwise GH Pages 404s.
    window.location.assign(`${BASE}${swapLocale(pathname, to)}`);
  }

  return (
    <button
      type="button"
      onClick={go}
      className="lang-btn"
      aria-label={from === "zh" ? "Switch to English" : "切换到中文"}
      title={from === "zh" ? "Switch to English" : "切换到中文"}
    >
      {from === "zh" ? "EN" : "中文"}
    </button>
  );
}
