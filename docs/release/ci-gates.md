<!-- SPDX-License-Identifier: Apache-2.0 -->
# CI/CD and release-chain gates (maintainer guide)

What `.github/workflows/build.yml` enforces on every push, what the
release chain checks before a tag may move, and how the regression
floor is enforced. Companion pages: [process](process.md) (the SOP
these gates hang on), [build-reference](build-reference.md) (the
pipeline CI builds), [guard-scripts](guard-scripts.md) (the static
guards).

## 1. The workflow — shape and provenance

`.github/workflows/build.yml` is the repo's single CI workflow:

- **Name** `build`; **triggers**: push to `main`, PR to `main`,
  `workflow_dispatch`; **permissions**: `contents: read`; one job
  `3.0.0 build (0-warning gate)` on `ubuntu-latest`,
  `timeout-minutes: 40`.
- **Honest provenance note** (declared in the file header): the 3.0.0
  mainline had NO CI workflow at its last mainline commit — nothing to
  restore. This file is NEW WP00 infrastructure so "CI green" remains
  an enforceable gate. **It builds; it does not boot** — every QEMU
  run is a sandbox-only acceptance step.
- Why build-only CI is acceptable here: the zero-warning gate plus the
  deterministic build catch most regressions mechanically, and the
  boot/E2E matrix (§5) runs in the release sandbox via
  `tools/build_release.sh --deep`. The process compensates with a hard
  rule: **the tag must point at a CI-green commit — never tag a red
  commit** (see [process](process.md) step 5).

## 2. The six CI steps, one by one

### Step 1 — Checkout with `fetch-depth: 0`

`actions/checkout@v4` with full history. **Mandatory, not cosmetic**:
the build id is `git rev-list --count HEAD + 21` (the `Makefile`
`OC_KERNEL_BUILD` derivation), and the OTA update check compares build
ids NUMERICALLY (BUG-0347: remote > local only). A shallow clone would
bake a build id far BELOW the published ones (278/321/414/416), and
every installed machine would silently ignore the release as a
"downgrade". A wrong-but-green CI build is worse than no build at all
— hence full depth is step zero.

### Step 2 — Install toolchain

```
sudo apt-get install -y -qq build-essential nasm xorriso mtools \
  grub-pc-bin grub-efi-amd64-bin grub-common
```

Exactly the package set `tools/build.sh` probes for (plus
build-essential for gcc/ld). Missing any of them fails the next step
with the compiler/packer's own error.

### Step 3 — Select compiler (prefer gcc-14)

Install `gcc-14` (best effort, `|| true`), then emit `cc=gcc-14` when
`command -v gcc-14` succeeds, else `cc=gcc`. gcc-14 is the release
compiler (Debian gcc 14.2.0 — the compiler the release builds have
always used); the fallback keeps the workflow from hard-failing on
runner image changes, at the cost of a slightly different codegen —
which is itself caught by the artifact assertions in step 5.

### Step 4 — Build (`make all`) — the CI "lint"

<code v-pre>make all CC=${{ steps.cc.outputs.cc }}</code> runs the full WP-13 pipeline:
userprogs → kernel (two-stage link) → dual-firmware OCBL ISO →
deterministic src zip → OTA package. The lint IS the build: every
component Makefile (kernel, OCIRE, OCBL, OCSE, agent) carries
`-Wall -Wextra -Werror`, so any warning anywhere fails `make` with the
compiler's diagnostic and a non-zero exit. This is the gate that
caught the 2.0.0-era "compiles locally but not on CI" class (the
tui_core.c port-constraint P0) — which is why the SOP demands
0-warning on BOTH the local toolchain and the runner.

### Step 5 — Verify artifacts

