#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - v1.0.0 release tooling
# File: tools/update_website.sh
# Purpose: update the website's release data at release time.
#
#   Reads:  dist/opencube-<ver>.iso / -src.zip / -update.tar.gz
#           (the release assets - must exist)
#           dist/ocse-*.iso (the NEWEST one - the dev-channel install
#           medium; or pass its path as the 6th argument)
#   Writes: website/assets/site-data.js   (the facade + download page data:
#           stable line, bilingual description, the GENERATED dev block)
#           website/update.json + update-v2.json   (the OTA manifest,
#           with the bilingual description object - spec 4.6)
#
#   Then the site build (website/build.sh) picks the numbers up; nothing
#   is hand-edited. Idempotent per release: same dist/ -> same outputs.
#
# Usage: tools/update_website.sh <version> <build> [date-iso] [changes] [changes-zh] [ocse-iso]
# Env:   OC_SITE_BASE (default https://ocos.cubestudio.cn) - the absolute
#       origin baked into every asset URL (spec 4.9: no relative links).

set -euo pipefail

OC_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$OC_ROOT"

VER="${1:?usage: update_website.sh <version> <build> [date-iso] [changes] [changes-zh] [ocse-iso]}"
BUILD="${2:?usage: update_website.sh <version> <build> [date-iso] [changes] [changes-zh] [ocse-iso]}"
DATE="${3:-$(date -u +%Y-%m-%d)}"
# v2.0.0: optional explicit changes text (e.g. the BREAKING summary the
# OTA checkupdate banner shows). Falls back to the generic one-liner.
CHANGES="${4:-}"
# P2D3C (spec 4.6): optional Simplified-Chinese changelog. Empty -> the
# description object duplicates the en text (a single-language manifest
# stays valid; kernel/web consumers fall back identically). The flat
# "changes" key ALWAYS stays for the old-kernel 127-byte discipline.
CHANGES_ZH="${5:-}"
# P2D3C (spec 4.9): the dev-channel ocse medium - explicit path, else
# the newest dist/ocse-*.iso (mtime order).
OCSE_ARG="${6:-}"
# P2D3C (spec 4.9): absolute origin for every generated asset URL.
BASE_URL="${OC_SITE_BASE:-https://ocos.cubestudio.cn}"

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

# ---- P2D3C (spec 4.9): the dev-channel ocse medium -------------------
# The download page's dev tab pairs THIS release's opencube ISO with the
# current ocse install medium; the dev block is GENERATED here on every
# run (the old hand-maintained carry shipped a site whose dev buttons
# were dead '#' links). A release therefore cannot ship without the
# ocse medium - same hard-error discipline as the three stable assets.
OCSE_SRC="$OCSE_ARG"
if [ -z "$OCSE_SRC" ]; then
    OCSE_SRC="$(ls -1t "$DIST"/ocse-*.iso 2>/dev/null | head -n 1 || true)"
fi
if [ -z "$OCSE_SRC" ] || [ ! -f "$OCSE_SRC" ]; then
    echo "update_website: no OCSE ISO for the dev channel (put the newest ocse-*.iso into dist/ or pass its path as arg 6)" >&2
    exit 1
fi
OCSE="$DIST/$(basename "$OCSE_SRC")"
if [ "$(readlink -f "$OCSE_SRC" 2>/dev/null || echo x)" != "$(readlink -f "$OCSE" 2>/dev/null || echo y)" ]; then
    cp -f "$OCSE_SRC" "$OCSE"
    echo "update_website: $OCSE_SRC -> $OCSE (canonical name)"
fi

sha() { sha256sum "$1" | awk '{print $1}'; }
sz()  { stat -c%s "$1"; }
szh() {
    python3 -c "import sys; b=int(sys.argv[1]); print(f'{b/1048576:.1f} MiB' if b>=1048576 else f'{b/1024:.1f} KiB')" "$(stat -c%s "$1")"
}

ISO_SHA=$(sha "$ISO"); ISO_SZ=$(sz "$ISO"); ISO_H=$(szh "$ISO")
SRC_SHA=$(sha "$SRC"); SRC_SZ=$(sz "$SRC"); SRC_H=$(szh "$SRC")
OTA_SHA=$(sha "$OTA"); OTA_SZ=$(sz "$OTA"); OTA_H=$(szh "$OTA")
OCSE_SHA=$(sha "$OCSE"); OCSE_SZ=$(sz "$OCSE"); OCSE_H=$(szh "$OCSE")
OCSE_NAME="$(basename "$OCSE")"

echo "update_website: $VER build $BUILD ($DATE)"
echo "  iso  $ISO_SZ  $ISO_SHA"
echo "  src  $SRC_SZ  $SRC_SHA"
echo "  ota  $OTA_SZ  $OTA_SHA"
echo "  ocse $OCSE_SZ  $OCSE_SHA  ($OCSE_NAME, dev channel)"

