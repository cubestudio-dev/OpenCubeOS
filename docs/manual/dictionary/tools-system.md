<!-- SPDX-License-Identifier: Apache-2.0 -->
# Application dictionary - System tools (18)

### date

| Field | Value |
|---|---|
| Name | date |
| Category | System tools |
| Function | show date/time from the RTC |
| Return codes | 0 |

**Executed example**:

```
date
Open Cube OS uptime: 3130 ms since boot (no RTC driver)
ush> 
```

### dmesg

| Field | Value |
|---|---|
| Name | dmesg |
| Category | System tools |
| Function | show the kernel log |
| Return codes | 0, 1 |

**Executed example**:

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

| Field | Value |
|---|---|
| Name | echo |
| Category | System tools |
| Function | print the arguments |
| Return codes | 0 |

**Executed example**:

```
echo hello dictionary
hello dictionary
ush> 
```

### free

| Field | Value |
|---|---|
| Name | free |
| Category | System tools |
| Function | memory usage summary |
| Return codes | 0 |

**Executed example**:

```
free
              total        used        free
Mem:     536739840 10969088 525770752
Swap:            0           0            0
ush> 
```

### health

| Field | Value |
|---|---|
| Name | health |
| Category | System tools |
| Function | system health snapshot/watch |
| Usage | `health [watch\|export [path]]` |
| Return codes | 0, 1 |

**Executed example**:

```
health
system health:
  cpu:
  busy                100 % (1 s window)
  memory:
  total               524160 KiB
  used                12544 KiB
  free                511616 KiB
  tasks:
  total               6
  running             2
  ready               1
  blocked             3
  disk:
  read                0 bytes
  written             0 bytes
  network:
  tx                  0 bytes
  rx                  0 bytes
  alarms: CPU-LOCKED 
ush> 
```

### help

| Field | Value |
|---|---|
| Name | help |
| Category | System tools |
| Function | show shell built-ins and the program registry |
| Hidden parameters | hidden parameter, not a public interface; compatibility is not guaranteed: `--all`, `--wp`, `-a`, `-w` |
| Return codes | 0, 1 |

**Executed example**:

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

### help_a_test

| Field | Value |
|---|---|
| Name | help_a_test |
| Category | System tools |
| Function | registry A-Z listing invariant test |

**Executed example**:

```
help_a_test
help_a_test: 'help -a' == default A-Z view, ordering verified: PASS
ush> 
```

### help_default_test

| Field | Value |
|---|---|
| Name | help_default_test |
| Category | System tools |
| Function | registry default listing test |
| Return codes | 0 |

**Executed example**:

```
help_default_test
help_default_test: A-Z ordering of the full command registry: PASS
help_default_test: run 'help' to see the full A-Z listing
ush> 
```

### help_wp_test

| Field | Value |
|---|---|
| Name | help_wp_test |
| Category | System tools |
| Function | registry work-package grouping test |

**Executed example**:

```
help_wp_test
help_wp_test: registry flat A-Z ordering (one group == all): PASS
help_wp_test: global listing still sorted: PASS
help_wp_test: run 'help' to see the flat A-Z listing
ush> 
```

### kill

| Field | Value |
|---|---|
| Name | kill |
| Category | System tools |
| Function | terminate a process |
| Usage | `kill <pid>` |
| Parameters | `pid` |
| Return codes | 0 |

**Executed example**:

```
kill
usage: kill <pid>
ush> 
```

### p2test

| Field | Value |
|---|---|
| Name | p2test |
| Category | System tools |
| Function | WP-fix-p2-p3-cleanup live probes (utf8 chunking / read0 / pipe) |
| Usage | `p2test <utf8\|read0\|pipe>` |
| Return codes | 0, 1 |

**Executed example**:

```
p2test
usage: p2test <utf8|read0|pipe>
ush> 
```

(Note: no-arguments usage)

### ps

| Field | Value |
|---|---|
| Name | ps |
| Category | System tools |
| Function | list processes |
| Return codes | 0 |

**Executed example**:

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

| Field | Value |
|---|---|
| Name | pwd |
| Category | System tools |
| Function | print the working directory |
| Return codes | 0 |

**Executed example**:

```
pwd
/
ush> 
```

### run

| Field | Value |
|---|---|
| Name | run |
| Category | System tools |
| Function | run a program with arguments |
| Usage | `run <program>   (registered programs below` |
| Parameters | `program` |
| Return codes | 0, 1 |

**Executed example**:

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

| Field | Value |
|---|---|
| Name | sed |
| Category | System tools |
| Function | stream editor |
| Usage | `sed s/old/new/[g] [file] \| sed -n Np [file] \| sed Nd [file]` |
| Return codes | 0 |

**Executed example**:

```
sed
usage: sed s/old/new/[g] [file] | sed -n Np [file] | sed Nd [file]
ush> 
```

### top

| Field | Value |
|---|---|
| Name | top |
| Category | System tools |
| Function | process overview (live) |
| Hidden parameters | hidden parameter, not a public interface; compatibility is not guaranteed: `-n` |
| Return codes | 0 |

**Executed example**:

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

(Note: full-screen page; the quit key was not honored in this session - output as captured, session reset)

### ush

| Field | Value |
|---|---|
| Name | ush |
| Category | System tools |
| Function | the Open Cube OS ring-3 shell (default entry) |
| Usage | `unalias name` |
| Hidden parameters | hidden parameter, not a public interface; compatibility is not guaranteed: `--all`, `--wp`, `-a`, `-w`, `bg`, `cd`, `env`, `exit`, `export`, `fg`, `help`, `history`, `jobs`, `set` |
| Return codes | -1, 0, 1, 2 |

**Executed example**:

```
ush

Open Cube OS User-space Shell (3.0.0-NAK)
Type 'help' for built-in commands.

ush> 
```

### write

| Field | Value |
|---|---|
| Name | write |
| Category | System tools |
| Function | write stdin to a file |
| Usage | `write <path> <text>` |
| Parameters | `path`, `text` |
| Return codes | 0 |

**Executed example**:

```
write
usage: write <path> <text>
ush> 
```
