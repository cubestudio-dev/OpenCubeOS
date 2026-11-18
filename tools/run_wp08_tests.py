#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""WP-08-driver-framework acceptance gate (QEMU, live system).

Boots the built ISO and exercises the driver-framework surface:

  T01 metadata registration    drv list shows both paths, counts add up
  T02 deterministic order      drv order: deps first, cycle reported
  T03 device tree              dev tree / dev list / dev info shapes
  T04 d-state declarations     drv info shows D0/D1/D3 for vtest-bus
  T05 hotplug chain            boot inserts + reload remove/insert flow,
                               vtest-mon delivery count follows
  T06 failure isolation        vtest-fail probe error visible, system
                               stays alive, retry bumps the count
  T07 umbrella == twin         every subcommand byte-identical to its
                               standalone twin (drv X vs drv_X)
  T08 error paths              unknown sub / missing arg / unknown node
  T09 zero-global-edit         drv list shows the late-added vtest-late
                               (registered by file addition only)
  T10 init-order determinism   three fresh boots, identical order lines

Usage: python3 tools/run_wp08_tests.py [iso]
Exit code: number of failed tests."""
import os
import re
import sys
import pexpect

OC_TOOLS = os.environ.get("OC_TOOLS", os.path.expanduser("~/opt/extract"))
ISO = sys.argv[1] if len(sys.argv) > 1 else "build/opencube.iso"
QEMU = f"{OC_TOOLS}/usr/bin/qemu-system-x86_64"
ENV = dict(os.environ,
           LD_LIBRARY_PATH=f"{OC_TOOLS}/usr/lib/x86_64-linux-gnu:"
                           f"{OC_TOOLS}/lib/x86_64-linux-gnu")

PROMPT = r"ush> "


def boot(timeout=120):
    c = pexpect.spawn(QEMU, ["-m", "512", "-cdrom", ISO, "-nographic",
                             "-no-reboot",
                             "-L", f"{OC_TOOLS}/usr/share/qemu",
                             "-L", f"{OC_TOOLS}/usr/share/seabios"],
                      env=ENV, timeout=timeout, encoding="utf-8",
                      codec_errors="replace")
    c.expect(PROMPT, timeout=timeout)
    return c


def run(c, line, timeout=60):
    """Send one command; return (echo_stripped_output, ok)."""
    c.sendline(line)
    idx = c.expect([PROMPT, pexpect.TIMEOUT, pexpect.EOF], timeout=timeout)
    out = c.before or ""
    # strip the leading echo of the command itself
    out = out.split("\r\n", 1)[1] if "\r\n" in out else out
    return out, idx == 0


results = []


def check(name, ok, detail=""):
    results.append((name, ok, detail))
    print(f"[{'PASS' if ok else 'FAIL'}] {name}" + (f"  {detail}" if detail and not ok else ""))


def main():
    fails = 0

    # ---------------- session 1: the command matrix -----------------
    c = boot()
    try:
        # T01 metadata + legacy registration
        out, ok = run(c, "drv list")
        m = re.search(r"driver registry: (\d+) drivers \((\d+) metadata \+ (\d+) legacy\)", out)
        check("T01.1 drv list header counts",
              ok and m and int(m.group(2)) + int(m.group(3)) == int(m.group(1)) and int(m.group(2)) >= 7,
              out[:200])
        rows = dict(re.findall(r"^(\S+)\s+\S+\s+(\S+)\s+(\d+)\s+meta", out, re.M) and
                    [(r[0], r[1]) for r in re.findall(r"^(\S+)\s+\S+\s+(\S+)\s+\d+\s+meta", out, re.M)] or [])
        check("T01.2 metadata states",
              "vtest-bus" in out and "vtest-ctl" in out and "legacy" in out and "meta" in out,
              out[:200])
        check("T01.3 vtest-ctl bound to 2 devices",
              re.search(r"vtest-ctl\s+1\.0\s+ok\s+2\s+meta", out) is not None,
              out[:200])

        # T02 deterministic order + cycle detection
        out, ok = run(c, "drv order")
        check("T02.1 order lists vtest-bus first",
              ok and re.search(r"1\. vtest-bus", out) is not None, out[:300])
        check("T02.2 vtest-ctl ordered after its dependency",
              re.search(r"2\. vtest-ctl\s+<-\s*vtest-bus", out) is not None, out[:300])
        check("T02.3 both cycle canaries isolated",
              out.count("dependency cycle (reported, never looped)") == 2, out[:300])

        # T03 device tree surfaces
        out, ok = run(c, "dev tree")
        check("T03.1 dev tree hierarchy",
              ok and "[1] root" in out and "vtest-bus0" in out
              and "vtest-ctl0" in out and "mmio 0xFEE00000+0x1000" in out
              and "unbound (last probe error -55)" in out, out[:300])
        out, ok = run(c, "dev list vtest-ctl")
        check("T03.2 dev list type filter",
              ok and "vtest-ctl0" in out and "vtest-ctl1" in out
              and "vtest-bus0" not in out, out[:300])
        out, ok = run(c, "dev info vtest-ctl0")
        check("T03.3 dev info by name",
              ok and "node: vtest-ctl0" in out and "parent    vtest-bus0 (id 2)" in out
              and "irq   11" in out, out[:300])

        # T04 d-states
        out, ok = run(c, "drv info vtest-bus")
        check("T04.1 d-state declaration query",
              ok and "d-states  D0 D1 D3" in out, out[:300])
        out, ok = run(c, "drv info vtest-ctl")
        check("T04.2 d-state declaration (dep driver)",
              ok and "d-states  D0 D3" in out, out[:300])

        # T05 hotplug chain
        out, ok = run(c, "drv info vtest-mon")
        m = re.search(r"hotplug\s+(\d+) events received", out)
        check("T05.1 boot inserts delivered",
              ok and m and int(m.group(1)) >= 2, out[:300])
        out, ok = run(c, "drv reload vtest-bus")
        check("T05.2 reload cascades remove+insert",
              ok and "event remove vtest-ctl0" in out and "event insert vtest-ctl0" in out,
              out[:400])
        out, ok = run(c, "drv info vtest-mon")
        m = re.search(r"hotplug\s+(\d+) events received", out)
        check("T05.3 delivery count grew by 4",
              ok and m and int(m.group(1)) >= 6, out[:300])

        # T06 failure isolation (probe failure recorded, system alive)
        out, ok = run(c, "drv failed")
        check("T06.1 failed ledger shows the injected failure",
              ok and re.search(r"vtest-fail\s+-55", out) is not None, out[:300])
        out, ok = run(c, "drv reload vtest-fail")
        check("T06.2 retry path re-runs, still isolated",
              ok and "reloaded vtest-fail: state failed, 0 node(s) bound" in out, out[:300])
        out, ok = run(c, "drv failed")
        m = re.search(r"vtest-fail\s+-55\s+(\d+)", out)
        check("T06.3 failure count bumped by the retry",
              ok and m and int(m.group(1)) >= 2, out[:300])
        out, ok = run(c, "uname -a")     # system still fully alive
        check("T06.4 system alive after the failures",
              ok and "Open Cube OS" in out, out[:200])

        # T07 umbrella == standalone twins (byte-identical)
        pairs = [
            ("drv list", "drv_list"),
            ("drv order", "drv_order"),
            ("drv failed", "drv_failed"),
            ("drv info vtest-bus", "drv_info vtest-bus"),
            ("drv info vtest-ctl", "drv_info vtest-ctl"),
            ("drv info nosuch", "drv_info nosuch"),
            ("dev tree", "dev_tree"),
            ("dev list", "dev_list"),
            ("dev list vtest-ctl", "dev_list vtest-ctl"),
            ("dev info vtest-ctl0", "dev_info vtest-ctl0"),
            ("dev info 999", "dev_info 999"),
            ("drv help", "drv_help"),
            ("dev help", "dev_help"),
        ]
        for umbrella, twin in pairs:
            o1, ok1 = run(c, umbrella)
            o2, ok2 = run(c, twin)
            check(f"T07 byte-equal: `{umbrella}` == `{twin}`",
                  ok1 and ok2 and o1 == o2,
                  f"len {len(o1)} vs {len(o2)}")

        # T08 error paths
        out, ok = run(c, "drv zzz")
        check("T08.1 unknown subcommand lists the options",
              ok and "unknown subcommand: zzz" in out
              and "available subcommands:" in out and "drv help" in out, out[:300])
        out, ok = run(c, "drv info")
        check("T08.2 missing argument",
              ok and "missing argument for 'info'" in out and "usage: drv info <name>" in out,
              out[:300])
        out, ok = run(c, "drv bind")
        check("T08.3 bind without args",
              ok and ("missing argument for 'bind'" in out or "need <driver> and <device>" in out)
              and "usage:" in out, out[:300])
        out, ok = run(c, "drv bind nosuch vtest-bus0")
        check("T08.4 bind with unknown driver",
              ok and "unknown driver: nosuch" in out, out[:300])
        out, ok = run(c, "dev info 999")
        check("T08.5 unknown node",
              ok and "unknown device node: 999" in out, out[:300])
        out, ok = run(c, "drv debug maybe")
        check("T08.6 debug with bad argument",
              ok and "unknown argument: maybe" in out and "expected on or off" in out,
              out[:300])
        # manual bind onto a free node of the WRONG type: the framework
        # allows the attempt (debug freedom), the driver's own probe
        # rejects it - recorded, system unaffected.  vtest-ctl nodes
        # SURVIVE their driver's unbind (the owning bus keeps them).
        out, ok = run(c, "drv unbind vtest-ctl vtest-ctl0")
        check("T08.7a unbind leaves the bus-owned node in place",
              ok and "unbound vtest-ctl from vtest-ctl0" in out, out[:300])
        out, ok = run(c, "drv bind vtest-bus vtest-ctl0")
        check("T08.7b type-mismatch manual bind: driver refuses, recorded",
              ok and "probe failed with error -22" in out, out[:300])
        # restore the regular binding
        out, ok = run(c, "drv bind vtest-ctl vtest-ctl0")
        check("T08.8 restore: vtest-ctl rebinds its node",
              ok and "bound vtest-ctl to vtest-ctl0" in out, out[:300])

        # T09 zero-global-edit: the late-added driver registers
        out, ok = run(c, "drv list")
        check("T09 late-added vtest-late registered by file addition",
              ok and re.search(r"vtest-late\s+1\.0\s+ok\s+1\s+meta", out) is not None,
              out[:300])
        out, ok = run(c, "drv order")
        check("T09.2 vtest-late ordered after vtest-ctl",
              ok and re.search(r"vtest-late\s+<-\s*vtest-ctl", out) is not None, out[:400])
    finally:
        c.close(force=True)

    # ---------------- session 2..4: init-order determinism ----------
    order_lines = []
    for i in range(3):
        c = boot()
        try:
            out, ok = run(c, "drv order")
            m = re.search(r"((?:  \d+\. \S+.*\n)+)", out)
            order_lines.append(m.group(1) if m else f"<no order in boot {i}>")
        finally:
            c.close(force=True)
    check("T10 init order identical across three fresh boots",
          order_lines[0] == order_lines[1] == order_lines[2],
          "\n".join(order_lines))

    fails = sum(1 for _, ok, _ in results if not ok)
    print(f"\nWP-08 gate: {len(results) - fails}/{len(results)} checks passed")
    return fails


if __name__ == "__main__":
    sys.exit(main())
