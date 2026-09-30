#!/bin/bash
# Rewrite batch: commit website rewrite to main
set -e
cd /home/z/my-project/oc-os
echo "PWD=$(pwd)"
echo "--- git add -A ---"
git add -A
git status --short
echo "--- git commit ---"
git commit -m "WP-09 website full rewrite: static export, hand-written CSS (no Tailwind), trailingSlash, /docs + /about pages"
echo "--- git push ---"
git push origin main 2>&1
