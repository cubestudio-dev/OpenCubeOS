<!-- SPDX-License-Identifier: Apache-2.0 -->
# The pull-request bar

What a PR must clear, in order:

1. **Builds clean**: `make kernel` with `-Wall -Wextra -Werror` - on
   your machine *and* on CI (CI's toolchain is often stricter; the
   multiboot2 packed-cast and rdrand setc fixes exist precisely
   because a stricter GCC/GAS on the runner caught what local builds
   accepted).
2. **Host tests**: `make test` - zero failures.
3. **Boots**: `make run-bios` and `make run-uefi` - both reach the
   `oc>` prompt with `boot complete ... OK`.
4. **The deep test for whatever you touched** - the matrix in
   [CONTRIBUTING.md](../root/CONTRIBUTING.md) (storage -> fstest;
   crypto -> cryptotest + HTTPS E2E; watchdog/klog/health -> the
   WP-12 E2E; boot path -> the 28-check regression).
5. **Reproducible**: `make reproducible-check` prints PASS.
6. **Docs in the same PR**: new command -> its reference page is
   generated (`make api-docs` refreshes `docs/manual/commands/` and
   `INTERFACES.md`); new interface -> the surface tables; behaviour
   change -> the matching manual page.
7. **CI green**: the workflow runs build, ISO, packages, host tests
   and the reproducibility gate on every PR - merge only on green.

## Writing the PR description

- What changed and *why* - the commit messages carry the detail.
- Which tests you ran (commands + result lines, pasted).
- Anything you deliberately did NOT do (scope edges).

## Review expectations

Expect questions about error paths, DMA bounds, and what happens when
the hardware lies (a truncated table, a stuck NAK, a card that
answers garbage). The tree's own history - BUG-0136..0238 - is a
catalogue of exactly these; reading a few of those fix commits is
the best preparation.
