#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - v1.0.0 release tooling
# File: tools/update_website.sh
# Purpose: update the website's release data at release time.
#
#   Reads:  dist/OpenCubeOS-<ver>.iso / -src.zip / -update.tar.gz
#           (the release assets - must exist)
#   Writes: website/assets/site-data.js   (the facade + download page data)
#           website/update.json + update-v2.json   (the OTA manifest)
#
#   Then the site build (website/build.sh) picks the numbers up; nothing
#   is hand-edited. Idempotent per release: same dist/ -> same outputs.
#
# Usage: tools/update_website.sh <version> <build> [date-iso]
# Env:   none (pure local computation)

set -euo pipefail

OC_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$OC_ROOT"

VER="${1:?usage: update_website.sh <version> <build> [date-iso]}"
BUILD="${2:?usage: update_website.sh <version> <build> [date-iso]}"
DATE="${3:-$(date -u +%Y-%m-%d)}"

DIST="$OC_ROOT/dist"
ISO="$DIST/opencube-${VER}.iso"
SRC="$DIST/opencube-${VER}-src.zip"
OTA="$DIST/opencube-${VER}-update.tar.gz"

# ---- locate the release assets ---------------------------------------
# The Makefile emits build/opencube.iso, dist/OpenCubeOS-src-<ver>.zip
# and dist/OpenCubeOS-<ver>-update.tar.gz; the canonical upload names
# (gh-pages downloads/, the GitHub Release, update.json) are the
# lowercase opencube-<ver>.* forms. Locate whichever exists and copy
# to the canonical name (no rename: both stay on disk).
find_asset() {
    # $1 = list of candidate paths
    for c in $1; do
        [ -f "$c" ] && { echo "$c"; return 0; }
    done
    return 1
}
ISO_SRC=$(find_asset "$ISO $DIST/OpenCubeOS-${VER}.iso $OC_ROOT/build/opencube.iso") \
    || { echo "update_website: no ISO for $VER (looked in dist/ and build/)" >&2; exit 1; }
SRC_SRC=$(find_asset "$SRC $DIST/OpenCubeOS-src-${VER}.zip") \
    || { echo "update_website: no SRC zip for $VER" >&2; exit 1; }
OTA_SRC=$(find_asset "$OTA $DIST/OpenCubeOS-${VER}-update.tar.gz") \
    || { echo "update_website: no OTA package for $VER" >&2; exit 1; }
for pair in "ISO:$ISO_SRC:$ISO" "SRC:$SRC_SRC:$SRC" "OTA:$OTA_SRC:$OTA"; do
    src="${pair#*:}"; src="${src%:*}"; dst="${pair##*:}"
    if [ "$src" != "$dst" ]; then
        cp -f "$src" "$dst"
        echo "update_website: $src -> $dst (canonical name)"
    fi
done

sha() { sha256sum "$1" | awk '{print $1}'; }
sz()  { stat -c%s "$1"; }
szh() {
    python3 -c "import sys; b=int(sys.argv[1]); print(f'{b/1048576:.1f} MiB' if b>=1048576 else f'{b/1024:.1f} KiB')" "$(stat -c%s "$1")"
}

ISO_SHA=$(sha "$ISO"); ISO_SZ=$(sz "$ISO"); ISO_H=$(szh "$ISO")
SRC_SHA=$(sha "$SRC"); SRC_SZ=$(sz "$SRC"); SRC_H=$(szh "$SRC")
OTA_SHA=$(sha "$OTA"); OTA_SZ=$(sz "$OTA"); OTA_H=$(szh "$OTA")

echo "update_website: $VER build $BUILD ($DATE)"
echo "  iso  $ISO_SZ  $ISO_SHA"
echo "  src  $SRC_SZ  $SRC_SHA"
echo "  ota  $OTA_SZ  $OTA_SHA"

# ---- regenerate site-data.js ----------------------------------------
# preserve the history array and stats from the existing file
python3 - "$VER" "$BUILD" "$DATE" \
    "$ISO_SHA" "$ISO_SZ" "$ISO_H" "$SRC_SHA" "$SRC_SZ" "$SRC_H" \
    "$OTA_SHA" "$OTA_SZ" "$OTA_H" <<'PYEOF'
import json, re, sys

(ver, build, date, iso_sha, iso_sz, iso_h, src_sha, src_sz, src_h,
 ota_sha, ota_sz, ota_h) = sys.argv[1:13]
path = "website/assets/site-data.js"
old = open(path).read()

