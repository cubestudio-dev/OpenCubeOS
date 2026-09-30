// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 cubestudio-dev <cubestudio@qq.com>

import type { ReactNode } from "react";
import { BASE, GITHUB_REPO } from "@/lib/site";
import { T, localePath, type Locale } from "@/lib/i18n";
import LangSwitch from "@/app/_components/LangSwitch";

// Shared header + footer for both locale layouts. Server component; the
// only client island is LangSwitch.
export default function Chrome({
  locale,
  children,
}: {
  locale: Locale;
  children: ReactNode;
}) {
  const t = T[locale];
  return (
    <>
      <header className="site-header">
        <div className="container header-inner">
          <a href={localePath(locale)} className="brand">
            <span className="brand-mark">OC</span>
            <span>
              Open Cube OS <small>{t.brandSub}</small>
            </span>
          </a>
          <nav className="nav" aria-label={t.navAria}>
            <a href={localePath(locale)}>{t.navHome}</a>
            <a href={`${localePath(locale)}#downloads`}>{t.navDownloads}</a>
            <a href={localePath(locale, "docs")}>{t.navDocs}</a>
            <a href={localePath(locale, "about")}>{t.navAbout}</a>
            <a href={GITHUB_REPO} target="_blank" rel="noopener noreferrer">
              {t.navGithub}
            </a>
            <LangSwitch />
          </nav>
        </div>
      </header>
      <main>{children}</main>
      <footer className="site-footer">
        <div className="container footer-inner">
          <span>{t.footer}</span>
          <span className="footer-links">
            <a href={GITHUB_REPO} target="_blank" rel="noopener noreferrer">
              {t.navGithub}
            </a>
            <a href={localePath(locale, "docs")}>{t.navDocs}</a>
            <a href={localePath(locale, "about")}>{t.navAbout}</a>
            <a href={`${BASE}/downloads/opencube-wp09.iso`}>{t.footerDlIso}</a>
          </span>
        </div>
      </footer>
    </>
  );
}
