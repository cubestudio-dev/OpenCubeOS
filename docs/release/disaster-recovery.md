<!-- SPDX-License-Identifier: Apache-2.0 -->
# Release deployment and disaster recovery (maintainer guide)

How artifacts reach the world, how the working environment is rebuilt
after a sandbox reset, and how to push when the sandbox's egress
breaks. Companion pages: [process](process.md) (the SOP),
[ci-gates](ci-gates.md) (the gates), [website-update](website-update.md)
and [wiki-sync](wiki-sync.md) (the two sync mechanics in depth).

## 1. The deployment surface

| Channel | Content | Mechanism |
|---------|---------|-----------|
| GitHub repo `main` | source of truth | git push (or the API replay channel, §6) |
| GitHub Releases | ISO + src zip + OTA tar.gz + note with the three SHA256s | Release UI / API from the tagged, CI-green commit |
| `gh-pages` branch | the website: facade + docs center + OTA manifests | `website/build.sh` output pushed to the branch |
| Custom domain | `ocos.cubestudio.cn` | CNAME in the deploy root (GitHub Pages cname) |
| GitHub Wiki | curated bilingual docs view | `tools/sync_wiki.sh` |
| Private mirror | assets + release note + mirrored website files + audit docs | repo-to-repo copy (below) |

## 2. Two-repo synchronization rules

The project keeps a public repo (`cubestudio-dev/OpenCubeOS`) and a
private mirror. The mirror rules (the canonical statement is
[process](process.md) §7 + the mirror's own SYNC-RULES.md):

1. **What syncs**: the three release assets, the release note, the
   mirrored website files, and repo-level audit documents (postcheck
   reports, work log). The mirror is the audit trail of record — a
   full postcheck is archived there per release.
2. **What does NOT sync**: build products (`build/`, `dist/`, `iso/`,
   `releases/`) and the website build trees (`website/out`,
   `website/.next`, `website/node_modules`,
   `website/public/downloads`) — the same exclusion philosophy the
   `Makefile` `SRC_EXCLUDE` applies to the source zip. The mirror
   holds sources, evidence and assets, never transient build state.
   Website *outputs* reach users via gh-pages, not via the mirror.
3. **Gate**: `git ls-remote` on BOTH repos shows the expected HEADs
   (12-point check item 1; see [ci-gates](ci-gates.md) §4). Assets
   restored into the mirror are SHA-verified against the Release (the
   2.0.0 assets were re-fetched from a Release and hash-verified this
   way).
4. **Byte-identity promise**: the Release, gh-pages downloads and the
   mirror hold byte-identical copies of each asset (P-11 lesson: the
   public main tree must also carry them under
   `website/public/downloads/`).

## 3. Sandbox reset — the standard recovery procedure

The canonical sandbox layout (the one the tooling assumes):
`/home/z/ocos` = the repo, `/home/z/opt/extract` = the relocated
toolchain, `$HOME/.cache/opencube-cc` = the compile cache. After a
sandbox reset, rebuild the environment in this order:

1. **Restore the repo** — clone (or re-play from a bundle, §5) to
   `/home/z/ocos`. Confirm the working tree is clean and on the
   expected branch; the build id derivation needs the full history
   (`git rev-list --count HEAD`), so never restore a shallow copy.
2. **Unpack the toolchain (no root needed)** —
   `bash tools/install-toolchain.sh`: downloads the Debian .debs in
   three batches (build tools / qemu / grub+firmware, ~117 packages
   including dependencies) and `dpkg -x`-extracts them into
   `$OC_OPT` (default `/home/z/opt/extract`): nasm 2.16.03,
   qemu-system-x86 10.0.13 (+ data + libs), xorriso 1.5.6,
   grub-pc-bin + grub-efi-amd64-bin + grub-common, mtools, ovmf,
   seabios. gcc itself comes from the system (Debian gcc 14.2.0 is the
   release compiler).
3. **Source the environment** — `source tools/env-sandbox.sh`
   (the repo's sandbox environment script): exports
   `OC_TOOLS=/home/z/opt/extract`, prepends `$OC_TOOLS/usr/bin` to
   PATH, and sets `LD_LIBRARY_PATH` to the extract's lib dirs (the
   relocated QEMU needs them). Every QEMU-driving tool also honors
   `OC_TOOLS`.
4. **QEMU/OVMF placement check** — `OVMF_FD` defaults to
   `$(OC_TOOLS)/share/ovmf/OVMF.fd`, `QEMU_DATADIR` to
   `$(OC_TOOLS)/share/qemu`, `SEABIOS_DIR` to
   `$(OC_TOOLS)/share/seabios`. Verify with `make run-bios` on the
   freshly built ISO; if the runner's OVMF build needs the split
   pflash pair, `OVMF_CODE_4M.fd` + `OVMF_VARS_4M.fd` are used by
   `build_release.sh`'s UEFI gate (plain `-bios` silently fails to
   run the payload on current OVMF builds).