```
file build/opencube.elf            → grep 'ELF 64-bit'
file build/opencube.iso            → grep 'ISO 9660'
test -s build/opencube.elf         (non-empty)
test -s build/opencube.iso
xorriso -indev ... -report_el_torito plain
  → grep 'El Torito boot img : 1 BIOS y none'
  → grep 'El Torito boot img : 2 UEFI y none'
test -s dist/OpenCubeOS-src-3.0.0-NAK.zip
test -s dist/OpenCubeOS-3.0.0-NAK-update.tar.gz
```

**The three hard assertions** (each greps the real artifact, so a
wrongly-shaped product cannot pass):

1. **Type assertions** — the ELF must be `ELF 64-bit` and the ISO must
   be `ISO 9660`, and both must be non-empty. A truncated or
   mis-linked product fails here.
2. **Dual-firmware El Torito assertion** — the xorriso report must
   show BOTH `El Torito boot img : 1 BIOS y none` AND
   `: 2 UEFI y none`. This is the machine check that the same ISO
   carries the BIOS and the UEFI boot images (the OCBL dual-firmware
   promise); a BIOS-only or UEFI-only ISO fails here even though it
   "built fine".
3. **Completeness assertion** — the src zip and the OTA tar.gz must
   exist and be non-empty (the pipeline did not silently skip a leg).

### Step 6 — Verify `make clean` leaves no products

```
make clean
left=$(find build dist -type f 2>/dev/null | wc -l)
test "$left" -eq 0
```

Zero files may remain under `build dist`. This catches rule-hygiene
regressions: artifacts written outside `$(BUILD)`/`$(DIST)`, or files
the clean rule forgot, would leave `left > 0` and fail the step.

## 3. What CI does NOT run (and where those gates live instead)

`make test` (host unit harnesses), every QEMU boot, the 31-check
regression, and the E2E suites never run in CI. They are
release-time gates in the sandbox:

- `tools/build_release.sh <ver> --deep` (SOP steps 1–5: clean build →
  reproducible-check → BIOS+UEFI boot banner gates → deep gates →
  docs/website data) — the boot gates grep a FRESH mktemp log per run
  (BUG #63 fix: the old fixed `/tmp/rel_boot.log` let a stale log
  satisfy the greps — a fake green).
- The verification matrix (`make test`, `run_fix4_regression.py` 31/31,
  `ssh_g11a_e2e.py` 5 checks, `run_fix4_https_e2e.py` 8 checks,
  `wp12_watchdog_logging_e2e.py` S1–S14, the `wp10_*` suites, the
  mega-app smoke 21/21) — see [tool-inventory](tool-inventory.md) for
  the tools and [process](process.md) step 4 for the matrix.

## 4. The release chain — the 12-point check, item by item

Run after the assets are live (from [process](process.md) §8). Every
item is a pass/fail question with a concrete verification action:

1. **The remote HEADs match local** — `git ls-remote` on the public
   repo shows the expected SHAs (the push actually landed, nowhere is
   half-updated).
2. **The tag exists on the public repo and points at a CI-green
   commit** — the Actions run for that exact SHA is green; never tag a
   red commit.
3. **Author identity on every release commit** — `cubestudio-dev <cubestudio@qq.com>`; `build_release.sh` refuses to start otherwise.
4. **gh-pages updated** — the live site serves the new version
   (fetch the site, check the version marker).
5. **`update.json` consistency (both copies)** — version + build +
   `package_sha256` present, and the two copies (website/ top level
   AND `website/public/`) are byte-identical.
6. **Three-way asset hashes** — see §5 below.
7. **Website numbers regenerated from `docs/data/*.json`** — the
   commands/interfaces/lines counters match the freshly generated
   JSON, never a copied stale block (the 2.0.0 site shipped a stale
   130,011 line count that way).
8. **The step-4 gates all green** — paste the real outputs into the
   release evidence, skip counts declared.
9. **Token hygiene** — only the last 4 characters of any token ever
   appear in logs/reports.
10. **Whole-repo number consistency** — the command-home /
    category / L1SVC-op / syscall counts and the stats.json line count
    are recomputed every release, never carried over.
