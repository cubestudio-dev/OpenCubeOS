<!-- SPDX-License-Identifier: Apache-2.0 -->
# Website update mechanics

`tools/update_website.sh` - run at release time after the assets
exist. What it does (each step verifiable):

1. Reads the three assets from `dist/`, computes SHA256 + size.
2. Regenerates the site's data file (`site.ts` constants: RELEASE /
   ISO / SRC / OTA names + hashes + sizes) from those values - no
   hand-editing of versions anywhere.
3. Regenerates `update.json` + `update-v2.json` (version, build,
   time, changes text, package URL/sha/size) from the manifest
   template + the release notes.
4. Runs the hash gate: the downloads directory's files must match
   the constants before anything is pushed.
5. Builds the static site and pushes gh-pages.

Verification after it runs:

- `https://cubestudio-dev.github.io/OpenCubeOS/update.json` shows the
  new version/build/sha.
- The download page's three hashes equal the Release's three.
- `curl -sL <asset-url> | sha256sum` on each asset equals its
  declared SHA256.

The script is idempotent per release: rerunning with the same dist/
produces the same pages (deterministic templates).
