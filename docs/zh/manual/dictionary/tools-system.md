<!-- SPDX-License-Identifier: Apache-2.0 -->
# 应用字典 - 系统工具 (34)

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
Open Cube OS uptime: 3460 ms since boot (no RTC driver)
ush> 
```

### dev

| 字段 | 值 |
|---|---|
| 名称 | dev |
| 分类 | 系统工具 |
| 功能 | 设备树诊断（总命令） |

**实际执行示例**：

```
dev tree
device tree: 6 node(s)
[1] root  type=root  drv=-
  [2] vtest-bus0  type=vtest-bus  drv=vtest-bus  D0
    [3] vtest-ctl0  type=vtest-ctl  drv=vtest-ctl  D0  mmio 0xFEE00000+0x1000  irq 11
    [4] vtest-ctl1  type=vtest-ctl  drv=vtest-ctl  D0  mmio 0xFEE01000+0x1000  irq 11
  [5] vtest-fail0  type=vtest-fail  drv=-  unbound (last probe error -55)
  [6] vtest-late0  type=vtest-late  drv=vtest-late  D0
ush> 
```

### dev_debug

| 字段 | 值 |
|---|---|
| 名称 | dev_debug |
| 分类 | 系统工具 |
| 功能 | 开关设备树调试日志（调试） |

**实际执行示例**：

```
dev_debug
dev: missing argument for 'debug'
usage: dev debug <on|off>
ush> 
```

### dev_help

| 字段 | 值 |
|---|---|
| 名称 | dev_help |
| 分类 | 系统工具 |
| 功能 | dev 命令用法 |

**实际执行示例**：

```
dev_help
dev: driver framework diagnostics

usage: dev <subcommand> [args]

subcommands:
  dev tree - print the device tree
  dev list [type] - list device nodes (optionally by type)
  dev info <id|name> - device node details
  dev debug <on|off> - debug: framework log switch
  dev help - this usage text
ush> 
```

### dev_info

| 字段 | 值 |
|---|---|
| 名称 | dev_info |
| 分类 | 系统工具 |
| 功能 | 查看单个设备节点详情 |

**实际执行示例**：

```
dev_info vtest-ctl0
node: vtest-ctl0
  id        3
  type      vtest-ctl
  parent    vtest-bus0 (id 2)
  children  0
  driver    vtest-ctl (meta path)
  d-state   D0
  resources 
    mmio  0xFEE00000 + 0x1000
    irq   11
  priv      0xC7