11. **CI green on the release commit AND on the tagged commit** — the
    same SHA checked twice, because a tag can move.

## 5. SHA256 three-way verification

The release note publishes the SHA256 of each of the three assets
(ISO, src zip, OTA tar.gz). The check proves the SAME bytes reached
every channel:

1. **Local**: `sha256sum` over the three `dist/`/`build/` artifacts
   (`build_release.sh` prints them at the end of a gated run).
2. **Release API**: fetch the GitHub Release asset metadata via the
   API and compare the digest the platform computed on upload — this
   catches truncated or wrong-version uploads.
3. **Live download**: download the asset from the public URL a user
   would use and hash it again — this catches CDN/cache/redirect
   drift between the Release object and what users actually get.

All three must be equal for each asset. The same discipline extends
to `update.json` (the OTA manifest carries `package_sha256`, so the
kernel's own updater re-verifies at install time) and to the site
(`website/public/update.json` byte-identical with `website/update.json`).

## 6. The regression floor: 276 minimum, 284 measured

`tools/run_fix4_regression.py` boots the built ISO (512M,
`-serial mon:stdio`, e1000 user-net, an HMP monitor unix socket for the
framebuffer assertion), waits for `ush>`, runs 24 commands, and scores
**31 checks**; the exit code equals the number of failed checks (the
release matrix quotes 31/31).

The command-count check (check 6 of 31) is the **interface-freeze
floor**:

- **Logic**: the count is measured dynamically from the `help -a`
  registry listing (two-space-indented `name  [tag]  description`
  rows) — never a stale literal (the old gate grepped
  `Total: 233 commands.`, a string that no longer exists in the
  output at all).
- **Floor**: `n >= 276`. 276 = the g3 baseline (275) + p0leak (the
  WP-fix-critical-security P0 fence probe that rides the gate so every
  future release re-proves it).
- **Current measurement**: 284 commands on the WP-l1apps-merge tree —
  the floor is 8 under the measured surface, so adding commands is
  always fine and removing even one fails the gate.
- **Rule behind it**: commands may be ADDED, never removed. When the
  measured count rises permanently, the floor literal is raised in the
  same commit that adds the commands (never before).

Anti-fake-green mechanics baked into the same gate (why the floor
cannot be satisfied by accident):

- Every pattern is scoped to ONE command's output section (BUG #64
  fix: patterns used to match the WHOLE combined log, so command
  echoes satisfied name-bearing patterns and any PASS anywhere
  satisfied every check — the gate was structurally always-true).
- The echoed command line is stripped before matching.
- The framebuffer check requires an HMP screendump with ≥ 2 colors and
  a measurable foreground ratio (BUG #66).
- The `g6_test` async tally gets its own 300-second wait window.

## 7. build_release.sh — the gated front door

`tools/build_release.sh <version> [--deep]` automates SOP steps 1–5
and refuses to run when: the tag `<version>` already exists, or the
git identity is not `cubestudio-dev`. Sequence: clean `make -j all`
(0-warning gate) → `make reproducible-check` → BIOS boot gate → UEFI
boot gate (pflash CODE + writable VARS copy; the plain `-bios` form
silently fails to run the payload on current OVMF builds) → optional
`--deep` gates (`run_fix4_regression.py`, `wp12_watchdog_logging_e2e.py`)
→ `gen_api_docs.py --pages --pages-zh --md` + `update_website.sh` +
`website/build.sh` → prints the three SHA256s. It pushes NOTHING —
review the outputs, then run the release chain manually (deliberate
asymmetry; the push-channel runbook is maintained by the maintainers
outside the public docs).

The older `tools/github_release_wp08.sh` (WP-08 era) pushes
commit+tag and creates the Release with the ISO asset via the API
(`GITHUB_TOKEN` with repo + workflow scopes) — retained for history;
the modern flow creates the Release by hand from the gated artifacts.