5. **Evidence baseline** — restore/rebuild `evidence/` captures the
   gates consume: `evidence/help_a_runtime.txt` (the runtime
   `help -a` capture behind `make api-docs`) and the release gate
   logs. Then run the cheap verification ladder:
   `make clean && make all` (0-warning gate) →
   `make reproducible-check` → `make run-bios` / `make run-uefi`
   banner gates. A full recovery is proven by
   `tools/build_release.sh <ver> --deep` passing end to end.
6. **Compile cache** — optional; wipe or keep
   `~/.cache/opencube-cc` (`make cache-clear`); hits are
   byte-identical so the cache never invalidates a gate.

## 4. Website update flow

`website/build.sh` (run as the last `build_release.sh` step, after
`tools/update_website.sh` has written the release numbers):

1. Installs docs-center dependencies once (`npm install` → vitepress).
2. Syncs the repo `docs/` tree into the build-time copy
   `docs-src/docs/` via `rsync -a --delete` (excluding `archive/`
   and `data/`) — the repository docs/ is the single source of truth;
   the copy is git-ignored.
3. Builds the docs center (`npm run docs:build`; VitePress, EN root +
   `zh/` locale, per-locale search indexes; ~642 pages, 777 on the
   WP-l1apps-merge full rebuild).
4. Assembles the deploy root `out/`: facade pages
   (`index/architecture/download/about.html`), `assets/`,
   `out/docs/`, plus the three deploy-critical files the hard way:
   - `out/CNAME` — content `ocos.cubestudio.cn` (custom domain;
     previously only on the gh-pages branch, so a full-tree redeploy
     could silently drop it — WP06d-site_fix),
   - `out/.nojekyll` — empty; stops GitHub Pages from running Jekyll
     over the VitePress output,
   - `out/update.json` + `out/update-v2.json` — the OTA manifests
     written by `update_website.sh` (release-managed content), copied
     so the kernel's checkupdate and the download-page footer link
     never 404.
5. The `out/` tree is committed to `gh-pages` and pushed (§5–§6 for
   the channel). Pages rebuilds; verify online (allow a minute):
   the docs center, a known deep page (byte-compare with the local
   build), and the facade's version marker. The only tolerated
   live-vs-local difference is the CDN's email-obfuscation injection.

`update_website.sh <ver> <build> [date] [changes]` must run BEFORE the
build: it reads the dist assets and writes
`website/assets/site-data.js` + `website/update.json` +
`update-v2.json` — nothing on the site is hand-edited (details in
[website-update](website-update.md)).

## 5. Wiki synchronization

`tools/sync_wiki.sh [--dry-run]` (env: `GH_TOKEN` with wiki push
rights) maps `docs/` + `docs/zh/` onto the GitHub Wiki
(`cubestudio-dev/OpenCubeOS.wiki.git`):

- Chinese is the canonical page under the mapped name; English ships
  as `<Page>-EN.md`; every page carries a language banner linking to
  its counterpart.
- The `map_page()` table is the contract: ~83 mapped Wiki page names
  (Home, Getting-Started, Mega-Apps, Boot-Modes, Shell-Basics,
  Filesystems, Networking, Update-and-OTA, TUI-Guide,
  Chinese-Console, Recovery, User-Manual, Command-Reference,
  Building-from-Source, Architecture-*, L1-*, Drivers-*,
  Internals-*, Troubleshooting-*, Versioning, Release-Process, …)
  plus the `Release-Notes = GENERATED` sentinel. Pages NOT in the
  table are reachable only through the docs center — the Wiki is the
  curated view, the docs center is the full tree.
- Links are rewritten to `[[Wiki-Links]]` per language (zh → canonical
  names, en → `-EN`-suffixed), directory links collapse to the
  section index page, anchors are dropped (zh headings are localized),
  relative assets are stripped.
- Runs at release time after docs/ changes land, and on demand for
  docs-only updates. Verification: every mapped page exists, no red
  links among the mapped set, and the script diffs Wiki vs docs before
  pushing (see [wiki-sync](wiki-sync.md)).

## 6. The restricted-egress push channel (api.github.com replay)

**The failure**: in the release sandbox, `git push` over smart-HTTP
persistently dies with a GnuTLS error (-110, the connection-terminated
/ TLS record overflow class). The `git` channel
is therefore unusable exactly when a release must ship.

**First-line workaround** (keeps using git where possible):
`deploy_with_token.sh <GH_TOKEN>` — a fresh `git init` scratch repo,
`git config http.version HTTP/1.1`, fetch the remote base refs, fetch
the prepared commits from thin `*.bundle` files (bundles carry ONLY
the new commits; prerequisites = the current remote tips, so the
pushes are fast-forwards), push `main` + `gh-pages`, then PATCH the
Release body via the REST API. Requires no local clone of the repo.
When even this push dies (the observed case), fall through to:

**The API replay channel — `api_push3.py`** (hierarchical
chunk-accumulation tree builder over the Git Data API):