ush> 
```

### dev_list

| 字段 | 值 |
|---|---|
| 名称 | dev_list |
| 分类 | 系统工具 |
| 功能 | 按类型列出设备节点 |

**实际执行示例**：

```
dev_list
device nodes:
id   name           type         driver
[1] root          root        -
[2] vtest-bus0    vtest-bus   vtest-bus
[3] vtest-ctl0    vtest-ctl   vtest-ctl
[4] vtest-ctl1    vtest-ctl   vtest-ctl
[5] vtest-fail0   vtest-fail  -
[6] vtest-late0   vtest-late  vtest-late
ush> 
```

### dev_tree

| 字段 | 值 |
|---|---|
| 名称 | dev_tree |
| 分类 | 系统工具 |
| 功能 | 打印设备树 |

**实际执行示例**：

```
dev_tree
device tree: 6 node(s)
[1] root  type=root  drv=-
  [2] vtest-bus0  type=vtest-bus  drv=vtest-bus  D0
    [3] vtest-ctl0  type=vtest-ctl  drv=vtest-ctl  D0  mmio 0xFEE00000+0x1000  irq 11
    [4] vtest-ctl1  type=vtest-ctl  drv=vtest-ctl  D0  mmio 0xFEE01000+0x1000  irq 11
  [5] vtest-fail0  type=vtest-fail  drv=-  unbound (last probe error -55)
  [6] vtest-late0  type=vtest-late  drv=vtest-late  D0
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
[0.330] INFO  boot: [00:00:00.320] VFS (virtual file system)......................... OK
[0.360] INFO  boot: [00:00:00.350] ramfs (in-memory file system, mounted at /)....... OK
[0.570] INFO  boot: [00:00:00.560] ATA/IDE PIO driver (LBA28)........................ OK
[0.590] INFO  boot: [00:00:00.580] virtio-blk + AHCI + NVMe drivers.................. OK
[0.750] INFO  boot: [00:00:00.740] USB hosts up: 0................................... OK
[0.760] INFO  boot: [00:00:00.750] USB stack (4 HC backends, HID/MSC/serial/audio)... OK
[0.790] INFO  boot: [00:00:00.760] boot chain 3/4: driver-dependent services......... OK
[0.870] INFO  boot: [00:00:00.840] FAT32 (R/W) + exFAT (R/W) + ext4 (RO) drivers..... OK
[0.920] INFO  boot: config: created default /etc/opencube.conf (ramfs)
[0.950] INFO  boot: [00:00:00.920] system config (/etc/opencube.conf, fat32/ramfs) (OK)
[0.960] INFO  boot: [00:00:00.950] ACPI PM timer (FADT PM_TMR_BLK)................... OK
[0.990] INFO  boot: abupdate: no disk with an A/B partition layout found
[1.750] INFO  boot: [00:00:01.740] LAPIC (local APIC)................................ OK
[2.300] INFO  boot: [00:00:02.290] network stack (e1000 + TCP/IP + socket API)....... OK
[2.300] INFO  watchdog: slot 1 registered: kernel-health 15000 ms policy 0
[2.310] INFO  watchdog: slot 1 (kernel-health) ARMED, 15000 ms
[2.320] INFO  health: monitor started (1 s samples, alarms: mem>90% tasks>64 cpu>=90%x10s)
[2.340] INFO  boot: [00:00:02.330] health monitor (cpu/mem/tasks/disk/net + alarms).. OK
[2.520] INFO  boot: [00:00:02.510] sound: no sound card present (probe OK)........... OK
[2.530] INFO  boot: block devices: 0 registered
[2.540] INFO  boot: disk cache: 64 slots, LRU write-back
[2.550] INFO  boot: [00:00:02.540] disk subsystem (block dev + cache + partition + FS). OK
[2.560] INFO  boot: pmm: 131040 pages, 128656 free
[2.570] INFO  boot: [00:00:02.560] kmalloc/kfree self-test........................... OK
[2.660] INFO  boot: [00:00:02.650] boot complete..................................... OK
[2.670] INFO  boot: [00:00:02.660] boot chain 4/4: interfaces ready.................. OK
[2.680] INFO  boot: [00:00:02.670] crash recovery armed (panic + dump + crashlog).... OK
[2.690] INFO  boot: [00:00:02.680] L0 ready, waiting for L1.......................... OK
[2.700] INFO  boot: l1 launcher: booting 'ush' (ring 3, 23496 bytes)
[13.320] ERROR health: alarm: CPU >=90% busy for 10 consecutive seconds
-- 30 lines (ring 88/1024, dropped 0) --
ush> 
```

### drv

| 字段 | 值 |
|---|---|
| 名称 | drv |
| 分类 | 系统工具 |
| 功能 | 驱动框架诊断（总命令） |

**实际执行示例**：

```
drv order
metadata driver init order (topological, deterministic:
dependencies first, then priority hint, then name - never link
order):
  1. vtest-bus
  2. vtest-ctl  <- vtest-bus
  3. vtest-fail
  4. vtest-late  <- vtest-ctl
  5. vtest-mon
cycle detection: 2 driver(s) isolated:
  vtest-cyc-b: dependency cycle (reported, never looped)
  vtest-cyc-a: dependency cycle (reported, never looped)
legacy OC_DRIVER_DECL drivers: 38 (initialized by the kmain family-manager sequence - outside
this computation by design)
ush> 
```

### drv_bind

| 字段 | 值 |
|---|---|
| 名称 | drv_bind |
| 分类 | 系统工具 |
| 功能 | 手动绑定驱动到设备（调试） |

**实际执行示例**：

```
drv_bind
drv: missing argument for 'bind'
usage: drv bind <drv> <dev>
ush> 
```

### drv_debug

| 字段 | 值 |
|---|---|
| 名称 | drv_debug |
| 分类 | 系统工具 |
| 功能 | 开关驱动框架调试日志（调试） |

**实际执行示例**：

```
drv_debug
drv: missing argument for 'debug'
usage: drv debug <on|off>
ush> 
```

### drv_failed

| 字段 | 值 |
|---|---|
| 名称 | drv_failed |
| 分类 | 系统工具 |
| 功能 | 列出 probe/init 失败 |

**实际执行示例**：

```
drv_failed
failed drivers: 1 (isolated - the boot was never blocked)
name            err   count  last seen
vtest-fail      -55  1     30 ms
ush> 
```

### drv_help

| 字段 | 值 |
|---|---|
| 名称 | drv_help |
| 分类 | 系统工具 |
| 功能 | drv 命令用法 |

**实际执行示例**：

```
drv_help
drv: driver framework diagnostics

usage: drv <subcommand> [args]

subcommands:
  drv list - list registered drivers (name, version, state, devices)
  drv info <name> - driver details (deps, d-states, probe result)
  drv failed - probe/init failures and their reasons
  drv bind <drv> <dev> - debug: manually bind a driver to a node
  drv unbind <drv> <dev> - debug: manually unbind a driver
  drv reload <drv> - debug: re-init and re-probe a driver
  drv debug <on|off> - debug: framework log switch
  drv order - init order (topological) + cycle report
  drv help - this usage text
