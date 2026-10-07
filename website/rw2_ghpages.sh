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
git commit -q -m "website build: WP-AUDIT-01-p1fix4 (HTTPS E2E disconnect fixed: IP-literal reference identity per RFC 6125; TLS probes silenced TLS_DBG=0), wp-audit-01-p1fix4 ISO-SRC assets (3-way sha256: iso ddb61e99, src 17531c26), stats 95,001 lines / 173 commands"
git log --oneline -1
echo "--- push gh-pages (force) ---"
git push -f "$ORIGIN_URL" gh-pages 2>&1
