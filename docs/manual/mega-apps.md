<!-- SPDX-License-Identifier: Apache-2.0 -->
# The 8 merged apps: wizard, direct, classic

Since 3.0.0 the user surface is organized around eight merged apps.
Each of them opens the same three doors; which one you use depends on
whether you are exploring (wizard), scripting (classic), or want to be
explicit about bypassing the menus (direct).

```
ush> disk-tool                wizard: menus, checks, confirmations
ush> disk-tool direct lsblk   explicit verbatim passthrough
ush> disk-tool lsblk          classic dispatch = the historical command
```

All three produce the **same service output** - the wizard only adds
detection, safety confirmations and grouping around it.

## The wizard (no arguments)

Running the app with no arguments opens an intent wizard:

- Numbered menus: press a single digit key (`1`-`9`) to pick, `q` or
  ESC to quit/back. No Enter needed.
- Destructive paths (mkfs, abdisk, install, rm, power actions) ask
  for the target name, make you **retype it**, then a bare `y` -
  anything else aborts.
- Wizards read the same English service output lines a script would
  and can **cross domains**: the `update` wizard fixes a missing IP
  through DHCP and a missing A/B disk through `lsblk` + `abdisk`
  before it ever checks for updates.
- Everything renders through the active language: `lang zh` switches
  the whole wizard, menus included.

## direct (explicit passthrough)

`<app> direct <op> [args...]` runs one op exactly as the historical
command did, with the word `direct` stripped from the argument line.
Use it in scripts when you want the intent to be self-documenting:

```
ush> update direct           the historical bare `update` (online flow)
ush> sound-tool direct volume 50
ush> sysdiag direct health watch
```

## classic (verbatim dispatch)

`<app> <op> [args...]` looks the op up in the app's table and runs it
with the historical spelling - output is byte-identical to running
the single-command app of the same name. The single-command apps
remain registered (the interface-freeze rule: commands are added,
never removed), so `lsblk` and `disk-tool lsblk` are two doors into
the same service call.

An unknown op name prints the app's usage with its full op table:

```
ush> disk-tool nope
disk-tool: unknown op: nope
usage: disk-tool            (interactive menu)
       disk-tool direct <op> [args]   (explicit passthrough)
       disk-tool <op> [args]          (classic op dispatch)
ops:
  ahci
  nvme
  ...
```

## App-by-app

### update - the OTA wizard

`update` (no args) walks the whole update intent in five steps:

1. **Network** - reads the IP status; a 0.0.0.0 address offers a DHCP
   fix, then rechecks.
2. **A/B disk** - reads `update status`; when the A/B disk is absent
   it lists `lsblk`, asks for the device name (retyped + confirmed)
   and runs `abdisk`, then rechecks until present.
3. **DNS** - parses the live `config get update_url` value and
   resolves the update host.
4. **Check** - `checkupdate`: up-to-date ends the wizard with OK.
5. **Install** - confirmed online update, then a confirmed reboot
   into the new slot.

Classic surface: `update status`, `update --local <path>`,
`update <anything>` = the verbatim OTA op.

### disk-tool - disks and filesystems

Menu: lsblk / parted / mkfs submenu (FAT32, exFAT, ext4 - double
confirmation) / mount manager (list, mount, umount, fatmount,
fatstat) / fsck / dskstat / A/B manager (status, create, abcfg).

Classic table: all 23 block ops (`ahci` .. `abcfg`) plus the 6 fs ops
(`mount`, `mounts`, `umount`, `fatmount`, `fatstat`, `tree`).

### net-tool - connectivity

Menu: show IP / DHCP / static IP (address, mask, gateway collected
one field at a time) / DNS / WiFi manager (status, scan, test, sdio)
/ Bluetooth (status, usb, uart, test) / firewall / route + arp /
connectivity tests (ping, wget, ssh, dnstest - ping and wget go
through the same NETCMD bridge the historical apps used).

Classic table: the 14 net ops (`arp` .. `sshd`) plus `wifi` and `bt`.

### sound-tool - audio

Menu: device overview / volume (digits-only validation) / the 440 Hz
test tone / per-driver status (HDA, AC'97, SB16, ES1370, virtio-snd,
USB audio) / selftests (the RW loopback, sample-rate accuracy, or
every driver's own selftest in sequence).

Classic table: all 17 snd ops (`sound` .. `sample_rate_test`).

### files - the file manager

Menu: ls / tree / disk usage (df + du) / copy / move / delete (with
confirmation) / edit (the full-screen editor) / info + permissions
(stat, then optional chmod with octal validation).

Dispatch here is fork + execve of the historical programs (the `run`
mechanism), because the file tools are real programs, not service
calls - the editor is interactive. Classic table: `ls`, `tree`, `df`,
`du`, `cp`, `mv`, `rm`, `edit`, `stat`, `chmod`.

### sysdiag - system diagnostics

Menu: health snapshot / kernel log / memory picture (mem + frag +
heap) / scheduler + sync stats / crash records / log tools (logcat,
logctl, health export, health watch) / selftest lab (memtest,
heaptest, fstest, vmtest - individually or all four in sequence).

Classic table: all 39 diag ops plus health, dmesg/logcat/logctl and
watchdog - 46 entries. `crashtest` (deliberate panic injection) is
reachable through classic/direct only, on purpose.

### hwinfo - hardware inventory

Menu: system summary (uname -a + memory) / clock sources (RTC, HPET,
PM timer, TSC) / PCI (lspci, pcidump, pcibars, pcie) / ACPI /
SMBus-I2C / USB / camera & legacy input / display + font + encoding.

Classic table: 26 read-only ops across the display, time, net, diag,
usb, camera and input surfaces.

### power - power management

Menu: shutdown / reboot / halt (each behind a bare-y confirmation) /
sleep N seconds (digits validated) / power-path selftests (each with
its own warning + confirmation - the probes exercise the real power
paths).

Classic table: `shutdown`, `poweroff`, `reboot`, `halt`, `sleep`,
`poweroff_test`, `suspend_test`, `halt_test`, `reboot_test`.

## i18n

Every wizard line renders through the active locale. Switch with:

```
ush> lang zh        (menus, prompts and verdicts become Chinese)
ush> lang en        (back to English)
```

A missing translation falls back to English, then to the key itself -
an untranslated app can never crash or print garbage.
