<!-- SPDX-License-Identifier: Apache-2.0 -->
# Application dictionary - The 8 merged apps (8)

### disk-tool

| Field | Value |
|---|---|
| Name | disk-tool |
| Category | The 8 merged apps |
| Function | disk / filesystem wizard (no args) or verbatim block+fs ops |
| Usage | `disk-tool            (interactive menu` |
| Hidden parameters | hidden parameter, not a public interface; compatibility is not guaranteed: `direct` |
| Op table | ahci, nvme, ata, dskstat, lsblk, parted, mkfs.fat32, mkfs.exfat, mkfs.ext4, fsck, sync, ahci_test, nvme_test, ata_dma_test, virtio_blk_test, disk_rw_test, partition_test, fs_mount_test, real_hw_test, abdisk, install, grub-install, abcfg, mount, mounts, umount, fatmount, fatstat, tree |
| Return codes | 0, 1 |

**Executed example**:

```
disk-tool lsblk
Block devices:
Disk cache: 0/64 slots  hits=0 misses=0
ush> 
```

(Note: classic dispatch (verbatim op passthrough))

### files

| Field | Value |
|---|---|
| Name | files |
| Category | The 8 merged apps |
| Function | file manager wizard (no args) or direct program dispatch (ls/cp/mv/rm/edit/...) |
| Usage | `files                (interactive menu` |
| Hidden parameters | hidden parameter, not a public interface; compatibility is not guaranteed: `direct` |
| Op table | ls, tree, df, du, cp, mv, rm, edit, stat, chmod |
| Return codes | 0, 1 |

**Executed example**:

```
fatmount
fat32: invalid device string
vfs_mount: fs mount failed
fatmount: ata0 -> /mnt FAILED - no FAT32 partition on device or drive not present
(the mount point was NOT created - `ls /mnt` will report 'no such path' until a real mount succeeds)
ush> 
```

(Note: classic dispatch (verbatim op passthrough))

### hwinfo

| Field | Value |
|---|---|
| Name | hwinfo |
| Category | The 8 merged apps |
| Function | hardware inventory wizard (no args) or verbatim info ops |
| Usage | `hwinfo            (interactive menu` |
| Hidden parameters | hidden parameter, not a public interface; compatibility is not guaranteed: `direct` |
| Op table | uname, stats, font, encoding, chinese, rtc, hpet, pmtimer, tsc, lspci, pcidump, pcibars, pcie, acpi, acpidev, acpipow, i2cdetect, smbus, usb, usbdev, uvc, scsi, gameport, parallel, ir, touchpad |
| Return codes | 0, 1 |

**Executed example**:

```
help_wp_test
help_wp_test: registry flat A-Z ordering (one group == all): PASS
help_wp_test: global listing still sorted: PASS
help_wp_test: run 'help' to see the flat A-Z listing
ush> 
```

(Note: classic dispatch (verbatim op passthrough))

### net-tool

| Field | Value |
|---|---|
| Name | net-tool |
| Category | The 8 merged apps |
| Function | network wizard (no args) or verbatim net+wireless ops |
| Usage | `net-tool            (interactive menu` |
| Hidden parameters | hidden parameter, not a public interface; compatibility is not guaranteed: `direct` |
| Op table | arp, dhcp, dns, dnstest, firewall, ip, lspci, nf_test, route, tcpcc_test, tcpstats, tcptest, ssh, sshd, wifi, bt |
| Return codes | 0, 1 |

**Executed example**:

```
net-tool nope
net-tool: unknown op: nope
usage: net-tool            (interactive menu)
       net-tool direct <op> [args]   (explicit passthrough)
       net-tool <op> [args]          (classic op dispatch)
ops:  arp
  dhcp
  dns
  dnstest
  firewall
  ip
  lspci
  nf_test
  route
  tcpcc_test
  tcpstats
  tcptest
  ssh
  sshd
  wifi
  bt
ush> 
```

(Note: classic dispatch (verbatim op passthrough))

### power

| Field | Value |
|---|---|
| Name | power |
| Category | The 8 merged apps |
| Function | power wizard (no args) or verbatim power ops (shutdown/reboot/halt/sleep) |
| Usage | `power            (interactive menu` |
| Hidden parameters | hidden parameter, not a public interface; compatibility is not guaranteed: `direct` |
| Op table | shutdown, poweroff, reboot, halt, sleep, suspend, poweroff_test, suspend_test, halt_test, reboot_test |
| Return codes | 0, 1 |

