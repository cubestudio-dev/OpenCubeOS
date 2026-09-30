"use client";

// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 cubestudio-dev <cubestudio@qq.com>
//
// Site root: pick a locale once (saved choice wins, then browser language,
// default zh) and replace to /<locale>/ . Static-export compatible: this is
// the only client-side redirect; every real page lives under /zh/ or /en/.

import { useEffect, useState } from "react";
import { BASE } from "@/lib/site";
import { isLocale, type Locale } from "@/lib/i18n";

const LS_KEY = "oc-lang";

function pickLocale(): Locale {
  try {
    const saved = localStorage.getItem(LS_KEY);
    if (saved && isLocale(saved)) return saved;
  } catch {
    /* storage unavailable */
  }
  if (typeof navigator !== "undefined" && navigator.language) {
    return navigator.language.toLowerCase().startsWith("en") ? "en" : "zh";
  }
  return "zh";
}

export default function RootRedirect() {
  const [target, setTarget] = useState<string | null>(null);

  useEffect(() => {
    const lang = pickLocale();
    try {
      localStorage.setItem(LS_KEY, lang);
    } catch {
      /* ignore */
    }
    const url = `${BASE}/${lang}/`;
    setTarget(url);
    window.location.replace(url);
  }, []);

  return (
    <div className="container redirect-box" role="status">
      <p>正在进入 Open Cube OS 网站… / Entering the Open Cube OS site…</p>
      <noscript>
        <p>
          未启用 JavaScript，请选择语言 / JavaScript is disabled — please pick
          a language:
        </p>
        <p>
          <a href={`${BASE}/zh/`}>中文</a> ·{" "}
          <a href={`${BASE}/en/`}>English</a>
        </p>
      </noscript>
      {target && (
        <p>
          <a href={target}>{target}</a>
        </p>
      )}
    </div>
  );
}