# carry history + links from the existing generated file
python3 - "$VER" "$BUILD" "$DATE" \
    "$ISO_SHA" "$ISO_SZ" "$ISO_H" "$SRC_SHA" "$SRC_SZ" "$SRC_H" \
    "$OTA_SHA" "$OTA_SZ" "$OTA_H" "$CHANGES" "$CHANGES_ZH" \
    "$OCSE_NAME" "$OCSE_SHA" "$OCSE_SZ" "$OCSE_H" "$BASE_URL" <<'PYEOF'
import json, re, sys

(ver, build, date, iso_sha, iso_sz, iso_h, src_sha, src_sz, src_h,
 ota_sha, ota_sz, ota_h, changes, changes_zh,
 ocse_name, ocse_sha, ocse_sz, ocse_h, base_url) = sys.argv[1:20]
changes = changes or ("%s release - see the GitHub release note for "
                      "the change list" % ver)
# P2D3C (spec 4.6): description.en mirrors the changes text verbatim;
# description.zh falls back to the en text when no zh copy was given.
desc_en = changes
desc_zh = changes_zh or changes
path = "website/assets/site-data.js"
old = open(path).read()

# 2.1.0 (P-10 fix): REGENERATE the stats block from docs/data/*.json
# instead of carrying the previous file's block forward - the carry
# was how the 2.0.0 site shipped a stale 130,011 line count while the
# real tree measured 130,495 (audit P-10; violates the 12-point #7
# "website numbers match docs/data/*.json" gate).
jstats = json.load(open("docs/data/stats.json"))
jcmds = json.load(open("docs/data/commands.json"))
jifaces = json.load(open("docs/data/interfaces.json"))
sc = 0
for ln in open("ocos/services/sysapi/interface/svc_sysapi_syscall.h", encoding="utf-8"):
    if "#define SYS_" in ln:
        sc += 1
stats = """stats: {
    commands: %d,
    interfaces: %d,
    syscalls: %d,
    lines: "%s"
  }""" % (jcmds["count_v2_surface"], jifaces["l1_official_count"], sc,
         format(jstats["total_lines"], ","))

# WP06e -> P2D3C (spec 4.9): the dev block (the development-preview
# channel - THIS release's opencube ISO + the current ocse medium) is
# GENERATED from dist/ on every run; the old carry-verbatim regex is
# GONE (it preserved a block the script never wrote, which is how the
# site shipped dead '#' dev buttons in the first place).

links = old[old.index("links: {"):old.index("};")]
hist_m = re.search(r"history: \[(.*?)\]\s*,\s*\n\s*links", old, re.S)
history = []
if hist_m:
    # WP06e: an entry may carry iso_url (milestone assets whose file
    # name is not opencube-<version>.iso, e.g. the OCIRE ISO).
    for m in re.finditer(
            r'\{ version: "([^"]+)", date: "([^"]+)",(?:\s*'
            r'iso_url: "([^"]*)",)?\s*'
            r'iso_sha: "([0-9a-f]+)",\s*iso_size: (\d+) \}',
            hist_m.group(1)):
        history.append({"version": m.group(1), "date": m.group(2),
                        "iso_url": m.group(3) or "",
                        "iso_sha": m.group(4), "iso_size": int(m.group(5))})

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

# WP06d-site_fix: when the release line moves BACK to an earlier version
# (e.g. the facade returns from a dev milestone to the formal line), the
# current version may already sit in history - drop it there so the
# download page table never lists it twice.
history = [h for h in history if h["version"] != ver]

def _hist_row(h):
    if h.get("iso_url"):
        return ('    { version: "%s", date: "%s",\n      iso_url: "%s",\n'
                '      iso_sha: "%s",\n      iso_size: %d }'
                % (h["version"], h["date"], h["iso_url"], h["iso_sha"],
                   h["iso_size"]))
    return ('    { version: "%s", date: "%s",\n      iso_sha: "%s",\n'
            '      iso_size: %d }'
            % (h["version"], h["date"], h["iso_sha"], h["iso_size"]))

hist_lines = ",\n".join(_hist_row(h) for h in history)