**Executed example**:

```
power nope
power: unknown op: nope
usage: power            (interactive menu)
       power direct <op> [args]   (explicit passthrough)
       power <op> [args]          (classic op dispatch)
ops:  shutdown
  poweroff
  reboot
  halt
  sleep
  suspend
  poweroff_test
  suspend_test
  halt_test
  reboot_test
ush> 
```

(Note: classic dispatch (verbatim op passthrough))

### sound-tool

| Field | Value |
|---|---|
| Name | sound-tool |
| Category | The 8 merged apps |
| Function | audio wizard (no args) or verbatim snd-service ops |
| Usage | `sound-tool            (interactive menu` |
| Hidden parameters | hidden parameter, not a public interface; compatibility is not guaranteed: `direct` |
| Op table | sound, hda, ac97, sb16, es1370, virtiosnd, usbaudio, play, volume, hda_test, ac97_test, sb16_test, es1370_test, virtio_snd_test, usb_audio_test, audio_rw_test, sample_rate_test |
| Return codes | 0, 1 |

**Executed example**:

```
sound-tool nope
sound-tool: unknown op: nope
usage: sound-tool            (interactive menu)
       sound-tool direct <op> [args]   (explicit passthrough)
       sound-tool <op> [args]          (classic op dispatch)
ops:  sound
  hda
  ac97
  sb16
  es1370
  virtiosnd
  usbaudio
  play
  volume
  hda_test
  ac97_test
  sb16_test
  es1370_test
  virtio_snd_test
  usb_audio_test
  audio_rw_test
  sample_rate_test
ush> 
```

(Note: classic dispatch (verbatim op passthrough))

### sysdiag

| Field | Value |
|---|---|
| Name | sysdiag |
| Category | The 8 merged apps |
| Function | system diagnostics wizard (no args) or verbatim diag/health/log ops |
| Usage | `sysdiag            (interactive menu` |
| Hidden parameters | hidden parameter, not a public interface; compatibility is not guaranteed: `direct` |
| Op table | mem, frag, heap, sched, syncstat, crashlog, crashtest, dumpinfo, cr3test, exc, pftest, pmmrace, vmtest, vmkernelpt, vmmap, memtest, heaptest, heapbounds, irqabitest, timer, msitest, spawn, multi, nice, synctest, fstest, acpi, acpidev, acpipow, acpitest, pcidump, pcibars, pcie, i2cdetect, i2cget, i2cset, i2ctest, smbus, smbustest, health, health export, health watch, dmesg, logcat, logctl, watchdog |
| Return codes | 0, 1 |

**Executed example**:

```
sysdiag nope
sysdiag: unknown op: nope
usage: sysdiag            (interactive menu)
       sysdiag direct <op> [args]   (explicit passthrough)
       sysdiag <op> [args]          (classic op dispatch)
ops:  mem
  frag
  heap
  sched
  syncstat
  crashlog
  crashtest
  dumpinfo
  cr3test
  exc
  pftest
  pmmrace
  vmtest
  vmkernelpt
  vmmap
  memtest
  heaptest
  heapbounds
  irqabitest
  timer
  msitest
  spawn
  multi
  nice
  synctest
  fstest
  acpi
  acpidev
  acpipow
  acpitest
  pcidump
  pcibars
  pcie
  i2cdetect
  i2cget
  i2cset
  i2ctest
  smbus
  smbustest
  health
  health export
  health watch
  dmesg
  logcat
  logctl
  watchdog
ush> 
```

(Note: classic dispatch (verbatim op passthrough))

### update

| Field | Value |
|---|---|
| Name | update |
| Category | The 8 merged apps |
| Function | OTA update wizard (no args) or the verbatim update op |
| Usage | `update [...]` |
| Source | `ocos/services/selfhost/src/svc_selfhost_l1.c` |

**Executed example**:

```
update status
update: current version: 3.0.0-NAK
update: current boot: ISO
update: next boot: (default)
update: available: unknown (run update or checkupdate)
update: online update: enabled
update: A/B disk: absent
ush> 
```

(Note: classic dispatch (verbatim op passthrough))
