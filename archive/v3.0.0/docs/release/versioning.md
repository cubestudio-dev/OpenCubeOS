<!-- SPDX-License-Identifier: Apache-2.0 -->
# Versioning

## The scheme

Since the v1.0.0 public release, version identifiers are **SemVer**:
`1.0.0`, `2.0.0`, `2.1.0`, ... Before that, the engineering phase used
work-package stage names (`WP-14-docs_release`, `WP-13-build_system`,
...) - the internal equivalent of a release train per feature set.
Since v2.0.0 the tags are plain `v<version>` (`git tag v2.1.0`), and
`WP-*` tags exist only in history.

Additionally every build carries:

- **Build number** = the commit count (`git rev-list --count HEAD`)
  at build time **plus the permanent +21 alignment offset**
  (`OC_KERNEL_BUILD_OFFSET := 21` in the Makefile). The offset dates
  from v1.0.0: the published build ids had been baked from an
  engineering tree whose git history ran 21 commits longer than the
  public repo main (WP-14-docs_release shipped build 274 at public
  count 253), and the OTA update check compares build ids
  NUMERICALLY (BUG-0347) - so the public sequence must continue
  above the already-published 274. One-build-per-commit monotonicity
  is preserved: the v1.0.0 tag sits at public count 257 -> build 278;
  v2.0.0 at count 281 -> build 302.
- **Version banner** = the frozen `OC_RELEASE_VERSION ?=` literal in
  the Makefile (an explicit `OC_RELEASE_VERSION=` override still
  wins, which is how the E2E update test works) - baked into the
  kernel banner, uname, the OTA manifest and the ISO. Earlier builds
  derived it from `git describe --tags`; v1.0.0 froze it to a
  literal so the banner no longer depends on local git state.

## What the numbers mean together

| Where | Example | Meaning |
|-------|---------|---------|
| `uname -a` | `Open Cube OS 2.1.0 x86_64` | the running kernel's version |
| update.json | `version: 2.1.0, build: <n>` | the published manifest |
| Release tag | `v2.1.0` | the git tag the assets were built from |
| gh-pages | the same two numbers in the download page | the website mirrors the Release |

One release = one tag = one build number = one manifest = one set of
SHA256s across local / Release / gh-pages / private mirror. The tag
must point at a CI-green commit (2.1.0 release gate).

## Compatibility promises

- Shell commands are never removed without a declared deletion
  register; additions are the norm. The v1.0.0 baseline of 233
  commands is preserved whole: every one is mapped in the
  `INTERFACES.md` Part 1c compatibility contract (251 v2 command
  homes = 99 L0 + 133 SVC + 19 built-ins).
- L1SVC op numbers are never renumbered; new ops append
  (17 categories / 158 ops at 2.1.0). The retired v1 numbered
  surface's fate is per-entry truth: `INTERFACES.md` Part 2c/2d.
- The OTA package updates the kernel only; /etc volume contents
  carry forward.
- SemVer from here on: breaking changes bump MAJOR, new commands or
  L1SVC ops bump MINOR, fixes bump PATCH.

## Choosing the next version

SemVer rules above; `build` = commit count + 21 on release day -
never hand-picked.
