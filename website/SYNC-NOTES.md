<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 cubestudio-dev <cubestudio@qq.com> -->
# website/SYNC-NOTES.md — main-tree website ↔ gh-pages sync state

Written 2026-11-10 for BUG #91 / #99 (main `website/` was stuck at
3.0.0-WP06g2 / build 403 while gh-pages and the GitHub Release moved
to WP06g3).  Record of what this tree now holds and what still needs a
network-connected deploy to converge.

## What was done here (offline-safe)

- `website/` was **empty** in this mirror of the main tree (only a
  `.gitkeep`); the full site source lived frozen under
  `archive/v3.0.0/website/` — the exact WP06g2 / build-403 snapshot
  BUG #91 audited.  The site source (facade pages, `assets/`,
  `docs-src/`, `build.sh`, OTA manifests, `CNAME`, `.gitignore`,
  `package.json`) was restored from that snapshot into `website/`.
- Version/build brushed **3.0.0-WP06g2 / 403 → 3.0.0-WP06g3 / 414** in
  the hand-maintained "current release" surfaces: `assets/site-data.js`
  (dev channel block), `index.html`, `download.html` and the EN+ZH
  dictionaries in `assets/i18n.js` that render those two pages.
- The WP06g3 dev-channel assets are **not copied into
  `website/public/downloads/`** (nothing is fabricated): no g3 binary
  exists in this tree.  Instead the g3 entries point straight at the
  GitHub Release download URLs.  Names / sizes / SHA256 were taken from
  the Release API (`releases/tags/v3.0.0-WP06g3`, assets[].size /
  assets[].digest, re-checked 2026-11-10):
  - `ocse-3.0.0-WP06g3.iso` 5038080 B, sha256 `0abb622c…3ff` (matches
    the gh-pages blob hash recorded in the audit)
  - `opencube-3.0.0-WP06g3.iso` 4210688 B, sha256 `34bcccdb…ef9`
- Kept truthful on purpose (NOT bumped): the two index-page
  screenshots (`assets/shot-bios.png`, `assets/shot-uefi.png`) are
  real captures of the **WP06g2** ISO, so their captions/alt texts
  still say WP06g2.  `architecture.html` (+ `arch.sub` i18n strings)
  still describes the WP06g2 snapshot of the tree.

## Pending — needs a real deploy (git push is blocked in this sandbox)

1. **Re-capture the index screenshots from the WP06g3 ISO**
   (BIOS + UEFI), then bump the `shots.*` captions to g3.  Until then
   the images honestly show g2 boots.
2. **Regenerate `architecture.html`** (and its `arch.sub` strings)
   against the g3 tree — `website/build.sh` + VitePress, or the site
   generator used at g2.
3. **Rebuild + deploy the docs center.**  gh-pages carries the
   VitePress `docs/` build output (~2423 files, per the #99 audit);
   the main tree only has the `docs-src/` wrapper.  Run
   `website/build.sh` (needs npm) and publish `out/`.
4. **Restore the binary downloads** into `website/public/downloads/`
   (2.2.0 / WP06g2 assets and older).  They exist on gh-pages and in
   `archive/v3.0.0/website/public/downloads/` but are too large to
   mirror into every main-tree checkout; the relative
   `downloads/…` URLs keep working on the deployed site.  Do **not**
   invent g3 files there — the g3 entries intentionally point at the
   GitHub Release.
5. **Rotate the release history** with `tools/update_website.sh` at
   the next real release run: move WP06g2 into the generated
   `history` array (`iso_sha 53e22e538e6de9c9e4aa29216791e3f568b2a82e
   88422876b0f2f6517160f84d`, 3833856 B, date 2026-11-05) — that
   section is generator-owned, so it was not hand-edited here.
6. **Reconcile the build number.**  gh-pages reported g3 / build 416
   at audit time (#99); this tree now says build 414 (the
   rev-list-count+21 value of the release commit, the same number the
   g3 kernel banner carries — see `ocire/generated/ocire_version_gen.h`
   and BUG #92).  Whichever site goes live must show the number the
   shipped ISOs actually print.
7. **Push gh-pages** and re-run the three-way check (site ==
   GitHub Release == local tree, SHA256 for every asset) that
   `tools/update_website.sh` guarantees mechanically.

## Reference digests (Release API, 2026-11-10)

| Asset | Size | SHA256 |
|-------|------|--------|
| ocse-3.0.0-WP06g3.iso | 5038080 | 0abb622cf3e88641db227fcec1082ecdfa42c367d6eb85247524e4a3923cf3ff |
| opencube-3.0.0-WP06g3.iso | 4210688 | 34bcccdbcf8f66ab9e70bcb8c9a516a4a491d28c3dd9b47d970ee1b37c93fef9 |
| ocse-3.0.0-WP06g2.iso | 4655104 | 074ffa85b9e45b7b7a35bdf2f60b29724bd0f8c8afdc48d8760276891fa09e9b |
| opencube-3.0.0-WP06g2.iso | 3833856 | 53e22e538e6de9c9e4aa29216791e3f568b2a82e88422876b0f2f6517160f84d |
