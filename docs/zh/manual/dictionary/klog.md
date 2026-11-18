<!-- SPDX-License-Identifier: Apache-2.0 -->
# 应用字典 - 内核日志 (2)

### logcat

| 字段 | 值 |
|---|---|
| 名称 | logcat |
| 分类 | 内核日志 |
| 功能 | 带过滤的内核日志流 |
| 用法 | `logcat -l lvl -m mod -k key -t sec -n N [-f]` |
| 返回码 | 0, 1 |
| 来源 | `ocos/services/klog/src/svc_klog_l1.c` |

**实际执行示例**：

```
logcat
[0.340] INFO  boot: [00:00:00.330] boot chain 2/4: drivers + driver-free services.... OK
[0.350] INFO  boot: [00:00:00.340] VFS (virtual file system)......................... OK
[0.380] INFO  boot: [00:00:00.370] ramfs (in-memory file system, mounted at /)....... OK
[0.620] INFO  boot: [00:00:00.610] ATA/IDE PIO driver (LBA28)........................ OK
[0.640] INFO  boot: [00:00:00.630] virtio-blk + AHCI + NVMe drivers.................. OK
[0.810] INFO  boot: [00:00:00.800] USB hosts up: 0................................... OK
[0.820] INFO  boot: [00:00:00.810] USB stack (4 HC backends, HID/MSC/serial/audio)... OK
[0.860] INFO  boot: [00:00:00.820] boot chain 3/4: driver-dependent services......... OK
[0.940] INFO  boot: [00:00:00.930] FAT32 (R/W) + exFAT (R/W) + ext4 (RO) drivers..... OK
[0.970] INFO  boot: config: created default /etc/opencube.conf (ramfs)
[0.980] INFO  boot: [00:00:00.970] system config (/etc/opencube.conf, fat32/ramfs) (OK)
[0.990] INFO  boot: [00:00:00.980] ACPI PM timer (FADT PM_TMR_BLK)................... OK
[1.000] INFO  boot: abupdate: no disk with an A/B partition layout found
[1.660] INFO  boot: [00:00:01.650] LAPIC (local APIC)................................ OK
[2.270] INFO  boot: [00:00:02.260] network stack (e1000 + TCP/IP + socket API)....... OK
[2.270] INFO  watchdog: slot 1 registered: kernel-health 15000 ms policy 0
[2.280] INFO  watchdog: slot 1 (kernel-health) ARMED, 15000 ms
[2.290] INFO  health: monitor started (1 s samples, alarms: mem>90% tasks>64 cpu>=90%x10s)
[2.310] INFO  boot: [00:00:02.300] health monitor (cpu/mem/tasks/disk/net + alarms).. OK
[2.500] INFO  boot: [00:00:02.490] sound: no sound card present (probe OK)........... OK
[2.510] INFO  boot: block devices: 0 registered
[2.520] INFO  boot: disk cache: 64 slots, LRU write-back
[2.540] INFO  boot: [00:00:02.520] disk subsystem (block dev + cache + partition + FS). OK
[2.550] INFO  boot: pmm: 131040 pages, 128656 free
[2.560] INFO  boot: [00:00:02.550] kmalloc/kfree self-test........................... OK
[2.650] INFO  boot: [00:00:02.640] boot complete..................................... OK
[2.670] INFO  boot: [00:00:02.650] boot chain 4/4: interfaces ready.................. OK
[2.680] INFO  boot: [00:00:02.670] crash recovery armed (panic + dump + crashlog).... OK
[2.690] INFO  boot: [00:00:02.680] L0 ready, waiting for L1.......................... OK
[2.700] INFO  boot: l1 launcher: booting 'ush' (ring 3, 23496 bytes)
-- 30 lines --
'
ush> 
```

### logctl

| 字段 | 值 |
|---|---|
| 名称 | logctl |
| 分类 | 内核日志 |
| 功能 | 内核日志级别控制 |
| 用法 | `logctl status|level|...; logctl enable|disable debug|info|warn|error|fatal ; logctl filter clear | filter [level] [module] [keyword] ; logctl status|level|enable|disable|clear|flush|rotate|threshold|export|filter|test ; logctl threshold BYTES (min 512, default 1048576` |
| 隐藏参数 | 隐藏参数，非公开接口，兼容性不保证: `-` |
| 返回码 | 0, 1 |
| 来源 | `ocos/services/klog/src/svc_klog_l1.c` |

**实际执行示例**：

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
