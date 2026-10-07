#!/bin/bash
# Rewrite batch: force-push out/ as gh-pages branch (root commit)
set -e
OUT=${OC_GHPAGES_OUT:-/home/z/my-project/oc-work/oc-os/website/out}
ORIGIN_URL=$(git -C "${OC_PUBLIC_DIR:-/home/z/my-project/oc-work/oc-os}" config --get remote.origin.url)
echo "ORIGIN_URL=$(echo "$ORIGIN_URL" | sed 's|x-access-token:[^@]*|x-access-token:REDACTED|')"
cd "$OUT"
echo "PWD=$(pwd)"
git init -q -b gh-pages .
# build-local.sh deletes out/ (including any previous .git), so this fresh
# init has no identity — set it explicitly or the commit is authored as
# the sandbox default (Z User <z@container>), which fails repo author checks.
git config user.name "cubestudio-dev"
git config user.email "cubestudio@qq.com"
git add -A
git commit -q -m "website build: WP-10-AUDIT_P2-fix1 (P2 batch 1: all 53 fixes BUG-0136..0188 across memory/arch/USB/VFS-FAT + the heap-block-header alignment root-cause fix found by the regression pass), WP-10-AUDIT_P2-fix1 ISO-SRC assets (3-way sha256: iso 737c8cfa, src 89087bc1), stats 97,307 lines / 176 commands"
git log --oneline -1
echo "--- push gh-pages (force) ---"
git push -f "$ORIGIN_URL" gh-pages 2>&1
