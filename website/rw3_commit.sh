#!/bin/bash
# i18n batch: commit zh/en website (URL routing /zh/ + /en/)
set -e
cd /home/z/my-project/oc-os
echo "PWD=$(pwd)"
git add website/app website/lib website/docs-zh
git status --short
echo "--- commit ---"
git commit -q -m "WP-09 website i18n: URL-routed zh/en locales (/zh/ + /en/), zh doc translations (docs-zh/), lang switch with localStorage, root locale redirect"
git log --oneline -1
