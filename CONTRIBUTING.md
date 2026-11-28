<!-- SPDX-License-Identifier: Apache-2.0 -->
# Contributing to Open Cube OS

Thank you for your interest in contributing! Open Cube OS is a
from-scratch x86_64 kernel, and every contribution - code, documentation,
driver bring-up, bug reports - is welcome.

## Ground rules

1. **Kernel strings are ASCII English.** Everything the system itself
   prints (command names, prompts, boot messages, help text) must stay
   plain ASCII. Non-ASCII text may appear in user data and in
   documentation, never in kernel-visible strings.
2. **The command surface only grows.** A shell command that has shipped
   may be reworked internally, but its name and overall behaviour must
   stay; removals break the interface contract. Register new commands
   with `shell_register_command_ex()` and a work-package tag.
3. **Zero warnings.** The build compiles with `-Wall -Wextra -Werror`.
   A change that introduces a warning is a broken change, on your
   machine and on CI.
4. **No stubs, no fake data.** If a feature is partial, say so in the
   code and the docs - never paper over it. A driver that only probes
   and reports "not supported on this machine" is fine; a driver that
   pretends to work is not.
5. **Apache 2.0 everywhere.** Every new source file starts with the
   SPDX header:

   ```
   /* SPDX-License-Identifier: Apache-2.0 */
   /* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
   ```

## Development flow

### 1. Get a build going

Follow [BUILDING.md](BUILDING.md). The short version on Linux:

```
./build.sh iso          # kernel + bootable ISO
make test               # host-side unit tests
```

You need `gcc, nasm, grub-mkimage, xorriso, mtools, python3, zip`
(`tools/build.sh` checks and tells you the exact missing ones, or
installs them with `--install`).

### 2. Make your change

- Keep the change focused: one driver, one fix, one documentation
  chapter per pull request.
- Every kernel source file carries the SPDX header - keep it.
- New commands: register in the matching `shell/shell_cmds_*.c`,
  add the help line, and extend the regression expectations
  (see `tools/run_fix4_regression.py`, the command-count baseline).

### 3. Test before you push

The minimum bar, all of it:

```
make kernel             # 0 errors, 0 warnings (-Werror enforced)
make test               # host unit suite (tools/run_host_tests.py)
make iso                # hybrid BIOS+UEFI ISO builds
make reproducible-check # two clean builds -> byte-identical ELF
```

Then boot it:

```
make run-bios   # or: qemu-system-x86_64 -cdrom build/opencube.iso
make run-uefi   # OVMF path
```

If your change touches anything listed below, the matching deep test
is also required:

| You touched...                  | Also run                                              |
|---------------------------------|-------------------------------------------------------|
| storage / filesystems           | `fstest`, `dhtest` in the guest                       |
| crypto / TLS / SSH              | `cryptotest` in the guest, HTTPS E2E                  |
| watchdog / klog / health        | `tools/wp12_watchdog_logging_e2e.py build/opencube.iso` |
| the full boot path              | the regression suite (`tools/run_fix4_regression.py`) |
| A/B slots or OTA                | `ab_partition_test`, OTA install test in the guest    |

CI (`.github/workflows/build.yml`) runs the build, packaging, host tests
and reproducibility gate on every PR - it must be green before merge.

### 4. Commit style

- One commit per logical step; each commit message answers *what* and
  *why*, not just *what*.
- Prefix with the area: `usb: ...`, `fs: fat32: ...`, `docs: ...`,
  `build: ...`.
- If you fixed a tracked audit bug, reference its number
  (`BUG-0136`) in the message.

### 5. Pull requests

1. Fork or branch from `main`.
2. Push your branch, open the PR against `cubestudio-dev/OpenCubeOS`.
3. Fill in the checklist from the PR template:
   - [ ] builds with 0 warnings (`-Wall -Wextra -Werror`)
   - [ ] host tests pass (`make test`)
   - [ ] booted in QEMU (BIOS and UEFI)
   - [ ] relevant deep tests pass (table above)
   - [ ] docs updated (manual pages under `docs/`, interface lists if
         the L1 surface changed)
