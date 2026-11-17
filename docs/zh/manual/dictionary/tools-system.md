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
Mem:     536739840 10969088 525770752
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
Registry: 284 programs - 'help -a' lists them all, 'help -w' groups by work package.

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
top - up 9490 ms
Mem: 10342400 used / 536739840 total / 526397440 free
TID  STATE  PRI  CPU(ticks)  NAME
0    RUN   31    234          idle
1    READY 31    4          usb-poll
2    BLOCK 15    701          ush
3    RUN   15    2          ush
ush> 
```

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
