<!-- SPDX-License-Identifier: Apache-2.0 -->
# 应用字典 - 系统工具 (18)

### date

| 字段 | 值 |
|---|---|
| 名称 | date |
| 分类 | 系统工具 |
| 功能 | 显示 RTC 日期时间 |
| 返回码 | 0 |

**实际执行示例**：

```
date
Open Cube OS uptime: 3130 ms since boot (no RTC driver)
ush> 
```

### dmesg

| 字段 | 值 |
|---|---|
| 名称 | dmesg |
| 分类 | 系统工具 |
| 功能 | 显示内核日志 |
| 返回码 | 0, 1 |

**实际执行示例**：

```
dmesg
[0.240] INFO  boot: [00:00:00.230] VFS (virtual file system)......................... OK
[0.270] INFO  boot: [00:00:00.260] ramfs (in-memory file system, mounted at /)....... OK
[0.480] INFO  boot: [00:00:00.470] ATA/IDE PIO driver (LBA28)........................ OK
[0.500] INFO  boot: [00:00:00.490] virtio-blk + AHCI + NVMe drivers.................. OK
[0.650] INFO  boot: [00:00:00.640] USB hosts up: 0................................... OK
[0.660] INFO  boot: [00:00:00.650] USB stack (4 HC backends, HID/MSC/serial/audio)... OK
[0.690] INFO  boot: [00:00:00.660] boot chain 3/4: driver-dependent services......... OK
[0.770] INFO  boot: [00:00:00.740] FAT32 (R/W) + exFAT (R/W) + ext4 (RO) drivers..... OK
[0.800] INFO  boot: config: created default /etc/opencube.conf (ramfs)
[0.810] INFO  boot: [00:00:00.800] system config (/etc/opencube.conf, fat32/ramfs) (OK)
[0.820] INFO  boot: [00:00:00.810] ACPI PM timer (FADT PM_TMR_BLK)................... OK
[0.830] INFO  boot: abupdate: no disk with an A/B partition layout found
[1.430] INFO  boot: [00:00:01.420] LAPIC (local APIC)................................ OK
[1.990] INFO  boot: [00:00:01.980] network stack (e1000 + TCP/IP + socket API)....... OK
[1.990] INFO  watchdog: slot 1 registered: kernel-health 15000 ms policy 0
[2.000] INFO  watchdog: slot 1 (kernel-health) ARMED, 15000 ms
[2.010] INFO  health: monitor started (1 s samples, alarms: mem>90% tasks>64 cpu>=90%x10s)
[2.030] INFO  boot: [00:00:02.020] health monitor (cpu/mem/tasks/disk/net + alarms).. OK
[2.210] INFO  boot: [00:00:02.200] sound: no sound card present (probe OK)........... OK
[2.220] INFO  boot: block devices: 0 registered
[2.230] INFO  boot: disk cache: 64 slots, LRU write-back
[2.240] INFO  boot: [00:00:02.230] disk subsystem (block dev + cache + partition + FS). OK
[2.250] INFO  boot: pmm: 131040 pages, 128681 free
[2.260] INFO  boot: [00:00:02.250] kmalloc/kfree self-test........................... OK
[2.350] INFO  boot: [00:00:02.340] boot complete..................................... OK
[2.360] INFO  boot: [00:00:02.350] boot chain 4/4: interfaces ready.................. OK
[2.370] INFO  boot: [00:00:02.360] crash recovery armed (panic + dump + crashlog).... OK
[2.380] INFO  boot: [00:00:02.370] L0 ready, waiting for L1.......................... OK
[2.390] INFO  boot: l1 launcher: booting 'ush' (ring 3, 23496 bytes)
[13.010] ERROR health: alarm: CPU >=90% busy for 10 consecutive seconds
-- 30 lines (ring 81/1024, dropped 0) --
ush> 
```

### echo

| 字段 | 值 |
|---|---|
| 名称 | echo |
| 分类 | 系统工具 |
| 功能 | 打印参数 |
| 返回码 | 0 |

**实际执行示例**：

```
echo hello dictionary
hello dictionary
ush> 
```

### free

| 字段 | 值 |
|---|---|
| 名称 | free |
| 分类 | 系统工具 |
| 功能 | 内存使用摘要 |
| 返回码 | 0 |

**实际执行示例**：

```
font-test
font-test: rendering sample row (16x16 CJK):

font-test: 16px row drawn above (zhongwen OpenCube shi)

ush> 
```

### health

| 字段 | 值 |
|---|---|
| 名称 | health |
| 分类 | 系统工具 |
| 功能 | 系统健康快照/监视 |
| 用法 | `health [watch\|export [path]]` |
| 返回码 | 0, 1 |

**实际执行示例**：

```
hda
Intel HDA: not present
ush> 
```

### help

| 字段 | 值 |
|---|---|
| 名称 | help |
| 分类 | 系统工具 |
| 功能 | 显示 shell 内建命令与程序注册表 |
| 隐藏参数 | 隐藏参数，非公开接口，兼容性不保证: `--all`, `--wp`, `-a`, `-w` |
| 返回码 | 0, 1 |

