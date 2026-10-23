#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - WP-14-docs_release
# File: website/build.sh
# Purpose: build the whole website - facade is static (nothing to do),
#          docs center is VitePress over a build-time copy of docs/.
#
# The copy (docs-src/docs/) is git-ignored: the single source of truth
# is the repository's docs/ tree; this script syncs it in, builds, and
# the output lands in out/:
#   out/            <- deploy root for gh-pages (facade + docs/)
#   out/docs/       <- VitePress output (322 pages incl. search index)
#
# Usage: website/build.sh [--serve]

set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$HERE"

command -v npm >/dev/null 2>&1 || { echo "ERROR: npm not found"; exit 1; }

# ---- 1. dependencies (once) ----
if [ ! -x node_modules/.bin/vitepress ]; then
    echo "--- installing docs-center dependencies (vitepress) ---"
    npm install --no-audit --no-fund
fi

# ---- 2. sync the docs tree in (single source: ../docs) ----
echo "--- syncing docs/ -> docs-src/docs/ (build-time copy) ---"
rsync -a --delete --exclude 'archive' --exclude 'data' ../docs/ docs-src/docs/
N=$(find docs-src/docs -name '*.md' | wc -l)
echo "    $N markdown pages"

# ---- 3. build ----
if [ "${1:-}" = "--serve" ]; then
    exec npm run docs:dev
fi
echo "--- building the docs center ---"
npm run docs:build

# ---- 4. assemble the deploy root ----
echo "--- assembling out/ (facade + docs center) ---"
mkdir -p out
cp -f index.html architecture.html download.html about.html out/
# copy CONTENTS: `cp -rf assets out/assets` on an existing out/assets
# would nest (out/assets/assets) instead of updating the files.
rm -rf out/assets
cp -rf assets out/assets
# downloads/ and update.json are managed by the release script
# (tools/update_website.sh) - not part of the site build.
echo "    out/index.html out/architecture.html out/download.html out/about.html out/assets/ out/docs/"
echo "build.sh: done."
