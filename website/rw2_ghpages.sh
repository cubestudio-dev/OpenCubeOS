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
git commit -q -m "website build: WP-10-AUDIT_P2-fix2b (fix2 payload re-verified in tree: 53/53 P2 items + 4/4 findings; embed-chain repair: libfoo solib leg via build_solib.py -Wl,-soname,libfoo.so, six dyn programs ET_DYN/PIE with hard e_type/PT_INTERP gates, libs/ld_so.c wired into USERPROG_SRCS, make userprogs green end-to-end, six dyn progs PASS in QEMU), WP-10-AUDIT_P2-fix2b ISO-SRC assets (3-way sha256: iso 6f5d7c2c, src bc2f0070), stats 101,150 lines / 176 commands"
git log --oneline -1
echo "--- push gh-pages (force) ---"
git push -f "$ORIGIN_URL" gh-pages 2>&1