4. Expect review comments about edge cases, DMA bounds and error
   paths - that is normal for kernel code.

## Version discipline (3.0.0 phase)

**One phase, one version stem.** Until the 74 work packages that make up the
3.0.0 phase are complete, the release version is fixed to
`3.0.0-WP-<work-package-name>`. The minor and patch components do **not**
move on their own: no contributor, and no work package acting alone, may
raise the patch number, the minor number, or the major number during this
phase - the stem stays `3.0.0`. A work package names itself in the suffix;
it never raises the stem.

**The Makefile literal is the only source of truth.** `OC_RELEASE_VERSION`
in the `Makefile` (`Makefile:102`) is the single authority for the version
string - the kernel banner, `uname`, the OTA manifest and the ISO all take
it from there. Editing that literal is a *release action*, not a code
change: it requires work-package authorization and it drags the whole tree
with it.

**The version lives in many places; a change must reach all of them.** The
same string appears in the `Makefile`, `README.md`, `docs/release/`, the
`website/` static pages and `website/assets/site-data.js`, the
`update.json` / `update-v2.json` manifests, the git tag name, and the asset
filenames (`.iso`, `-src.zip`, `-update.tar.gz`). Editing one surface is
never a version change - a version change is a repo-wide reconcile.
Acceptance is mechanical: `git grep -n '<old-version>'` must return nothing
except documented non-version matches (upstream npm package versions,
documentation IP literals, archived evidence filenames).

**A violation is a release incident.** An unauthorized version jump is
treated as a publishing incident, not a typo. The rollback procedure is:
replace the string repo-wide, re-tag, re-upload the assets, and redeploy
gh-pages - in that order, with the `git grep` check green between steps.

Reference: `docs/release/versioning.md` (versioning rules, build
numbers and the compatibility promises).

### Commit and push discipline

This applies to **every** task, not only release tasks.

**One verified batch, one push.** Commit and push only after *all* tasks in
the batch are complete and verified. Do not push while working, and never
push a half-finished state: a push is a claim that the tree builds, boots
and passes its gates.

**Before the commit, all of this must be true:** `make all` finishes with
0 errors and 0 warnings, the full regression battery passes, `git status`
shows no missed generated artifacts (version stamps, `docs/data/*.json`,
generated mirrors), and no background build or test process is still
running.

**Two repositories, one push.** The public repository
`cubestudio-dev/OpenCubeOS` and the private mirror repository are pushed
**together** - a single push covers both. A commit that reaches only one of
them is an incomplete delivery.

**The author identity is a machine gate.** The git author must be
`cubestudio-dev <cubestudio@qq.com>`; `tools/build_release.sh` (`:44-45`)
enforces the identity mechanically and fails the build otherwise.

**gh-pages follows the push.** Deploy gh-pages only after the push lands,
then verify the live site - the deployed pages are part of the delivery,
not a follow-up.

Related: `docs/dev/pr-workflow.md` (the pull-request bar this batch
discipline rides on).

## Reporting bugs

Open an issue with:

- the build identifier from `uname -a` inside the guest,
- the exact command sequence,
- the serial log (`-serial file:log.txt` is your friend),
- whether it reproduces on BIOS, UEFI, or both.

Security-sensitive reports: see `SECURITY` notes in the README - do not
open a public issue; email the maintainers instead.

## Documentation contributions

The documentation tree under `docs/` is the product, not an afterthought.
If you add a command or an interface, the matching manual/reference page
must land in the same PR.  Reference pages are generated from the real
tree (`make api-docs`) - do not hand-edit generated tables.

## Licensing

By submitting a contribution you agree it is licensed under
Apache 2.0 (the `LICENSE` file at the repository root), and that the
copyright line may carry the project's holder.

Thank you for building Open Cube OS with us.