ush> 
```

### drv_info

| 字段 | 值 |
|---|---|
| 名称 | drv_info |
| 分类 | 系统工具 |
| 功能 | 查看单个驱动详情 |

**实际执行示例**：

```
drv_info vtest-ctl
driver: vtest-ctl
  version   1.0
  author    cubestudio-dev
  license   Apache-2.0
  desc      verification virtual device (tests/drv_framework, not hw)
  path      meta (OC_DRIVER_INFO_DECL)
  state     ok
  priority  0
  depends   vtest-bus
  d-states  D0 D3
  failures  0
  hotplug   0 events received
  devices   vtest-ctl0 (id 3), vtest-ctl1 (id 4)
ush> 
```

### drv_list

| 字段 | 值 |
|---|---|
| 名称 | drv_list |
| 分类 | 系统工具 |
| 功能 | 列出已注册驱动 |

**实际执行示例**：

```
drv_list
driver registry: 45 drivers (7 metadata + 38 legacy)
name            version  state      devs  path    desc
vtest-bus       1.0      ok         1     meta    verification virtual bus (tests/drv_framework, not hw)
vtest-ctl       1.0      ok         2     meta    verification virtual device (tests/drv_framework, not hw)
vtest-cyc-b     1.0      cycle      0     meta    verification cycle canary B (tests/drv_framework, not hw)
vtest-cyc-a     1.0      cycle      0     meta    verification cycle canary A (tests/drv_framework, not hw)
vtest-fail      1.0      failed     0     meta    verification failure injector (tests/drv_framework, not hw)
vtest-late      1.0      ok         1     meta    verification late-addition (tests/drv_framework, not hw)
vtest-mon       1.0      ok         0     meta    verification hotplug observer (tests/drv_framework, not hw)
ac97            -        legacy     -     legacy  AC97 audio
es1370          -        legacy     -     legacy  Ensoniq ES1370 audio
hda             -        legacy     -     legacy  Intel HD Audio
sb16            -        legacy     -     legacy  Sound Blaster 16
virtio          -        legacy     -     legacy  virtio-snd audio
btusb           -        legacy     -     legacy  Bluetooth USB transport (class 0xe0)
uvc             -        legacy     -     legacy  USB video class (UVC camera)
placeholder     -        legacy     -     legacy  NAK registration-mechanism test driver (collected, never probed)
ath11k          -        legacy     -     legacy  Atheros ath11k (PCIe family)
ath10k          -        legacy     -     legacy  Atheros ath10k (PCIe family)
ath9k           -        legacy     -     legacy  Atheros ath9k (PCI family)
bcm57xx         -        legacy     -     legacy  Broadcom 57xx (tg3)
brcmfmac-sdio   -        legacy     -     legacy  brcmfmac SDIO client driver
brcmfmac        -        legacy     -     legacy  Broadcom brcmfmac (FullMAC PCI/SDIO family)
b43             -        legacy     -     legacy  Broadcom b43 (PCI family)
e1000e          -        legacy     -     legacy  Intel 82574 E1000E
igb             -        legacy     -     legacy  Intel I210/I350 (igb)
iwlwifi         -        legacy     -     legacy  Intel Wireless (iwlwifi PCI family)
ixgbe           -        legacy     -     legacy  Intel 82599 (ixgbe)
other           -        legacy     -     legacy  other/virtual NICs (netfilter, loopback-bridge)
rtl8139         -        legacy     -     legacy  Realtek 8139
rtl810x         -        legacy     -     legacy  Realtek 810x Fast
rtl8125         -        legacy     -     legacy  Realtek 8125 2.5GbE
rtl8168         -        legacy     -     legacy  Realtek 8168/8111
rtlwifi         -        legacy     -     legacy  Realtek wireless (rtlwifi PCI/USB family)
ahci            -        legacy     -     legacy  SATA AHCI
ata             -        legacy     -     legacy  ATA/ATAPI PIO (legacy ISA IDE)
ata_dma         -        legacy     -     legacy  ATA Bus-Master DMA (PCI IDE)
nvme            -        legacy     -     legacy  NVMe SSD
scsi            -        legacy     -     legacy  SCSI disk (virtio-scsi passthrough)
virtio_blk      -        legacy     -     legacy  virtio-blk storage
usb-audio       -        legacy     -     legacy  USB audio class (UAC hot-plug)
usbaudio        -        legacy     -     legacy  USB audio class
hid-mouse       -        legacy     -     legacy  USB HID boot-protocol mouse
hid-kbd         -        legacy     -     legacy  USB HID boot-protocol keyboard
usb-msc         -        legacy     -     legacy  USB mass-storage BOT (SCSI passthrough)
ftdi-serial     -        legacy     -     legacy  FTDI USB-serial adapter (vendor class)
cdc-acm         -        legacy     -     legacy  USB CDC-ACM serial adapter
ush> 
```

### drv_order

| 字段 | 值 |
|---|---|
| 名称 | drv_order |
| 分类 | 系统工具 |
| 功能 | 显示确定性初始化顺序 |

**实际执行示例**：

```
drv_order
metadata driver init order (topological, deterministic:
dependencies first, then priority hint, then name - never link
order):
  1. vtest-bus
  2. vtest-ctl  <- vtest-bus
  3. vtest-fail
  4. vtest-late  <- vtest-ctl
  5. vtest-mon
