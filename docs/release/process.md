<!-- SPDX-License-Identifier: Apache-2.0 -->
# The release process (SOP)

The exact sequence a release walks. Every step has a gate; a failed
gate stops the release.

## 0. Pre-flight

```
git fetch && git status        (clean tree, correct branch)
git tag --list 'WP-*'          (the new tag does not exist yet)
```

## 1. Build

```
make clean && make -j$(nproc) all
```

Gate: 0 errors 0 warnings (`-Wall -Wextra -Werror` is in the flags).
Outputs: `build/opencube.iso`, `dist/OpenCubeOS-<ver>-src.zip`,
`dist/OpenCubeOS-<ver>-update.tar.gz`.

## 2. Reproducibility

```
make reproducible-check
```

Gate: `PASS - ELF byte-identical across two clean builds`.

## 3. Boot gates

```
make run-bios      (banner + 233 commands + oc> + boot complete)
make run-uefi      (same, through OVMF)
```

## 4. Deep test gates (on the built ISO)

| Gate | Command | Must |
|------|---------|------|
| Regression battery | `tools/run_fix4_regression.py` | 28/28 |
| Watchdog/logging/health E2E | `tools/wp12_watchdog_logging_e2e.py` | 14/14 |
| dhtest + cryptotest | in-guest | PASS |
| SSH E2E | paramiko suite (seeded /etc) | 5/5 |
| HTTPS E2E | reference server | 7/7 |

## 5. Tag + Release

```
git tag WP-<stage>
git push origin main --tags
```

GitHub Release from the tag with the note containing: change summary,
new interfaces, known issues, the three SHA256s. Attach the three
assets. The release script (`tools/build_release.sh`) automates
manifest + note + upload.

## 6. Website + wiki

```
tools/update_website.sh    (versions/hashes/downloads on the site)
tools/sync_wiki.sh         (docs/ -> the GitHub Wiki)
```

Gate: the live site shows the new version, the three SHA256s match
the Release, downloads resolve HTTP 200 and hash-verify.

## 7. Private mirror

Copy the three assets + release note + the mirrored website files
into the private repository; push. Gate: `git ls-remote` on both
repos shows the expected HEADs.

## 8. The 12-point check

1. Both repos' remote HEADs match local.
2. The tag exists on the public repo.
3. Author identity on every release commit.
4. gh-pages updated (live site serves the new version).
5. update.json: version + build + package_sha256.
6. Three-way asset hashes: local = Release API digest = live
   download.
7. Website numbers (commands/interfaces/lines) match
   `docs/data/*.json`.
8. The gates in step 4 all green (paste the outputs).
9. Token hygiene (only last-4 ever shown).
10. Whole-repo number consistency (233 / 162 / 140,672-class).
11. Private mirror complete.
12. CI green on the release commit.

## 9. Report

The delivery report quotes: build evidence, all gate outputs, push
outputs (public + private), the Release URL, the site URL, the three
SHA256s, and the honest-deviation list.
