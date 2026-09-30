#!/bin/bash
# WP-09 website build script — cd is IN the file, cannot be forgotten
set -e
cd /home/z/my-project/oc-os/website
echo "PWD=$(pwd)"
echo "--- bun install ---"
bun install 2>&1 | tail -3
echo "--- bun run build ---"
bun run build 2>&1 | tail -15
echo "--- out/ check ---"
ls out/
echo "--- out/OpenCubeOS/ check ---"
ls out/OpenCubeOS/ | head -8
echo "--- downloads check ---"
ls out/OpenCubeOS/downloads/ 2>/dev/null | head -4
echo "BUILD-OK"
