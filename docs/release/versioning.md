<!-- SPDX-License-Identifier: Apache-2.0 -->
# Versioning

## The scheme

Since the v1.0.0 public release, version identifiers are **SemVer**:
`1.0.0`, `1.1.0`, ... Before that, the engineering phase used
work-package stage names (`WP-14-docs_release`, `WP-13-build_system`,
...) - the internal equivalent of a release train per feature set.

Additionally every build carries:

- **Build number** = the commit count (`git rev-list --count HEAD`)
  at build time - monotonically increasing, published in the update
  manifest's `build` field (the banner/uname carry the version).
- **Version banner** = the fixed `OC_RELEASE_VERSION ?= 1.0.0`
  literal in the Makefile (an explicit `OC_RELEASE_VERSION=` override
  still wins, which is how the E2E update test works) - baked into
  the kernel banner, uname, the OTA manifest and the ISO. Earlier
  builds derived it from `git describe --tags`; v1.0.0 froze it to a
  literal so the banner no longer depends on local git state.

## What the numbers mean together

| Where | Example | Meaning |
|-------|---------|---------|
| `uname -a` | `Open Cube OS 1.0.0 x86_64` | the running kernel's version |
| update.json | `version: 1.0.0, build: 275` | the published manifest |
| Release tag | `v1.0.0` | the git tag the assets were built from |
| gh-pages | the same two numbers in the download page | the website mirrors the Release |

One release = one tag = one build number = one manifest = one set of
SHA256s across local / Release / gh-pages / private mirror.

## Compatibility promises

- Shell commands are never removed; additions are the norm
  (baseline moves UP: 176 -> 192 -> ... -> 233).
- L1 numbered interfaces are never renumbered; new groups append.
- The OTA package updates the kernel only; /etc volume contents
  carry forward.
- SemVer from here on: breaking changes bump MAJOR, new commands or
  L1 groups bump MINOR, fixes bump PATCH.

## Choosing the next version

SemVer rules above; `build` = whatever the commit count is on release
day - never hand-picked.
