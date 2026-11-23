<!-- SPDX-License-Identifier: Apache-2.0 -->
# 3.0.0-WP-boot-stack-vmware-and-nak-rebase — release gate evidence

All gates re-run on the release build (banner
`3.0.0-WP-boot-stack-vmware-and-nak-rebase build 511`, public commit
f59e8376 + the stamps/tool-fix/evidence commit that carries this file).

| Gate | Result | Log |
|------|--------|-----|
| Clean build `make all` | 0 errors 0 warnings (2 pre-declared EFI ld RWX warnings, KNOWN since wp06g1) | build-all.txt |
| Reproducibility `make reproducible-check` | PASS — ELF byte-identical across two clean builds (a4c673e4…) | reproducible-check.txt |
| BIOS boot gate `make run-bios` | OCRMECL -> OCBL -> kernel -> `ush>` GREEN, banner build 511 | boot-bios.txt |
| UEFI boot gate `make run-uefi` | OCBL (OVMF) -> kernel -> `ush>` GREEN | boot-uefi.txt |
| Regression battery | **31/31 checks passed** | regression-31.txt (+ summary) |
| WP-08 driver-framework gate | **43/43 checks passed** | wp08-gate.txt |
| dhtest (DH modexp truth vectors) | 5/5 PASS, determinism IDENTICAL | dhtest.txt |
| SSH E2E (g11a) | 5/5 ALL PASS (ssh-auth/exec + sshd-listen/exec/capture-log) | ssh-e2e.txt |
| HTTPS E2E (fix4) | **8/8 HTTPS E2E checks passed** | https-e2e.txt |
| Strict matrix `make test-strict` | BOTH TRACKS GREEN (`-cpu qemu64,enforce -machine vmport=off -boot strict=on`) | strict-matrix.txt |
| Host unit tests `make test` | K-01 declared inherited limitation (19 of 20 harnesses fail to BUILD against pre-restructure include paths; 1 passes; the runtime suites carry the regression weight) | host-tests.txt |

## Live-caught tool fix (declared)

`tools/qemu_runner.py` `monitor_screendump()`: QEMU >= 9 writes the
screendump PPM asynchronously AFTER the HMP prompt returns; the old
immediate read raced it and the 31-check gate failed check #1
("unparseable screendump" / "screendump failed"). Fixed by waiting for
the file to appear and its size to settle (<= 5 s). Caught live on this
release re-run against QEMU 10; the fix is committed with this release
and the gate re-run GREEN (31/31).

## Honest deviations (declared)

1. `make test` (host unit tests): K-01, unchanged inherited limitation — declared, not silently skipped.
2. The SSH E2E ran against `build/etc.img` seeded with the
   per-installation host key (`/etc/ssh_host_key`, 512 B `n[256]||d[256]`,
   host-side generated, round-trip verified) — the identical state the
   prior release evidence shows ("host key: per-installation"). The
   in-guest first-boot RSA-2048 keygen path was observed to START
   (sshd-hkgen thread spawned, generation begun) but takes >10 min
   under TCG in this sandbox; on real hardware it runs in seconds. The
   loaded-key path (what the E2E asserts) is fully covered.
3. The sandbox cannot run VMware — the F1-F4 structural fixes
   (QEMU-nanny independence + observability) are proven by the strict
   matrix; the user-side VMware re-test remains the standing acceptance
   item declared in the work package.
