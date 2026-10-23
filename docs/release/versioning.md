<!-- SPDX-License-Identifier: Apache-2.0 -->
# Versioning

## The scheme

Version identifiers are **work-package stage names**:
`WP-14-docs_release`, `WP-13-build_system`, ... This is the project's
SemVer-equivalent: the stage name IS the compatibility contract
conversation (each WP is a release train with its own feature set
and gates).

Additionally every build carries:

- **Build number** = the commit count (`git rev-list --count HEAD`)
  at build time - monotonically increasing, visible in `uname -a`
  and the update manifest's `build` field.
- **Version banner** = `git describe --tags` (or an explicit
  `OC_RELEASE_VERSION=` override) - baked into the kernel banner,
  uname, the OTA manifest and the ISO.

## What the numbers mean together

| Where | Example | Meaning |
|-------|---------|---------|
| `uname -a` | `Open Cube OS WP-13-build_system build 259` | the running kernel's stage + build |
| update.json | `version: WP-13-build_system, build: 259` | the published manifest |
| Release tag | `WP-13-build_system` | the git tag the assets were built from |
| gh-pages | the same two numbers in the download page | the website mirrors the Release |

One release = one tag = one build number = one manifest = one set of
SHA256s across local / Release / gh-pages / private mirror.

## Compatibility promises

- Shell commands are never removed; additions are the norm
  (baseline moves UP: 176 -> 192 -> ... -> 233).
- L1 numbered interfaces are never renumbered; new groups append.
- The OTA package updates the kernel only; /etc volume contents
  carry forward.

## Choosing the next version

The next work package's stage name (decided in the development
plan), with `build` = whatever the commit count is on release day -
never hand-picked.
