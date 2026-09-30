// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 cubestudio-dev <cubestudio@qq.com>

import { GITHUB_REPO, RELEASES } from "@/lib/site";
import { T, localePath, type Locale } from "@/lib/i18n";
import { TIMELINE, pickAbout } from "@/lib/about";

export default function AboutView({ locale }: { locale: Locale }) {
  const t = T[locale];
  const timeline = pickAbout(TIMELINE, locale);
  return (
    <div className="container">
      <div className="page-head">
        <h1>{t.aboutTitle}</h1>
        <p>{t.aboutDesc}</p>
      </div>

      <div className="about-card">
        <h2>{t.aboutPositioning}</h2>
        <div className="quote">{t.aboutQuote}</div>
        <p>
          {t.aboutPositioningBodyPre}
          <b>{t.aboutPositioningL0}</b>
          {t.aboutPositioningL0Body}
          <b>{t.aboutPositioningL1}</b>
          {t.aboutPositioningL1Body}
        </p>
      </div>

      <div className="about-card">
        <h2>{t.aboutHistory}</h2>
        <div className="timeline">
          {timeline.map((tl) => (
            <div className="tl-item" key={tl.no}>
              <h3>
                <span className="wp-no">{tl.no}</span>
                {tl.title}
              </h3>
              <p>{tl.desc}</p>
            </div>
          ))}
        </div>
      </div>

      <div className="about-card">
        <h2>{t.aboutMethod}</h2>
        <p>
          {t.aboutMethodBody}
          <a
            href={`${GITHUB_REPO}/blob/main/docs/VERIFICATION_BATCH_B.md`}
            target="_blank"
            rel="noopener noreferrer"
          >
            docs/VERIFICATION_BATCH_B.md
          </a>
          {t.aboutMethodMid}
          <a
            href={`${GITHUB_REPO}/blob/main/docs/WORK_LOG.md`}
            target="_blank"
            rel="noopener noreferrer"
          >
            docs/WORK_LOG.md
          </a>
          {t.aboutMethodEnd}
        </p>
      </div>

      <div className="about-card">
        <h2>{t.aboutAi}</h2>
        <p>{t.aboutAiBody}</p>
      </div>

      <div className="about-card">
        <h2>{t.aboutLicense}</h2>
        <p>
          {t.aboutLicenseBody}
          <a
            href={`${GITHUB_REPO}/blob/main/LICENSE`}
            target="_blank"
            rel="noopener noreferrer"
          >
            LICENSE
          </a>
          {t.aboutLicenseMid}
          <a
            href={`${GITHUB_REPO}/blob/main/NOTICE`}
            target="_blank"
            rel="noopener noreferrer"
          >
            NOTICE
          </a>
          {t.aboutLicenseEnd}
        </p>
        <p>
          {t.aboutCopyright}
          <a href={GITHUB_REPO} target="_blank" rel="noopener noreferrer">
            github.com/cubestudio-dev/OpenCubeOS
          </a>
          {t.aboutRepoMid}
          <a href={RELEASES} target="_blank" rel="noopener noreferrer">
            Releases
          </a>
          {t.aboutRepoEnd}
        </p>
        <p>
          <a href={localePath(locale)} className="btn">
            {t.aboutBack}
          </a>
        </p>
      </div>
    </div>
  );
}
