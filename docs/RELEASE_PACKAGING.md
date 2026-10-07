<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 cubestudio-dev <cubestudio@qq.com> -->

# Release packaging: SRC zip standard procedure

Applies to every `opencube-*-src.zip` shipped as a Release asset or a
`website/public/downloads/` entry. Introduced after WP-AUDIT-01-p1fix1,
whose first SRC zip accidentally weighed 217 MB because the packaging
source tree carried `website/public/downloads/` (286 MB of historical
Release assets, including the 102 MB p0fix1 package).

## 1. Fixed exclusion list (mandatory, never optional)

A SRC zip contains source, docs and build tooling only. Exclude:

| Exclusion | Reason |
|---|---|
| `website/public/downloads/` (everything) | Historical Release assets; they live in git history and on Releases, never inside a SRC zip |
| `website/out/` | Website build output |
| `website/.next/` | Website incremental build cache (leaks 345 files / ~1.7 MB if present) |
| `website/node_modules/` | Website dependencies |
| `.git/` | Repository history |
| `build/` | Kernel/ISO build products |
| `docs/WORKFLOW.md` | Internal agent workflow notes; not part of the shipped source (excluded since WP-10-AUDIT_P2-fix1) |
| `*.o`, `*.elf`, `*.bin`, `*.iso`, `*.img`, `*.tar.gz` outside `iso/boot/grub/fonts/` | Compiled artifacts (the GRUB `unicode.pf2` font is a legit source-tree asset and stays) |
| `__pycache__/`, `*.pyc` | Python caches |

## 2. Standard command (run from a clean checkout or export)

```sh
rm -f /tmp/<name>-src.zip
zip -r -q /tmp/<name>-src.zip . \
  -x "website/public/downloads/*" \
  -x "website/public/downloads/" \
  -x "website/out/*" \
  -x "website/.next/*" \
  -x "website/node_modules/*" \
  -x ".git/*" \
  -x "build/*" \
  -x "docs/WORKFLOW.md" \
  -x "*.pyc" -x "*__pycache__*"
```

Sanity gates before shipping:

1. `unzip -l <name>-src.zip | grep -E "downloads/|/build/|node_modules|\.iso|\.next/|WORKFLOW\.md"` -> zero matches
   (only `iso/boot/grub/fonts/unicode.pf2` may match the broad `\.(bin|pf2)$` net).
2. Expected size: roughly 2.5-3.5 MB (p0fix2 = 2,566,826 B; p1fix1 = 2,647,642 B).
   Anything above ~5 MB means an exclusion leaked — stop and fix the pack.
3. `sha256sum` the zip and record it; the same value must appear on the
   GitHub Release asset, the site `/downloads/` copy and the local build
   (3-way identical rule, as with the ISO).

## 3. gh-pages downloads convention

`website/public/downloads/` holds the CURRENT release's ISO + SRC zip plus
already-published historical assets. Rules:

- Never commit a new large file (>50 MB, GitHub warning threshold) here.
  If a historical SRC zip was oversized, replace it with a corrected
  package (this was done for p0fix1: 102,802,637 B -> 2,323,002 B,
  sha256 51a9d93c...).
- The packaging step must not pull this directory into any SRC zip (see §1).
- gh-pages ships what this directory holds; keep it lean.

## 4. Token hygiene (related lesson from p1fix1)

- Public repo (cubestudio-dev/OpenCubeOS) push/Release/gh-pages:
  use the `opencube-os-push` token only.
- Private archive repo (cubestudio-dev/tdgh...): use the
  `OpneCubeOS-push-save` token only.
- A 403 with `Permission to ... denied` is a wrong-token symptom, not a
  missing-permission symptom, when each token's scope is known-good.
- `GET /repos/{owner}/{repo}` `permissions.push` reflects the USER's role
  (owner -> true), never the token's resource grant. Fine-grained PAT
  resource grants have no self-inspection API; test with a real write.
