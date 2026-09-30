// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 cubestudio-dev <cubestudio@qq.com>

import { GITHUB_REPO } from "@/lib/site";
import { T, localePath, type Locale } from "@/lib/i18n";
import { getDocSections } from "@/lib/docs";

export default function DocsView({ locale }: { locale: Locale }) {
  const t = T[locale];
  const sections = getDocSections(locale);
  return (
    <div className="container">
      <div className="page-head">
        <h1>{t.docsTitle}</h1>
        <p>
          {t.docsDescPre}
          <a
            href={GITHUB_REPO}
            target="_blank"
            rel="noopener noreferrer"
          >
            {t.githubRepo}
          </a>
          {t.docsDescPost}
        </p>
        <p className="docs-lang-note">{t.docsLangNote}</p>
      </div>
      <nav className="docs-nav" aria-label={t.docsNavAria}>
        {sections.map((s) => (
          <a href={`#${s.id}`} key={s.id}>
            {s.title}
          </a>
        ))}
        <a href={localePath(locale)}>{t.docsBack}</a>
      </nav>
      {sections.map((s) => (
        <section className="doc-block" id={s.id} key={s.id}>
          <h2>{s.title}</h2>
          <div className="doc-src">
            {t.docsSourcePrefix}
            {s.source}
          </div>
          <pre className="doc-pre">{s.body}</pre>
        </section>
      ))}
    </div>
  );
}
