#!/bin/bash
# Rewrite batch: force-push out/ as gh-pages branch (root commit)
set -e
OUT=/home/z/my-project/oc-os/website/out
ORIGIN_URL=$(git -C /home/z/my-project/oc-os config --get remote.origin.url)
echo "ORIGIN_URL=$ORIGIN_URL"
cd "$OUT"
echo "PWD=$(pwd)"
git init -q -b gh-pages .
# build-local.sh deletes out/ (including any previous .git), so this fresh
# init has no identity — set it explicitly or the commit is authored as
# the sandbox default (Z User <z@container>), which fails repo author checks.
git config user.name "cubestudio-dev"
git config user.email "cubestudio@qq.com"
git add -A
git commit -q -m "website build: WP-10d (USB host stack UHCI-OHCI-EHCI-XHCI, device-level), docs zh-en sync, wp10d ISO-SRC assets (3-way sha256), update.json -> WP-10d OTA package"
git log --oneline -1
echo "--- push gh-pages (force) ---"
git push -f "$ORIGIN_URL" gh-pages 2>&1
