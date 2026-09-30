// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 cubestudio-dev <cubestudio@qq.com>

import {
  GITHUB_REPO,
  ISO_FILE,
  ISO_SHA256,
  ISO_SIZE_B,
  ISO_SIZE_MB,
  ISO_URL,
  RELEASE_WP09,
  SRC_FILE,
  SRC_SHA256,
  SRC_SIZE_B,
  SRC_SIZE_MB,
  SRC_URL,
} from "@/lib/site";
import { T, localePath, type Locale } from "@/lib/i18n";
import {
  STATS,
  FEATURES,
  BASE_KERNEL,
  WORK_PACKAGES,
  VERIFY,
  pick,
  fmtBytes,
} from "@/lib/home";

export default function HomeView({ locale }: { locale: Locale }) {
  const t = T[locale];
  const stats = pick(STATS, locale);
  const feats = pick(FEATURES, locale);
  const base = pick(BASE_KERNEL, locale);
  const wps = pick(WORK_PACKAGES, locale);
  const verify = pick(VERIFY, locale);

  return (
    <div className="container">
      {/* Hero */}
      <section className="hero">
        <span className="badge">{t.badge}</span>
        <h1 className="hero-title">
          {t.heroTitleLine1}
          <br />
          {t.heroTitleLine2}
        </h1>
        <p className="hero-sub">
          {t.heroSubPre}
          <b>{t.heroSubQuoted}</b>
          {t.heroSubMid}
          <b>{t.heroSubBold}</b>
          {t.heroSubPost}
          <b>{t.heroSubBold2}</b>
          {t.heroSubEnd}
        </p>
        <div className="cta-row">
          <a href={ISO_URL} className="btn btn-primary">
            {t.downloadIso(ISO_SIZE_MB)}
          </a>
          <a href={localePath(locale, "docs")} className="btn">
            {t.readDocs}
          </a>
          <a
            href={GITHUB_REPO}
            target="_blank"
            rel="noopener noreferrer"
            className="btn"
          >
            {t.githubRepo}
          </a>
        </div>

        {/* Stats */}
        <div className="stats">
          {stats.map((s) => (
            <div className="stat" key={s.label}>
              <b>{s.value}</b>
              <span>{s.label}</span>
            </div>
          ))}
        </div>
      </section>

      {/* Downloads */}
      <section id="downloads">
        <div className="sec-head">
          <h2>{t.dlSectionTitle}</h2>
          <p>{t.dlSectionDesc}</p>
        </div>
        <div className="dl-grid">
          <div className="dl-card">
            <h3>{ISO_FILE}</h3>
            <div className="dl-meta">{t.isoMeta(fmtBytes(ISO_SIZE_B), ISO_SIZE_MB)}</div>
            <div className="sha-box">
              <b>SHA256</b>
              <br />
              {ISO_SHA256}
            </div>
            <div className="dl-actions">
              <a href={ISO_URL} className="btn btn-primary">
                {t.siteDlIso}
              </a>
              <a
                href={RELEASE_WP09}
                target="_blank"
                rel="noopener noreferrer"
                className="btn"
              >
                {t.ghRelease}
              </a>
            </div>
          </div>
          <div className="dl-card">
            <h3>{SRC_FILE}</h3>
            <div className="dl-meta">{t.srcMeta(fmtBytes(SRC_SIZE_B), SRC_SIZE_MB)}</div>
            <div className="sha-box">
              <b>SHA256</b>
              <br />
              {SRC_SHA256}
            </div>
            <div className="dl-actions">
              <a href={SRC_URL} className="btn btn-primary">
                {t.siteDlSrc}
              </a>
              <a
                href={RELEASE_WP09}
                target="_blank"
                rel="noopener noreferrer"
                className="btn"
              >
                {t.ghRelease}
              </a>
            </div>
          </div>
        </div>
        <p className="dl-note">
          {t.dlNotePre}
          <code className="inline">sha256sum opencube-wp09.iso</code>
          {t.dlNotePost}
        </p>
      </section>

      {/* WP-09 features */}
      <section>
        <div className="sec-head">
          <h2>{t.featSectionTitle}</h2>
          <p>{t.featSectionDesc}</p>
        </div>
        <div className="feat-grid">
          {feats.map((f) => (
            <div className="feat" key={f.name}>
              <h3>{f.name}</h3>
              <p>{f.desc}</p>
              <span className="tag">{f.tag}</span>
            </div>
          ))}
        </div>
      </section>

      {/* Base kernel */}
      <section>
        <div className="sec-head">
          <h2>{t.baseSectionTitle}</h2>
          <p>{t.baseSectionDesc}</p>
        </div>
        <div className="base-list">
          {base.map((b) => (
            <div className="base-item" key={b.k}>
              <span className="k">{b.k}</span>
              <span className="v">{b.v}</span>
            </div>
          ))}
        </div>
      </section>

      {/* Work packages */}
      <section>
        <div className="sec-head">
          <h2>{t.wpSectionTitle}</h2>
          <p>{t.wpSectionDesc}</p>
        </div>
        <table className="wp-table">
          <thead>
            <tr>
              <th>{t.wpThNo}</th>
              <th>{t.wpThContent}</th>
              <th>{t.wpThStatus}</th>
            </tr>
          </thead>
          <tbody>
            {wps.map((w) => (
              <tr key={w.no}>
                <td>{w.no}</td>
                <td>{w.title}</td>
                <td className="done">done</td>
              </tr>
            ))}
          </tbody>
        </table>
      </section>

      {/* Verification */}
      <section>
        <div className="sec-head">
          <h2>{t.verifySectionTitle}</h2>
          <p>
            {t.verifySectionPre}
            <a
              href={`${GITHUB_REPO}/blob/main/docs/VERIFICATION_BATCH_B.md`}
              target="_blank"
              rel="noopener noreferrer"
            >
              docs/VERIFICATION_BATCH_B.md
            </a>
            {t.verifySectionPost}
          </p>
        </div>
        <div className="verify-list">
          {verify.map((v) => (
            <div className="verify-item" key={v.name}>
              <span className="ok">PASS</span> · <b>{v.name}</b>
              <br />
              {v.desc}
            </div>
          ))}
        </div>
      </section>
    </div>
  );
}