cycle detection: 2 driver(s) isolated:
  vtest-cyc-b: dependency cycle (reported, never looped)
  vtest-cyc-a: dependency cycle (reported, never looped)
legacy OC_DRIVER_DECL drivers: 38 (initialized by the kmain family-manager sequence - outside
this computation by design)
ush> 
```

### drv_reload

| 字段 | 值 |
|---|---|
| 名称 | drv_reload |
| 分类 | 系统工具 |
| 功能 | 重载驱动（调试） |

**实际执行示例**：

```
drv_reload
drv: missing argument for 'reload'
usage: drv reload <drv>
ush> 
```

### drv_unbind

| 字段 | 值 |
|---|---|
| 名称 | drv_unbind |
| 分类 | 系统工具 |
| 功能 | 手动解绑驱动（调试） |

**实际执行示例**：

```
drv_unbind
drv: missing argument for 'unbind'
usage: drv unbind <drv> <dev>
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
free
              total        used        free
Mem:     536739840 10256384 526483456
Swap:            0           0            0
ush> 
```

### health

| 字段 | 值 |
|---|---|
| 名称 | health |
| 分类 | 系统工具 |
| 功能 | 系统健康快照/监视 |
| 用法 | `health [watch|export [path]]` |
| 返回码 | 0, 1 |

**实际执行示例**：

```
health
system health:
  cpu:
  busy                100 % (1 s window)
  memory:
  total               524160 KiB
  used                11056 KiB
  free                513104 KiB
  tasks:
  total               4
  running             2
  ready               1
  blocked             1
  disk:
  read                0 bytes
  written             0 bytes
  network:
  tx                  0 bytes
  rx                  0 bytes
  alarms: none
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
Registry: 300 programs - 'help -a' lists them all, 'help -w' groups by work package.

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
help_a_test
help_a_test: 'help -a' == default A-Z view, ordering verified: PASS
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
help_default_test
help_default_test: A-Z ordering of the full command registry: PASS
help_default_test: run 'help' to see the full A-Z listing
ush> 
```

### help_wp_test

| 字段 | 值 |
|---|---|
| 名称 | help_wp_test |
| 分类 | 系统工具 |
| 功能 | 注册表工作包分组测试 |

**实际执行示例**：

```
help_wp_test
help_wp_test: registry flat A-Z ordering (one group == all): PASS
help_wp_test: global listing still sorted: PASS
help_wp_test: run 'help' to see the flat A-Z listing
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
kill
usage: kill <pid>
ush> 
```

### p2test

| 字段 | 值 |
|---|---|
| 名称 | p2test |
| 分类 | 系统工具 |
| 功能 | WP-fix-p2-p3-cleanup 实机探针（UTF-8 分块 / 零长读 / 管道） |
| 用法 | `p2test <utf8|read0|pipe>` |
| 返回码 | 0, 1 |

**实际执行示例**：

```
p2test
usage: p2test <utf8|read0|pipe>
ush> 
```

(备注: retry OK; plain retry)

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
0    RUN   31    240          idle
1    READY 31    18          usb-poll
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
dev
dev_debug
dev_help
dev_info
dev_list
dev_tree
df
dhcp
dhtest
disk-tool
disk_rw_test
dlsym_test
dmesg
dns
dnstest
drv
drv_bind
drv_debug
drv_failed
drv_help
drv_info
drv_list
drv_order
drv_reload
drv_unbind
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
-- 300 programs registered --
ush> 
```

### sed

| 字段 | 值 |
|---|---|
| 名称 | sed |
| 分类 | 系统工具 |
| 功能 | 流编辑器 |
| 用法 | `sed s/old/new/[g] [file] | sed -n Np [file] | sed Nd [file]` |
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
top - up 5270 ms
Mem: 10928128 used / 536739840 total / 525811712 free
TID  STATE  PRI  CPU(ticks)  NAME
0    RUN   31    244          idle
1    READY 31    4          usb-poll
2    BLOCK 15    101          ush
3    RUN   15    3          ush
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
