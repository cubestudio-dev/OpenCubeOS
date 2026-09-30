#!/bin/bash
# Rewrite batch: force-push out/ as gh-pages branch (root commit)
set -e
OUT=/home/z/my-project/oc-os/website/out
ORIGIN_URL=$(git -C /home/z/my-project/oc-os config --get remote.origin.url)
echo "ORIGIN_URL=$ORIGIN_URL"
cd "$OUT"
echo "PWD=$(pwd)"
git init -q -b gh-pages .
git add -A
git commit -q -m "WP-09 website build (full rewrite): static export, /docs + /about, trailingSlash"
git log --oneline -1
echo "--- push gh-pages (force) ---"
git push -f "$ORIGIN_URL" gh-pages 2>&1
