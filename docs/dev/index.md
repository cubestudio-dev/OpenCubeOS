<!-- SPDX-License-Identifier: Apache-2.0 -->
# Developer handbook

From a fresh machine to merged PRs - the 3.0.0 tree (the ocos/
service kernel, 284 ring-3 programs, the 8 merged mega-apps).

| Page | What it covers |
|------|----------------|
| [environment](environment.md) | requirements, layout, QEMU recipes |
| [build](build.md) | targets, the compile cache, determinism, cross builds |
| [qemu-testing](qemu-testing.md) | interactive + automated runs, device matrix |
| [debugging](debugging.md) | boot-log method, panic path, gdb, host tests |
| [host-tests](host-tests.md) | the `make test` harnesses |
| [e2e-suites](e2e-suites.md) | the deep gates: regression 31/31, SSH 5/5, HTTPS 8/8, mega-app smoke |
| [pr-workflow](pr-workflow.md) | the PR bar, in order |
| [portability](portability.md) | the stricter-toolchain classes + flag contract |
| [coding-style](coding-style.md) | C, assembly, naming, limits discipline |
| [ocire](ocire.md) | the independent recovery kernel (second L0) |

## Where things are

```
ocos/kernel/       L0 core (boot, sched, mm, usermode)
ocos/services/     one folder per service facet (block, net, ...)
ocos/drivers/      one folder per hardware class
ocos/interface/    the ring-3 headers (l1_lib, l1svc_ops, i18n,
                   wizard_lib, fs_lib, editor_core)
ocos/l1apps/       one command = one app = one folder (284 programs)
ocire/             the independent recovery kernel
tools/             build / test / docs generators
docs/              this documentation (en + zh mirror)
```

The app-side guide (how programs are written, the L1SVC surface,
the wizard library) has its own index:
[../l1/index.md](../l1/index.md).
