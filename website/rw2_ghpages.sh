#!/bin/bash
# Rewrite batch: force-push out/ as gh-pages branch (root commit)
set -e
OUT=/home/z/my-project/oc-work/oc-os/website/out
ORIGIN_URL=$(git -C /home/z/my-project/oc-os config --get remote.origin.url)
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
git commit -q -m "website build: WP-AUDIT-01-p1fix3 (P1 items 63..94 fixed, BUG-0104..0135; all 94 P1 done), wp-audit-01-p1fix3 ISO-SRC assets (3-way sha256: iso 8fe3d496, src fe2b45b4), stats 94,821 lines / 173 commands"
git log --oneline -1
echo "--- push gh-pages (force) ---"
git push -f "$ORIGIN_URL" gh-pages 2>&1
