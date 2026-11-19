<!-- SPDX-License-Identifier: Apache-2.0 -->
# Application dictionary - Kernel log (2)

### logcat

| Field | Value |
|---|---|
| Name | logcat |
| Category | Kernel log |
| Function | filtered kernel log stream |
| Usage | `logcat -l lvl -m mod -k key -t sec -n N [-f]` |
| Return codes | 0, 1 |
| Source | `ocos/services/klog/src/svc_klog_l1.c` |

**Executed example**:

```
logcat
[0.320] INFO  boot: [00:00:00.310] boot chain 2/4: drivers + driver-free services.... OK
[0.330] INFO  boot: [00:00:00.320] VFS (virtual file system)......................... OK
[0.360] INFO  boot: [00:00:00.350] ramfs (in-memory file system, mounted at /)....... OK
[0.570] INFO  boot: [00:00:00.560] ATA/IDE PIO driver (LBA28)........................ OK
[0.590] INFO  boot: [00:00:00.580] virtio-blk + AHCI + NVMe drivers.................. OK
[0.750] INFO  boot: [00:00:00.740] USB hosts up: 0................................... OK
[0.760] INFO  boot: [00:00:00.750] USB stack (4 HC backends, HID/MSC/serial/audio)... OK
[0.790] INFO  boot: [00:00:00.760] boot chain 3/4: driver-dependent services......... OK
[0.830] INFO  boot: [00:00:00.820] FAT32 (R/W) + exFAT (R/W) + ext4 (RO) drivers..... OK
[0.860] INFO  boot: config: created default /etc/opencube.conf (ramfs)
[0.870] INFO  boot: [00:00:00.860] system config (/etc/opencube.conf, fat32/ramfs) (OK)
[0.880] INFO  boot: [00:00:00.870] ACPI PM timer (FADT PM_TMR_BLK)................... OK
[0.890] INFO  boot: abupdate: no disk with an A/B partition layout found
[1.470] INFO  boot: [00:00:01.460] LAPIC (local APIC)................................ OK
[2.040] INFO  boot: [00:00:02.030] network stack (e1000 + TCP/IP + socket API)....... OK
[2.040] INFO  watchdog: slot 1 registered: kernel-health 15000 ms policy 0
[2.050] INFO  watchdog: slot 1 (kernel-health) ARMED, 15000 ms
[2.060] INFO  health: monitor started (1 s samples, alarms: mem>90% tasks>64 cpu>=90%x10s)
[2.080] INFO  boot: [00:00:02.070] health monitor (cpu/mem/tasks/disk/net + alarms).. OK
[2.260] INFO  boot: [00:00:02.250] sound: no sound card present (probe OK)........... OK
[2.270] INFO  boot: block devices: 0 registered
[2.280] INFO  boot: disk cache: 64 slots, LRU write-back
[2.290] INFO  boot: [00:00:02.280] disk subsystem (block dev + cache + partition + FS). OK
[2.300] INFO  boot: pmm: 131040 pages, 128656 free
[2.310] INFO  boot: [00:00:02.300] kmalloc/kfree self-test........................... OK
[2.400] INFO  boot: [00:00:02.390] boot complete..................................... OK
[2.410] INFO  boot: [00:00:02.400] boot chain 4/4: interfaces ready.................. OK
[2.420] INFO  boot: [00:00:02.410] crash recovery armed (panic + dump + crashlog).... OK
[2.430] INFO  boot: [00:00:02.420] L0 ready, waiting for L1.......................... OK
[2.440] INFO  boot: l1 launcher: booting 'ush' (ring 3, 23496 bytes)
-- 30 lines --
'
ush> 
```

### logctl

| Field | Value |
|---|---|
| Name | logctl |
| Category | Kernel log |
| Function | kernel log level control |
| Usage | `logctl status\|level\|...; logctl enable\|disable debug\|info\|warn\|error\|fatal ; logctl filter clear \| filter [level] [module] [keyword] ; logctl status\|level\|enable\|disable\|clear\|flush\|rotate\|threshold\|export\|filter\|test ; logctl threshold BYTES (min 512, default 1048576` |
| Hidden parameters | hidden parameter, not a public interface; compatibility is not guaranteed: `-` |
| Return codes | 0, 1 |
| Source | `ocos/services/klog/src/svc_klog_l1.c` |

**Executed example**:

```
logctl
klog status:
  ring: 87/1024 entries, dropped 0
  live level (console+serial): 1 (INFO)
  level switches: DEBUG=on INFO=on WARN=on ERROR=on FATAL=on
  file: /var/log/kernel.log open, 3467 bytes (rotate at 1048576, keep 3)
  filter: none
ush> 
```