out = """/* SPDX-License-Identifier: Apache-2.0 */
/* Open Cube OS website data - the STABLE line (version/build/date/
   changes/description/assets/history) AND the dev block (the current
   milestone's opencube + ocse pair) are GENERATED by
   tools/update_website.sh at release time (do not hand-edit those).
   P2D3C spec 4.6: description carries the bilingual {en, zh} changelog
   (changes stays the flat compat key).  P2D3C spec 4.9: the dev block
   is generated from dist/ (the hand-maintained carry is gone) and
   every asset URL is ABSOLUTE. */
window.SITE_DATA = {
  version: "%(ver)s",
  build: %(build)d,
  date: "%(date)s",
  changes: "%(changes)s",
  description: {
    en: "%(desc_en)s",
    zh: "%(desc_zh)s"
  },
  assets: {
    iso: {
      name: "opencube-%(ver)s.iso",
      size: %(iso_sz)d,
      size_h: "%(iso_h)s",
      sha256: "%(iso_sha)s",
      url: "%(base)s/downloads/opencube-%(ver)s.iso"
    },
    src: {
      name: "opencube-%(ver)s-src.zip",
      size: %(src_sz)d,
      size_h: "%(src_h)s",
      sha256: "%(src_sha)s",
      url: "%(base)s/downloads/opencube-%(ver)s-src.zip"
    },
    ota: {
      name: "opencube-%(ver)s-update.tar.gz",
      size: %(ota_sz)d,
      size_h: "%(ota_h)s",
      sha256: "%(ota_sha)s",
      url: "%(base)s/downloads/opencube-%(ver)s-update.tar.gz"
    }
  },
  /* P2D3C (spec 4.9): the development-preview channel - the opencube
     ISO of this release + the current ocse install medium, generated
     from dist/ on every run (absolute URLs; the defensive GitHub
     Releases fallback lives in assets/app.js). */
  dev: {
    version: "%(ver)s",
    date: "%(date)s",
    assets: {
      opencube: {
        name: "opencube-%(ver)s.iso",
        size: %(iso_sz)d,
        size_h: "%(iso_h)s",
        sha256: "%(iso_sha)s",
        url: "%(base)s/downloads/opencube-%(ver)s.iso"
      },
      ocse: {
        name: "%(ocse_name)s",
        size: %(ocse_sz)d,
        size_h: "%(ocse_h)s",
        sha256: "%(ocse_sha)s",
        url: "%(base)s/downloads/%(ocse_name)s"
      }
    }
  },
  %(stats)s,
  history: [
%(hist)s
  ],
  %(links)s
};
""" % {"ver": ver, "build": int(build), "date": date, "changes": changes,
       "desc_en": desc_en, "desc_zh": desc_zh,
       "iso_sha": iso_sha, "iso_sz": int(iso_sz), "iso_h": iso_h,
       "src_sha": src_sha, "src_sz": int(src_sz), "src_h": src_h,
       "ota_sha": ota_sha, "ota_sz": int(ota_sz), "ota_h": ota_h,
       "ocse_name": ocse_name, "ocse_sha": ocse_sha,
       "ocse_sz": int(ocse_sz), "ocse_h": ocse_h,
       "stats": stats.rstrip().rstrip(","), "hist": hist_lines,
       "links": links.rstrip().rstrip(","), "base": base_url}

open(path, "w", encoding="utf-8").write(out)
print("update_website: website/assets/site-data.js regenerated")
PYEOF

# ---- regenerate update.json + update-v2.json ------------------------
python3 - "$VER" "$BUILD" "$DATE" "$OTA_SHA" "$OTA_SZ" "$CHANGES" "$CHANGES_ZH" <<'PYEOF'
import json, sys
ver, build, date, sha, size, changes, changes_zh = sys.argv[1:8]
changes = (changes or
           "%s release. See the GitHub release note for the full "
           "change list, new interfaces and known issues." % ver)
# P2D3C (spec 4.6): the bilingual description object. en mirrors the
# flat changes text; zh falls back to the en text when no zh copy was
# passed. The flat "changes" key STAYS - old kernels (WP-09-era
# 128-byte buffers) keep reading the manifest unchanged.
manifest = {
    "version": ver,
    "build": int(build),
    "time": date,
    "changes": changes,
    "description": {"en": changes, "zh": changes_zh or changes},
    "changes_v2_url":
        "https://ocos.cubestudio.cn/update-v2.json",
    "package_url":
        "https://ocos.cubestudio.cn/downloads/"
        "opencube-%s-update.tar.gz" % ver,
    "package_sha256": sha,
    "package_size": int(size),
}
for name in ("update.json", "update-v2.json"):
    # 2.1.0 (P-12 fix): BOTH copies are written byte-identical - the
    # website/ top-level pair AND website/public/. Through 2.0.0 only
    # the top-level pair was rewritten and website/public/ drifted
    # (it still carried the WP-13/build-259 manifests at the 2.0.0
    # release - audit P-12).
    # P2D3C (spec 4.6): ensure_ascii=False - description.zh is written
    # as RAW UTF-8, not \uXXXX escapes.  The kernel's scoped JSON
    # parser copies value bytes verbatim and takes "\x" escapes
    # literally, so an escaped zh changelog would display as garbage;
    # old kernels never read the description object either way.
    for path in ("website/" + name, "website/public/" + name):
        with open(path, "w", encoding="utf-8") as f:
            json.dump(manifest, f, indent=4, ensure_ascii=False)
            f.write("\n")
    print("update_website: %s regenerated (top level + public/)" % name)
PYEOF

echo "update_website: done. Next: website/build.sh then deploy out/ to gh-pages."
