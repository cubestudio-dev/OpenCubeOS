<!-- SPDX-License-Identifier: Apache-2.0 -->
# Wiki synchronization mechanics

`tools/sync_wiki.sh` - the docs/ tree also lives on the GitHub Wiki.

## What syncs

The Wiki gets the curated page set (Home, Getting Started, Building,
Architecture Overview, Debugging & QEMU, L1 Extension Guide, User
Manual, FAQ, Release Notes, plus the per-domain manuals) rendered
from the same `docs/` sources - one authoring surface, two outputs
(docs center on the website, Wiki on GitHub).

## How

1. The script maps `docs/` pages onto Wiki page names (the mapping
   table lives in the script).
2. Rewrites relative links to Wiki page links.
3. Clones `{repo}.wiki`, writes the pages, commits, pushes.

## Verification

- Every mapped Wiki page exists (the script prints the list).
- Link check: no red links among the mapped set.
- Content equality: the Wiki page and the docs page render the same
   markdown source (the script diffs them before pushing).

## When it runs

At release time (after docs/ changes land) and on demand for docs-
only updates.