# carry stats + history + links from the existing generated file
stats = old[old.index("stats: {"):old.index("history:")]
links = old[old.index("links: {"):old.index("};")]
hist_m = re.search(r"history: \[(.*?)\]\s*,\s*\n\s*links", old, re.S)
history = []
if hist_m:
    for m in re.finditer(
            r'\{ version: "([^"]+)", date: "([^"]+)",\s*'
            r'iso_sha: "([0-9a-f]+)",\s*iso_size: (\d+) \}',
            hist_m.group(1)):
        history.append({"version": m.group(1), "date": m.group(2),
                        "iso_sha": m.group(3), "iso_size": int(m.group(4))})

# read the previous version's entry for history from the old file's
# assets block (before overwriting)
prev = re.search(r'version: "([^"]+)",\s*\n\s*build: (\d+)', old)
prev_date = re.search(r'date: "([^"]+)"', old)
prev_iso = re.search(r'opencube-([^"]+?)\.iso",\s*\n\s*size: (\d+),[\s\S]*?sha256: "([0-9a-f]{64})"', old)
prev_ver = prev.group(1) if prev else None
already = any(h["version"] == prev_ver for h in history)
if prev and prev_iso and prev_ver != ver and not already:
    history.insert(0, {"version": prev_ver,
                       "date": prev_date.group(1) if prev_date else "-",
                       "iso_sha": prev_iso.group(3),
                       "iso_size": int(prev_iso.group(2))})

hist_lines = ",\n".join(
    '    { version: "%s", date: "%s",\n      iso_sha: "%s",\n'
    '      iso_size: %d }' % (h["version"], h["date"], h["iso_sha"],
                             h["iso_size"]) for h in history)

out = """/* SPDX-License-Identifier: Apache-2.0 */
/* Open Cube OS website data - GENERATED by tools/update_website.sh at
   release time. Do not hand-edit: the script rewrites this file from
   the real dist/ assets + the release manifest. */
window.SITE_DATA = {
  version: "%(ver)s",
  build: %(build)d,
  date: "%(date)s",
  changes: "%(ver)s release - see the GitHub release note for the change list",
  assets: {
    iso: {
      name: "opencube-%(ver)s.iso",
      size: %(iso_sz)d,
      size_h: "%(iso_h)s",
      sha256: "%(iso_sha)s",
      url: "downloads/opencube-%(ver)s.iso"
    },
    src: {
      name: "opencube-%(ver)s-src.zip",
      size: %(src_sz)d,
      size_h: "%(src_h)s",
      sha256: "%(src_sha)s",
      url: "downloads/opencube-%(ver)s-src.zip"
    },
    ota: {
      name: "opencube-%(ver)s-update.tar.gz",
      size: %(ota_sz)d,
      size_h: "%(ota_h)s",
      sha256: "%(ota_sha)s",
      url: "downloads/opencube-%(ver)s-update.tar.gz"
    }
  },
  %(stats)s,
  history: [
%(hist)s
  ],
  %(links)s
};
""" % {"ver": ver, "build": int(build), "date": date,
       "iso_sha": iso_sha, "iso_sz": int(iso_sz), "iso_h": iso_h,
       "src_sha": src_sha, "src_sz": int(src_sz), "src_h": src_h,
       "ota_sha": ota_sha, "ota_sz": int(ota_sz), "ota_h": ota_h,
       "stats": stats.rstrip().rstrip(","), "hist": hist_lines,
       "links": links.rstrip().rstrip(",")}

open(path, "w").write(out)
print("update_website: website/assets/site-data.js regenerated")
PYEOF

# ---- regenerate update.json + update-v2.json ------------------------
python3 - "$VER" "$BUILD" "$DATE" "$OTA_SHA" "$OTA_SZ" <<'PYEOF'
import json, sys
ver, build, date, sha, size = sys.argv[1:6]
manifest = {
    "version": ver,
    "build": int(build),
    "time": date,
    "changes": ("%s release. See the GitHub release note for the full "
                "change list, new interfaces and known issues." % ver),
    "changes_v2_url":
        "https://cubestudio-dev.github.io/OpenCubeOS/update-v2.json",
    "package_url":
        "https://cubestudio-dev.github.io/OpenCubeOS/downloads/"
        "opencube-%s-update.tar.gz" % ver,
    "package_sha256": sha,
    "package_size": int(size),
}
for name in ("update.json", "update-v2.json"):
    with open("website/" + name, "w") as f:
        json.dump(manifest, f, indent=4)
        f.write("\n")
    print("update_website: website/%s regenerated" % name)
PYEOF

echo "update_website: done. Next: website/build.sh then deploy out/ to gh-pages."