**实际执行示例**：

```
heapbounds
Heap bounds test (BUG-0140 wrap / BUG-0141 ceiling):
  PASS
ush> 
```

### help_a_test

| 字段 | 值 |
|---|---|
| 名称 | help_a_test |
| 分类 | 系统工具 |
| 功能 | 注册表 A-Z 列表不变量测试 |

**实际执行示例**：

```
heaptest
Heap overhead test (100 allocs):
  heap_size=3657728 allocated=1583392 overhead=14176
  requested=26542 waste=1556850 overhead%=0
  after free: alloc=134 free=216 total_allocs=462 total_frees=328
  PASS
ush> 
```

### help_default_test

| 字段 | 值 |
|---|---|
| 名称 | help_default_test |
| 分类 | 系统工具 |
| 功能 | 注册表默认列表测试 |
| 返回码 | 0 |

**实际执行示例**：

```
hello
hello from userspaceush> 
```

### help_wp_test

| 字段 | 值 |
|---|---|
| 名称 | help_wp_test |
| 分类 | 系统工具 |
| 功能 | 注册表工作包分组测试 |

**实际执行示例**：

```
help

Open Cube OS User-space Shell (ush) - built-in commands:

cd [dir]        change directory (shell state)
help [-a|-w]    this text; -a = full A-Z, -w = by work package
export VAR=val  set environment variable
alias name=val  set command alias
unalias name    remove alias
env             show environment variables
set             show a variable
history         show command history
jobs            list background jobs
fg [job]        bring a job to the foreground
bg              send a job to the background
exit            not available (use shutdown / reboot)

All other commands are ring-3 apps served from the registry.
Registry: 284 programs - 'help -a' lists them all, 'help -w' groups by work package.

ush> 
```

### kill

| 字段 | 值 |
|---|---|
| 名称 | kill |
| 分类 | 系统工具 |
| 功能 | 终止进程 |
| 用法 | `kill <pid>` |
| 参数 | `pid` |
| 返回码 | 0 |

**实际执行示例**：

```
irtest
irtest: NEC / RC5 / RC6 / SIRC codec round trips
  nec known (1A/3C): PASS
  rc5 known (05/15 t1): PASS
  rc6 known (12/34 t0): PASS
  rt nec 0x12/0x34: PASS
  rt nec 0xFF/0x00: PASS
  rt nec repeat: PASS
  rt rc5 0x0A/0x0C t0: PASS
  rt rc5 0x1B/0x2D t1: PASS
  rt rc6 0x00/0xFF t0: PASS
  rt rc6 0xA5/0x5A t1: PASS
  rt sirc 0x15/0x01: PASS
  nec corrupted rejected: PASS
  live ir devices: 0
  (no live device: codec tests above ran on the real code paths)
irtest: 0 failures - PASS
ush> 
```

### p2test

| 字段 | 值 |
|---|---|
| 名称 | p2test |
| 分类 | 系统工具 |
| 功能 | WP-fix-p2-p3-cleanup 实机探针（UTF-8 分块 / 零长读 / 管道） |
| 用法 | `p2test <utf8\|read0\|pipe>` |
| 返回码 | 0, 1 |

**实际执行示例**：

```
p2test
usage: p2test <utf8|read0|pipe>
ush> 
```

(备注: no-arguments usage)

### ps

| 字段 | 值 |
|---|---|
| 名称 | ps |
| 分类 | 系统工具 |
| 功能 | 列出进程 |
| 返回码 | 0 |

**实际执行示例**：

```
ps
TID  STATE  PRI  CPU(ticks)  NAME
0    RUN   31    233          idle
1    READY 31    26          usb-poll
2    READY 15    28          ush
3    RUN   15    0          ush
ush> 
```

### pwd

| 字段 | 值 |
|---|---|
| 名称 | pwd |
| 分类 | 系统工具 |
| 功能 | 显示当前目录 |
| 返回码 | 0 |

**实际执行示例**：

```
pwd
/
ush> 
```

### run

| 字段 | 值 |
|---|---|
| 名称 | run |
| 分类 | 系统工具 |
| 功能 | 带参数运行程序 |
| 用法 | `run <program>   (registered programs below` |
| 参数 | `program` |
| 返回码 | 0, 1 |

**实际执行示例**：