```
python3 api_push3.py <repo-path> <slug> <token> <branch> <base> <head> \
    [--threads 6] [--chunk 400] [--state file.tsv]
```

Why it exists (both alternatives fail):
- a FLAT full-tree POST is 422-rejected by GitHub on ~2600-entry
  payloads ("request timed out / input too large") — non-retryable;
- `base_tree` + null-sha deletions silently KEEP the null-sha entries
  (observed in the wiki/deploy work).

How it works per commit being replayed:
1. **Base verification first**: the remote branch head's tree SHA must
   equal the local `<base>` tree SHA — a mismatch aborts with
   `FATAL: ... replay base mismatch` (exit 2) so the replay can never
   land on top of an unknown remote state.
2. **Threaded blob upload** for changed files: skip when the content
   SHA already exists anywhere in the remote tree; POST
   `/git/blobs` (base64) otherwise; verify the returned SHA equals
   the local one (`blob sha drift` is fatal); progress and completion
   are appended to a `--state` TSV under a lock — the resumable
   checkpoint after an interruption.
3. **Dirty-dirtree rebuild, deepest first**: each dirty directory is
   rebuilt from its FINAL children set in ≤ `--chunk` (default 400)
   accumulation passes (first pass plain, later passes
   `base_tree=<prev> + next chunk`). Deletions never appear because
   the child set is final. **Every directory's resulting tree SHA is
   hard-verified against the local git tree SHA** — any level
   mismatch aborts (`FATAL: dir '<d>' tree <sha> != local <sha>`,
   exit 3).
4. **Root tree verified == the local commit's tree SHA**, then the
   commit is created with preserved author + committer (name, email,
   ISO dates — a bit-exact replay when the parent chain matches).
5. **Ref update**: `PATCH /git/refs/heads/<branch>` with
   `force: false` — fast-forward only. Retry policy: 502/503/504/429
   retried with backoff, everything else fatal with the response body.

Known pitfalls (all fixed or documented in the tool):
- the ~2600-entry whole-tree POST → 422 (api_push2.py, the flat
  version, is kept only as an archive of this failure);
- the print lock was not re-entrant → a nested log call inside a
  worker thread deadlocked the whole run; now an `RLock`;
- interrupted runs resume from the state file — never delete it
  mid-run, and always re-run with the same `--state` path.

Recorded usage (the WP-l1apps-merge close-out, 2026-11-15): both
`main` (tree `e964148f`) and `gh-pages` (tree `3cd102cb`) were
replayed this way and verified online — docs center, mega-apps page,
release page byte-identical with local builds; screenshots archived
under the deploy kit's `online-proof/`.

## 7. The persistent disaster-recovery directory

The sandbox resets; some things must outlive it. Convention: a
`download/` directory that is NOT part of the sandbox's reset scope
holds the release-survival kit:

- **Bundles**: `main-new.bundle` / `gh-pages-new.bundle` — thin
  git bundles carrying only the new commits. If the remote baseline
  has not moved, they are directly re-playable (`git fetch <bundle> <branch>` then push, or the deploy script's flow).
- **The deploy scripts themselves**: `deploy_with_token.sh` (git
  channel), `api_push2.py` (flat-tree archive), `api_push3.py` (the
  working channel).
- **State + logs**: `state-*.tsv` (blob resume checkpoints),
  `push-*.log` (execution logs) — keep them with the tooling that
  wrote them.
- **The release body**: `release-body.md` — the exact note text, so a
  Release can be re-created or re-PATCHed without reconstructing it.
- **Evidence**: `online-proof/` screenshots and `preview-*.png` —
  the "it is live" record for the delivery report.

Rules of the directory: it mirrors only what the release chain
produces (assets live in Releases/gh-pages, not here); every file is
re-creatable from the repo + the Release, but keeping it turns a
2-hour recovery into a 10-minute one. After each release, refresh the
kit (new bundles if commits moved, new state files, new proof) and
prune anything older than the last two releases.

## 8. Recovery checklist (one page)

```
# environment
bash tools/install-toolchain.sh          # once; $OC_OPT=/home/z/opt/extract
source tools/env-sandbox.sh              # OC_TOOLS + PATH + LD_LIBRARY_PATH

# prove the tree
cd /home/z/ocos && git status            # clean, full history
make clean && make -j all                # 0-warning gate
make reproducible-check                  # byte-identical ELF
make run-bios && make run-uefi           # banner gates

# release data + site
tools/update_website.sh <ver> <build> <date>
website/build.sh                         # out/ = deploy root

# push (pick the channel that works)
git push origin main                     # normal
./deploy_with_token.sh $GH_TOKEN         # bundle + HTTP/1.1 + API PATCH
python3 api_push3.py <repo> <slug> $GH_TOKEN main <base> <head> --state s.tsv

# verify
tools/sync_wiki.sh
# ...then the 12-point check (ci-gates.md §4)
```
