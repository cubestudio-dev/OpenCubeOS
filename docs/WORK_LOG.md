<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 cubestudio-dev <cubestudio@qq.com> -->

# Open Cube OS — Public Work Log (WP-09 closeout)

The full engineering log (WP-01 → WP-09, every batch, every bug, every
decision) lives in the private archive repository (`worklog.md`,
cubestudio-dev/tdghuczhdmkwicsxzjduaychuyzshjufajckafk). This file is the
public summary of the WP-09 closeout batches.

## Batch 14 (2026-09-30) — SSH root-cause fixes + first full WP-09 regression

- SSH client K mismatch root causes fixed (DH group14 prime, sequence
  advance, CBC rolling IV, CHANNEL_DATA parsing).
- sshd hardened: `is_exec`/`password`/`session` NUL-dependence replaced with
  `oc_memcmp`; exit-status reply buffer overflow fixed (16→32 B);
  `shell_execute_captured()` added to shell.c (serial hook chain-restore);
  capture-hierarchy destroy bug fixed.
- Full clean rebuild: 0 errors 0 warnings (-Wall -Wextra -Werror).
- Regression: 18/18 PASS, dhtest 5/5, SSH client E2E + sshd E2E vs paramiko.
- Release: tag `WP-09-batch-14` (ISO + SRC zip). Commit author corrected to
  cubestudio-dev (see Batch B below).

## Batch A (2026-09-30) — SSH closeout assessment

- Both SSH directions re-verified with byte-level K agreement.
- Conclusion: no remaining SSH development items; optional quality items
  (host-key persistence, publickey auth, keepalive, algorithm whitelist)
  deferred to WP-10+ — see docs/KNOWN_ISSUES.md §2A.4.
- No code changes → no Release (no empty releases policy).

## Batch B (2026-09-30) — Regression evidence archived (this repo)

- 18/18 regression re-run in one QEMU session — real outputs archived.
- dhtest 5/5 re-run — real outputs archived.
- HTTPS E2E (first run with full dual-side evidence): kernel TLS 1.2 client
  vs tools/https_test_server.py — handshake, encrypted GET, MAC-verified
  response, `Saved 24 bytes`, body verified.
- SSH both directions re-run: 4/4 + byte-level K match
  (2f4130816e935c6a, len=256).
- Evidence: docs/VERIFICATION_BATCH_B.md + docs/verification/*.log (7 files).
- New tool: tools/https_test_server.py.
- Commit: 90903b6 (author cubestudio-dev). No Release (kernel artifacts
  byte-identical to batch-14; docs+tools only).

## Batch C (2026-09-30) — Documentation set updated to WP-09

- README.md → WP-09 (stats, WP-09 section, tests, downloads).
- MANIFEST.md → WP-09 (structure, how-to-test, line counts).
- VERIFICATION_REPORT.md → WP-09 (§10 added, real evidence).
- docs/KNOWN_ISSUES.md → WP-09 (§2A: 7 WP-09 known items/accepted behaviors).
- docs/STATUS.md → WP-09 status + acceptance table.
- docs/EXTENSIONS_WP09.md created (WP-09 interfaces + evidence).
- docs/INTERFACES.md created (full interface index, real signatures).
- docs/WORK_LOG.md created (this file).

## Batch D (2026-09-30) — Official WP-09 release

- Tag `WP-09` + GitHub Release with ISO + SRC zip assets.
- Pre-release batch tags kept as-is (history preserved).

## Batch E (2026-09-30) — Website + GitHub Pages

- website/ subtree (Next.js static export) + .github/workflows/deploy.yml.
- Verified live at https://cubestudio-dev.github.io/OpenCubeOS/.

## Batch F (2026-09-30) — Privacy / license / SHA256 cross-check

- Privacy scan, Apache-2.0 license header check, 3-way SHA256 comparison
  (local build / GitHub Release assets / website download links).
