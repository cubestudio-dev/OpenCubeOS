#!/bin/bash
# WP-09 website build script — cd is IN the file, cannot be forgotten
set -e
cd /home/z/my-project/archive-repo/oc-os/website
echo "PWD=$(pwd)"
# WP-09-fix5 FIX: start from a clean out/ — a stale out/ kept the OLD
# downloads/ directory alive and gh-pages shipped stale assets while the
# new files landed at out/ root (mixed layouts). Always rebuild fresh.
rm -rf out
echo "--- bun install ---"
bun install 2>&1 | tail -3
echo "--- bun run build ---"
bun run build 2>&1 | tail -15
echo "--- out/ check ---"
ls out/
echo "--- out/ check ---"
ls out/ | head -8
echo "--- downloads check ---"
ls out/downloads/ | head -4
sha256sum out/downloads/* 2>/dev/null | head -2
echo "BUILD-OK"
