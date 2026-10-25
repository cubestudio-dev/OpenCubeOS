<!-- SPDX-License-Identifier: Apache-2.0 -->
# The release process (SOP)

The exact sequence a release walks. Every step has a gate; a failed
gate stops the release.

## 0. Pre-flight

```
git fetch && git status        (clean tree, correct branch)
git tag --list 'v*'           (the new tag does not exist yet)
git config user.name / user.email   (author = cubestudio-dev <cubestudio@qq.com>)
```

## 1. Build

```
make clean && make -j$(nproc) all
```

Gate: 0 errors 0 warnings (`-Wall -Wextra -Werror` is in the flags)
**on BOTH the local toolchain and the GitHub Actions runner** - the
2.0.0 release shipped source that compiled locally but not on CI
(the tui_core.c port-constraint P0; see KNOWN_ISSUES.md). CI green
is a hard release gate, not a nice-to-have.
Outputs: `build/opencube.iso`, `dist/OpenCubeOS-<ver>-src.zip`,
`dist/OpenCubeOS-<ver>-update.tar.gz`.

## 2. Reproducibility

```
make reproducible-check
```

Gate: `PASS - ELF byte-identical across two clean builds`.

## 3. Boot gates

```
make run-bios      (banner + ring-3 ush> + boot complete)
make run-uefi      (same, through OVMF)
```

The default boot lands in the ring-3 `ush>` shell; `oc>` appears
after `exit` (or on launcher failure - the recovery posture).

## 4. Deep test gates (on the built ISO)

The 2.1.0 verification matrix - every suite drives a real QEMU boot
over the serial console; paste the real outputs (skip counts
included) into the release evidence:

| Gate | Command | What it proves |
|------|---------|----------------|
| Host unit tests | `make test` | 17 harnesses PASS (+2 SKIP-FIXTURE without openssl fixtures - declare the skips) |
| Watchdog/logging/health E2E | `tools/wp12_watchdog_logging_e2e.py` | 14 scenarios (S1-S14 incl. Intel TCO real reset) |
| TUI command surface E2E | `tools/wp10_tui_e2e.py` | the L0 TUI commands on the v2 boot flow |
| Chinese console E2E | `tools/wp10_chinese_e2e.py` | the CJK layer end to end |
| TUI desktop apps E2E | `tools/wp10_tuiqa_apps_e2e.py` | the desktop five-app flow from `ush>` |
| Native desktop E2E | `tools/wp10_native_e2e.py` | the native editor/file-manager flows |
| L1 per-app record | run every `l1apps/` program via `run` from ush | ush + each embedded program actually executes (2.1.0 addition; see docs/dev/e2e-suites.md) |

## 5. Tag + Release

```
git tag -a v<version>          # ONLY on a CI-green commit
git push origin main --tags
```

GitHub Release from the tag with the note containing: change summary,
new interfaces, known issues, the three SHA256s. Attach the three
assets. The release script (`tools/build_release.sh --deep`)
automates manifest + note + upload.

**Release gate (2.1.0): the tag must point at the CI-green commit.
Never tag a red commit.**

## 6. Website + wiki

```
tools/update_website.sh    (versions/hashes/downloads on the site)
tools/sync_wiki.sh         (docs/ -> the GitHub Wiki)
```

Gate: the live site shows the new version, the three SHA256s match
the Release, downloads resolve HTTP 200 and hash-verify, and the
site stats block is REGENERATED from `docs/data/*.json` (never
copied from a stale block - the 2.0.0 site shipped a stale 130,011
line count that way).

## 7. Private mirror

Copy the three assets + release note + the mirrored website files
into the private repository (`oc-os/` tree + repo-level audit
documents); push. Gate: `git ls-remote` on both repos shows the
expected HEADs.

## 8. The 12-point check

1. Both repos' remote HEADs match local.
2. The tag exists on the public repo and points at a CI-green commit.
3. Author identity on every release commit.
4. gh-pages updated (live site serves the new version).
5. update.json: version + build + package_sha256 (both copies -
   website/ top level AND website/public/ - byte-identical).
6. Three-way asset hashes: local = Release API digest = live
   download.
7. Website numbers (commands/interfaces/lines) match
   `docs/data/*.json` (regenerated, not copied).
8. The gates in step 4 all green (paste the outputs, skip counts
   declared).
9. Token hygiene (only last-4 ever shown).
10. Whole-repo number consistency: 251 command homes (99+133+19) /
    17 categories / 158 L1SVC ops / 54 syscalls / the current
    stats.json line count (recomputed every release; never a stale
    figure).
11. Private mirror complete.
12. CI green on the release commit (and on the tagged commit).

## 9. Report

The delivery report quotes: build evidence (local + CI), all gate
outputs, push outputs (public + private), the Release URL, the site
URL, the three SHA256s, and the honest-deviation list.
