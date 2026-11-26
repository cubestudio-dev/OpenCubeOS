# WP-severe-fix-and-closure — ② the update full chain (B-update-chain)

Spec: "从 L1 update 程序 → 网络下载 OTA → A/B 槽位识别 → 刷写，全链路排查
（拉取失败/解包错误/槽位死锁），提供完整成功串口日志".

## The chain under test (all on the serial transcript)

```
build/abdisk.img  (GRUB self-booting A/B disk, tools/make_ab_disk.sh)
  p1 64 MiB  FAT32  boot/flags   - /boot/grub/grub.cfg + next_B/ok_B/bootfail_B
  p2 128 MiB FAT32  slot A      - /boot/opencube.elf = the 3.0.0 tree kernel
  p3 128 MiB FAT32  slot B      - EMPTY before the update (the flash target)
  p4 128 MiB FAT32  data        - download cache
```

The L1 chain (QEMU BIOS boot, unix-socket serial, slirp user-net,
host-side HTTP fixture on 10.0.2.2:8008):

1. GRUB `BOOT_SLOT=A` -> the 3.0.0-WP-compat-and-security-hardening banner
2. `dhcp` -> `DHCP success: IP=10.0.2.15`
3. `config set update_url http://10.0.2.2:8008/update.json` -> `config: saved`
4. `update --status` -> `current boot: A` / `next boot: A` /
   `A/B disk: present`
5. `update` (the L1 intent wizard, every step driven over the serial line):
   - 1/5 `[OK] Network is up.`
   - 2/5 `[OK] A/B disk is present.`
   - 3/5 `Resolved 10.0.2.2:8008 -> 10.0.2.2 (literal)` (IP-literal fix below)
   - 4/5 `New version available.` -> `3.0.1-WP-severe-fix-test`
   - 5/5 (confirmed `y`):
     `update: downloading... (1387 KB)` ->
     `update: verifying SHA256... OK` ->
     `update: extracting to B partition... OK` ->
     `update: setting boot to B... OK` ->
     `update: reboot required` -> `[OK] Update installed.`
   - confirmed reboot (`y`) -> `[ocst] clean-reboot marker recorded`
6. the reboot boots slot B: `BOOT_SLOT=B` -> the **3.0.1** package kernel
   banner (`[oc] Open Cube OS 3.0.1-WP-severe-fix-test kmain entered`) ->
   `uname -r` -> `3.0.1-WP-severe-fix-test` ->
   `update --status` -> `current boot: B` / `next boot: B` (confirm_boot
   promoted B to the confirmed default)

Host-side byte proof (after poweroff):

- p2 `/boot/opencube.elf` sha256 == the 3.0.0 seed kernel (slot A untouched)
- p3 `/boot/opencube.elf` sha256 == the package member `kernel/opencube.elf`
  (the flash is byte-identical, not just "the banner says so")

Result: **10/10 PASS** — `tools/abchain_e2e.py`, transcript in
`b-update-chain-transcript.log` (raw + ANSI-stripped copy).

## The three chain breaks found and fixed

### 1. The wizard's DNS gate rejected an IP-literal update server (K-10 sibling)

The wizard's 3/5 step feeds the update_url's AUTHORITY component
(`10.0.2.2:8008`, port included) to the `dns` command, which fed it to the
recursive resolver: `DNS resolution failed (timeout)` 3 s later and the
whole wizard aborted at 3/5 — the network was fine, the name was never a
name.  Fix (`ocos/services/net/src/svc_net_core.c`, `net_cmd_dns`): an IP
literal resolves directly
(`Resolved 10.0.2.2:8008 -> 10.0.2.2 (literal)`); `net_connect` /
`ota_update_url_parse` have always accepted IP literals — the wizard's DNS
gate was the only step that insisted on a name.

### 2. The test manifest's field names drifted from the production format

The hand-rolled test manifest used `"sha256"` where the production
website manifest (and the kernel parser `ota_update_check_pkg`) uses
`"package_sha256"` — `have_package` stayed 0 and the 5/5 install aborted
with `update: manifest has no package_url`.  Fix: the battery regenerates
`build/update.json` from the package's real sha256/size with the PRODUCTION
field names (`package_url` / `package_sha256` / `package_size`) — this
battery now permanently catches that class of drift (a test fixture that
never matches production proves nothing).

### 3. `dhcp` violated RFC 2131 — a single DISCOVER, a ~500 ms window

`net_dhcp_discover` sent ONE DHCPDISCOVER and waited ~500 ms (500 ticks
@1024 Hz RTC); a healthy slirp LAN was reported as
`DHCP failed (timeout)` once in ~8 boots (live-caught during this
battery's run 2: the wizard's step 1/5 then claimed "Network is up" from
the late-arriving offer while the standalone `dhcp` command had just
failed).  RFC 2131 4.1 requires client retransmission and every mainstream
client retries.  Fix: DISCOVER and REQUEST (same xid, RFC 2131 4.4.1) each
retransmit up to 3 attempts x 800 ms wall-clock (`core_timer_now_ms` —
the window is now tick-source independent, identical on the RTC and the
PIT fallback), with `[dhcp] retransmitting DISCOVER/REQUEST...` trace lines.

## Test-driver honesty notes (live-caught by the byte-proof)

- The first driver served the PREVIOUS run's package: the module-level
  `PKG_BODY = open(PKG).read()` ran at import time, BEFORE
  `prepare_assets()` rebuilt the package — the guest downloaded and
  flashed the OLD package while every version-string assertion stayed
  green (the old package carried the same version string).  The host-side
  byte proof caught it; the fixture bodies now load after prepare.
- The first driver's "SHA256 verified" assertion grepped for a string the
  kernel never prints (`manifest SHA256 verified` is the `--local` path;
  the online path prints `update: verifying SHA256... OK`).

## Reproduce

```
python3 tools/abchain_e2e.py            # prepares assets + runs the chain
python3 tools/abchain_e2e.py --no-prepare   # reuse built assets
# transcript: build/abchain-transcript.log
```

Asset preparation embedded in the driver (idempotent):

```
make OC_RELEASE_VERSION=3.0.1-WP-severe-fix-test OC_KERNEL_BUILD=999 kernel
bash tools/make_update_pkg.sh WP-severe-fix build/opencube.elf build/update-pkg.tar.gz
make kernel                                     # restore the 3.0.0 tree kernel
bash tools/make_ab_disk.sh build/opencube.elf   # slot A seeded with 3.0.0
# + build/update.json regenerated with the production field names
```

Both kernels in the test carry the DHCP retransmission fix — the fix is
in the shared `svc_net_core.c`, not the version string.
