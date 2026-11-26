# WP-severe-fix-and-closure — ③ the website sync (B-website-sync)

Spec: "修复 update_website.sh 和前端数据源，版本号、统计数字、更新日志、
下载页资产 100% 同步，提供无头浏览器线上截图".

## The break that was found (and closed)

The `update_website.sh` data chain (site-data.js + update.json) was
regenerated for 3.0.1 correctly - but the HAND-MAINTAINED layer was TWO
releases stale:

- the static HTML (index/download/architecture/about: titles, meta
  descriptions, hero/tagline/terminal/shots/arch milestone texts) still
  named **3.0.0-WP-boot-stack-vmware-and-nak-rebase** (two releases ago)
  while the data files carried 3.0.1 - the live title literally read
  "3.0.0-WP-boot-stack" over a 3.0.1 release
- the i18n layer (EN + ZH) carried the same stale version strings plus
  the stale milestone text (dl.dev.h2/dl.dev.sub described the
  boot-stack milestone) and the stale command/program/line counts
  (303 listed / 312 registered / 301-program / 165,416 lines)
- the docs/data fact source needed regeneration (the tree gained code:
  166,643 -> **167,195** lines) - regenerated via
  `make api-docs` (`gen_api_docs.py`) before the site data was rebuilt,
  so every count on the site reads the CURRENT tree

## What was reconciled (every surface)

| Surface | Before | After |
|---|---|---|
| Live page title (EN+ZH) | 3.0.0-WP-boot-stack-vmware-and-nak-rebase | 3.0.1-WP-severe-fix-and-closure |
| Meta descriptions (4 pages) | 3.0.0-WP-boot-stack | 3.0.1-WP-severe-fix-and-closure |
| hero.tagline / hero.term.title / shots.* / arch.sub | 3.0.0-WP-boot-stack | 3.0.1-WP-severe-fix-and-closure |
| dl.dev.h2 / dl.dev.sub (the milestone text, EN+ZH) | the boot-stack milestone | the severe-fix release line |
| stat-cmds (listed commands) | 303 | 304 |
| registered commands | 312 (EN) / 311 (ZH) | 313 |
| L1 program world | 301-program / 301 个程序 | 269-program / 269 个程序 |
| stat-lines2 (original code lines) | 165,416 | 167,195 |
| README (current release + counts) | 3.0.0 / 301 / 311 | 3.0.1 / 269 / 313 |

Facts source: `docs/data/*.json` regenerated from the live tree
(make api-docs): **313 registered commands (304 listed)** /
**167,195 lines** / **269 L1 program directories**.

## The release chain (3.0.1, build 529)

1. `make all` (0 errors / 0 warnings) -> the final release bytes; every
   acceptance battery re-run ON those bytes (strict both tracks, 31/31
   on the NOSPLASH diagnostic ISO, dhtest 5/5, OCIRE 14/14, HTTPS E2E
   8/8, SSH outbound PASS, update-chain 10/10 - see the release notes).
2. tag `v3.0.1-WP-severe-fix-and-closure` pushed; GitHub Release
   459717030 with FOUR assets (iso / ocse medium / OTA / src) - the
   GitHub-reported digests match the published SHA256 line byte for
   byte (e4bfd710…, 06a29b3b…, 26c7e7cd…, b9267daf…).
3. `update_website.sh 3.0.1-WP-severe-fix-and-closure 529 <en changes>
   <zh changes> <ocse iso>` regenerated site-data.js (+ the bilingual
   update.json/update-v2.json pair, top level + public/, byte-identical).
4. `website/build.sh` -> out/; deployed to gh-pages (a9d354c6) with the
   four assets copied into downloads/.

## Headless-browser LIVE verification (the spec's screenshot gate)

Fresh session (cache-busted), DOM asserted BEFORE the screenshots:

- home: title = "Open Cube OS 3.0.1-WP-severe-fix-and-closure - a
  from-scratch x86_64 kernel that boots into ring 3";
  `#stat-cmds` renders **313**; `#stat-lines2` renders **167,195**;
  "severe-fix-and-closure" present on the page
- download page: the four dev-channel cards render the 3.0.1 asset
  names (opencube ISO / OCSE medium / OTA tar.gz / src zip), the four
  hrefs point at `https://ocos.cubestudio.cn/downloads/opencube-…3.0.1…`
  and the four SHA256 lines match the published digests exactly
- live download links: iso 200, ocse 200, ota 206 (range), src 206
  (range) - every asset answers
- live OTA manifest: `update.json` carries version 3.0.1 build 529
  (the in-OS `checkupdate` against this URL reports the new version)

Screenshots (1440x900, live site):

- `website/live-home-3.0.1.png` - the home facade (the 3.0.1 title,
  hero with 313 commands / 167,195 lines)
- `website/live-download-3.0.1.png` - the download page (the four
  3.0.1 asset cards with SHA256 lines)

## Honest notes

- The first two `website/build.sh` runs were OOM-killed (the sandbox's
  4 GB was mostly held by the dev server + a leftover browser); the
  build went green after closing the browser.  No site output was
  published from a killed build.
- The browser's first reads of the updated pages served the CDN's
  cached old title; a fresh cache-busted session read the new bytes
  (the curl of the same URL confirmed the live title immediately) -
  the screenshots were taken from the fresh session.
