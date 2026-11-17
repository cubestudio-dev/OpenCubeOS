<!-- SPDX-License-Identifier: Apache-2.0 -->
# 应用字典 - 八大融合应用 (8)

### disk-tool

| 字段 | 值 |
|---|---|
| 名称 | disk-tool |
| 分类 | 八大融合应用 |
| 功能 | 磁盘 / 文件系统向导（无参数）或原样透传的块设备+文件系统操作 |
| 用法 | `disk-tool            (interactive menu` |
| 隐藏参数 | 隐藏参数，非公开接口，兼容性不保证: `direct` |
| 操作表 | ahci, nvme, ata, dskstat, lsblk, parted, mkfs.fat32, mkfs.exfat, mkfs.ext4, fsck, sync, ahci_test, nvme_test, ata_dma_test, virtio_blk_test, disk_rw_test, partition_test, fs_mount_test, real_hw_test, abdisk, install, grub-install, abcfg, mount, mounts, umount, fatmount, fatstat, tree |
| 返回码 | 0, 1 |

**实际执行示例**：

```
disk-tool lsblk
Block devices:
Disk cache: 0/64 slots  hits=0 misses=0
ush> 
```

(备注: classic dispatch (verbatim op passthrough))

### files

| 字段 | 值 |
|---|---|
| 名称 | files |
| 分类 | 八大融合应用 |
| 功能 | 文件管理向导（无参数）或程序直通分发（ls/cp/mv/rm/edit/...） |
| 用法 | `files                (interactive menu` |
| 隐藏参数 | 隐藏参数，非公开接口，兼容性不保证: `direct` |
| 操作表 | ls, tree, df, du, cp, mv, rm, edit, stat, chmod |
| 返回码 | 0, 1 |

**实际执行示例**：

```
fatmount
fat32: invalid device string
vfs_mount: fs mount failed
fatmount: ata0 -> /mnt FAILED - no FAT32 partition on device or drive not present
(the mount point was NOT created - `ls /mnt` will report 'no such path' until a real mount succeeds)
ush> 
```

(备注: classic dispatch (verbatim op passthrough))

### hwinfo

| 字段 | 值 |
|---|---|
| 名称 | hwinfo |
| 分类 | 八大融合应用 |
| 功能 | 硬件盘点向导（无参数）或原样透传的信息类操作 |
| 用法 | `hwinfo            (interactive menu` |
| 隐藏参数 | 隐藏参数，非公开接口，兼容性不保证: `direct` |
| 操作表 | uname, stats, font, encoding, chinese, rtc, hpet, pmtimer, tsc, lspci, pcidump, pcibars, pcie, acpi, acpidev, acpipow, i2cdetect, smbus, usb, usbdev, uvc, scsi, gameport, parallel, ir, touchpad |
| 返回码 | 0, 1 |

**实际执行示例**：

```
help_wp_test
help_wp_test: registry flat A-Z ordering (one group == all): PASS
help_wp_test: global listing still sorted: PASS
help_wp_test: run 'help' to see the flat A-Z listing
ush> 
```

(备注: classic dispatch (verbatim op passthrough))

### net-tool

| 字段 | 值 |
|---|---|
| 名称 | net-tool |
| 分类 | 八大融合应用 |
| 功能 | 网络向导（无参数）或原样透传的网络+无线操作 |
| 用法 | `net-tool            (interactive menu` |
| 隐藏参数 | 隐藏参数，非公开接口，兼容性不保证: `direct` |
| 操作表 | arp, dhcp, dns, dnstest, firewall, ip, lspci, nf_test, route, tcpcc_test, tcpstats, tcptest, ssh, sshd, wifi, bt |
| 返回码 | 0, 1 |

**实际执行示例**：

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

(备注: classic dispatch (verbatim op passthrough))

### power

| 字段 | 值 |
|---|---|
| 名称 | power |
| 分类 | 八大融合应用 |
| 功能 | 电源向导（无参数）或原样透传的电源操作（shutdown/reboot/halt/sleep） |
| 用法 | `power            (interactive menu` |
| 隐藏参数 | 隐藏参数，非公开接口，兼容性不保证: `direct` |
| 操作表 | shutdown, poweroff, reboot, halt, sleep, suspend, poweroff_test, suspend_test, halt_test, reboot_test |
| 返回码 | 0, 1 |

**实际执行示例**：

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

(备注: classic dispatch (verbatim op passthrough))

### sound-tool

| 字段 | 值 |
|---|---|
| 名称 | sound-tool |
| 分类 | 八大融合应用 |
| 功能 | 音频向导（无参数）或原样透传的声音服务操作 |
| 用法 | `sound-tool            (interactive menu` |
| 隐藏参数 | 隐藏参数，非公开接口，兼容性不保证: `direct` |
| 操作表 | sound, hda, ac97, sb16, es1370, virtiosnd, usbaudio, play, volume, hda_test, ac97_test, sb16_test, es1370_test, virtio_snd_test, usb_audio_test, audio_rw_test, sample_rate_test |
| 返回码 | 0, 1 |

**实际执行示例**：

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

(备注: classic dispatch (verbatim op passthrough))

### sysdiag

| 字段 | 值 |
|---|---|
| 名称 | sysdiag |
| 分类 | 八大融合应用 |
| 功能 | 系统诊断向导（无参数）或原样透传的诊断/健康/日志操作 |
| 用法 | `sysdiag            (interactive menu` |
| 隐藏参数 | 隐藏参数，非公开接口，兼容性不保证: `direct` |
| 操作表 | mem, frag, heap, sched, syncstat, crashlog, crashtest, dumpinfo, cr3test, exc, pftest, pmmrace, vmtest, vmkernelpt, vmmap, memtest, heaptest, heapbounds, irqabitest, timer, msitest, spawn, multi, nice, synctest, fstest, acpi, acpidev, acpipow, acpitest, pcidump, pcibars, pcie, i2cdetect, i2cget, i2cset, i2ctest, smbus, smbustest, health, health export, health watch, dmesg, logcat, logctl, watchdog |
| 返回码 | 0, 1 |

**实际执行示例**：

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

(备注: classic dispatch (verbatim op passthrough))

### update

| 字段 | 值 |
|---|---|
| 名称 | update |
| 分类 | 八大融合应用 |
| 功能 | OTA 更新向导（无参数）或原样透传的 update 操作 |
| 用法 | `update [...]` |
| 来源 | `ocos/services/selfhost/src/svc_selfhost_l1.c` |

**实际执行示例**：

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

(备注: classic dispatch (verbatim op passthrough))