```
run
usage: run <program>   (registered programs below)
ab_partition_test
abcfg
abdisk
ac97
ac97_test
acpi
acpidev
acpipow
acpitest
ahci
ahci_test
arp
ata
ata_dma_test
audio_rw_test
awk
badapp
bcm57xx
bcm57xx_test
bt
cat
checkupdate
checkupdate_test
chinese
chinesetest
chmod
chown
clear
config
config_test
cp
cr3test
crashlog
crashtest
cryptotest
date
df
dhcp
dhtest
disk-tool
disk_rw_test
dlsym_test
dmesg
dns
dnstest
dskstat
du
dumpinfo
dyn_hello
e1000e
e1000e_test
echo
edit
encoding
encoding-test
es1370
es1370_test
exc
exec_test
fatmount
fatstat
fdref_test
files
firewall
font
font-test
fork_test
frag
free
fs_mount_test
fsck
fstest
g6_exec_a
g6_exec_b
g6_oom
g6_test
g7mmap
gameport
gameporttest
grep
grub-install
halt
halt_test
hda
hda_test
head
health
heap
heapbounds
heaptest
hello
help
help_a_test
help_default_test
help_wp_test
hpet
hwinfo
i2cdetect
i2cget
i2cset
i2ctest
ifconfig
igb
igb_test
ime
ime-test
install
int3_user
ip
ir
irqabitest
irtest
ixgbe
ixgbe_test
kill
l1test
lang
ld_so
ldd
ln
logcat
logctl
loop
ls
lsblk
lspci
main_dyn
mem
memtest
mkdir
mkfs.exfat
mkfs.ext4
mkfs.fat32
mmap_multi
mmap_test
mount
mounts
mprotect_test
msitest
multi
mv
nano
net-tool
netstat
nf_test
nic_rw_test
nice
nvme
nvme_test
other_nic_test
p0leak
p2test
p3_test
parallel
paralleltest
parted
partition_test
pcibars
pcidump
pcie
pf_test
pftest
pie_test
ping
pipe_test
play
pmmrace
pmtimer
power
poweroff
poweroff_test
priv_test
ps
pwd
real_hw_test
real_update_test
reboot
reboot_test
reloc_test
rm
rmdir
rollback
route
rtc
rtl810x
rtl810x_test
rtl8125
rtl8125_test
rtl8139
rtl8139_test
rtl8168
rtl8168_test
run
sample_rate_test
sb16
sb16_test
sched
scsi
scsitest
sed
select_test
select_zero_test
serial_encoding
shutdown
signal_test
sigsegv_test
sleep
smbus
smbustest
so_test
sort
sound
sound-tool
spawn
sse_test
ssh
sshd
stackovf
stat
stats
suspend_test
sync
syncstat
synctest
sysdiag
tail
tcpcc_test
tcpstats
tcptest
timer
top
touch
touchpad
touchtest
tree
tsc
tui
tui-drv
tui-ext
tui-hw
tui-net
tui-sys
tui-sysinfo
tui-test
tui-time
umount
uname
uniq
update
update_check_test
update_download_test
update_install_test
update_local_test
update_pkg_test
update_rollback_test
update_status_test
update_verify_test
usb
usb_audio_test
usb_core_test
usb_hotplug_test
usb_hub_test
usb_kbd_test
usb_mouse_test
usb_serial_test
usb_storage_test
usbaudio
usbdev
ush
uvc
uvctest
vi
virtio_blk_test
virtio_snd_test
virtiosnd
vmkernelpt
vmmap
vmtest
volume
watchdog
wc
wget
wifi
write
-- 284 programs registered --
ush> 
```

### sed

| 字段 | 值 |
|---|---|
| 名称 | sed |
| 分类 | 系统工具 |
| 功能 | 流编辑器 |
| 用法 | `sed s/old/new/[g] [file] \| sed -n Np [file] \| sed Nd [file]` |
| 返回码 | 0 |

**实际执行示例**：

```
sed
usage: sed s/old/new/[g] [file] | sed -n Np [file] | sed Nd [file]
ush> 
```

### top

| 字段 | 值 |
|---|---|
| 名称 | top |
| 分类 | 系统工具 |
| 功能 | 进程总览（动态） |
| 隐藏参数 | 隐藏参数，非公开接口，兼容性不保证: `-n` |
| 返回码 | 0 |

**实际执行示例**：

```
top
top - up 5290 ms
Mem: 10825728 used / 536739840 total / 525914112 free
TID  STATE  PRI  CPU(ticks)  NAME
0    RUN   31    236          idle
1    READY 31    14          usb-poll
2    BLOCK 15    102          ush
3    RUN   15    2          ush
ush> 
```

(备注: full-screen page; the quit key was not honored in this session - output as captured, session reset)

### ush

| 字段 | 值 |
|---|---|
| 名称 | ush |
| 分类 | 系统工具 |
| 功能 | Open Cube OS 的 ring-3 shell（默认入口） |
| 用法 | `unalias name` |
| 隐藏参数 | 隐藏参数，非公开接口，兼容性不保证: `--all`, `--wp`, `-a`, `-w`, `bg`, `cd`, `env`, `exit`, `export`, `fg`, `help`, `history`, `jobs`, `set` |
| 返回码 | -1, 0, 1, 2 |

**实际执行示例**：

```
ush

Open Cube OS User-space Shell (3.0.0-NAK)
Type 'help' for built-in commands.

ush> 
```

### write

| 字段 | 值 |
|---|---|
| 名称 | write |
| 分类 | 系统工具 |
| 功能 | 把标准输入写入文件 |
| 用法 | `write <path> <text>` |
| 参数 | `path`, `text` |
| 返回码 | 0 |

**实际执行示例**：

```
write
usage: write <path> <text>
ush> 
```
