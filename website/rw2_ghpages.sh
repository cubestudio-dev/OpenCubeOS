#!/bin/bash
# Rewrite batch: force-push out/ as gh-pages branch (root commit)
set -e
OUT=/home/z/my-project/archive-repo/oc-os/website/out
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
git commit -q -m "website build: WP-AUDIT-01-p1fix2 (P1 items 32..62 fixed, BUG-0073..0103), wp-audit-01-p1fix2 ISO-SRC assets (3-way sha256: iso b1445300, src 7dc6f58a), stats 93,923 lines / 173 commands"
git log --oneline -1
echo "--- push gh-pages (force) ---"
git push -f "$ORIGIN_URL" gh-pages 2>&1
