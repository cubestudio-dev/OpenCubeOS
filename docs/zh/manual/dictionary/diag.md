<!-- SPDX-License-Identifier: Apache-2.0 -->
# 应用字典 - 诊断 (39)

### acpi

| 字段 | 值 |
|---|---|
| 名称 | acpi |
| 分类 | 诊断 |
| 功能 | ACPI 表摘要 |
| 返回码 | 0, 1 |
| 来源 | `ocos/services/diag/src/svc_diag_l1.c` |

**实际执行示例**：

```
acpi
RSDP: 0x00050178 rev 0 OEM BOCHS 
tables: 5
  RSDT rev=1 len=52 @0x1ffe232f
  FACP rev=1 len=116 @0x1ffe21e3
  APIC rev=3 len=120 @0x1ffe2257
  HPET rev=1 len=56 @0x1ffe22cf
  WAET rev=1 len=40 @0x1ffe2307
FADT: rev 1 SCI=IRQ9 PM1a_CNT=0x0604 GPE0=0xafe0 GPE0len=4
DSDT @0x1ffe0040 SSDTs=0 FACS @0x1ffe0000
MCFG: not present (legacy CF8/CFC only)
ush> 
```

### acpidev

| 字段 | 值 |
|---|---|
| 名称 | acpidev |
| 分类 | 诊断 |
| 功能 | ACPI 设备树 |
| 返回码 | 0, 1 |
| 来源 | `ocos/services/diag/src/svc_diag_l1.c` |

**实际执行示例**：

```
acpidev
AML namespace: nodes=342 methods=102 devices=54
  \_SB_.PCI0 HID=PNP0a03
  \_SB_.PCI0.PRES HID=PNP0a06
  \_SB_.PCI0.GPE0 HID=PNP0A06 STA=0x0b
  \_SB_.PCI0.PHPR HID=PNP0A06 STA=0x0b
  \_SB_.PCI0.FWCF HID=QEMU0002 STA=0x0b
  \_SB_.PCI0.S00_
  \_SB_.PCI0.S08_ ADR=0x10000
  \_SB_.PCI0.S08_.KBD_ HID=PNP0303 STA=0x0f
  \_SB_.PCI0.S08_.MOU_ HID=PNP0f13 STA=0x0f
  \_SB_.PCI0.S08_.FDC0 HID=PNP0700
  \_SB_.PCI0.S08_.FDC0.FLPA
  \_SB_.PCI0.S08_.LPT1 HID=PNP0400 STA=0x0f
  \_SB_.PCI0.S08_.COM1 HID=PNP0501 STA=0x0f
  \_SB_.PCI0.S08_.RTC_ HID=PNP0b00
  \_SB_.PCI0.S09_ ADR=0x10001
  \_SB_.PCI0.S0B_ ADR=0x10003
  \_SB_.PCI0.S10_ ADR=0x20000
  \_SB_.PCI0.S18_ ADR=0x30000
  \_SB_.PCI0.S20_ ADR=0x40000
  \_SB_.PCI0.S28_ ADR=0x50000
  \_SB_.PCI0.S30_ ADR=0x60000
  \_SB_.PCI0.S38_ ADR=0x70000
  \_SB_.PCI0.S40_ ADR=0x80000
  \_SB_.PCI0.S48_ ADR=0x90000
  \_SB_.PCI0.S50_ ADR=0xa0000
  \_SB_.PCI0.S58_ ADR=0xb0000
  \_SB_.PCI0.S60_ ADR=0xc0000
  \_SB_.PCI0.S68_ ADR=0xd0000
  \_SB_.PCI0.S70_ ADR=0xe0000
  \_SB_.PCI0.S78_ ADR=0xf0000
  \_SB_.PCI0.S80_ ADR=0x100000
  \_SB_.PCI0.S88_ ADR=0x110000
  ... (truncated at 32)
D
ush> 
```

### acpipow

| 字段 | 值 |
|---|---|
| 名称 | acpipow |
| 分类 | 诊断 |
| 功能 | ACPI 电源状态 |
| 返回码 | 0, 1 |
| 来源 | `ocos/services/diag/src/svc_diag_l1.c` |

**实际执行示例**：

```
acpipow
sleep states offered: 0x38 (S1..S5 bit i-1)
  S1: not offered
  S2: not offered
  S3: SLP_TYP=0x01
  S4: SLP_TYP=0x02
  S5: SLP_TYP=0x00
SCI: IRQ 9  events: sci=0 gpe=0 pwrbtn=0
FACS: waking_vector=0x00000000 x_waking=0x00000000
resume: no S3 wake yet this boot
ush> 
```

### acpitest

| 字段 | 值 |
|---|---|
| 名称 | acpitest |
| 分类 | 诊断 |
| 功能 | ACPI 解析测试 |
| 来源 | `ocos/services/diag/src/svc_diag_l1.c` |

**实际执行示例**：

```
acpitest
acpitest: AML interpreter self-test
  fixture table eval:        PASS
  \_S5 SLP_TYP resolved: 0x00      PASS
  namespace device walk: 54        PASS
acpitest: PASS
ush> 
```

### cr3test

| 字段 | 值 |
|---|---|
| 名称 | cr3test |
| 分类 | 诊断 |
| 功能 | CR3 寄存器审计 |
| 返回码 | 0, 1 |
| 来源 | `ocos/services/diag/src/svc_diag_l1.c` |

**实际执行示例**：

```
cr3test
CR3 switch test:
  current CR3=0x0000003d2000
  new AS CR3=0x000000767000
  map at 4GiB: OK
  switched CR3 to 0x000000767000
  verify mapped in new AS: OK phys=0x000000769000
  switched back to 0x0000003d2000
  PASS
ush> 
```

### crashlog

| 字段 | 值 |
|---|---|
| 名称 | crashlog |
| 分类 | 诊断 |
| 功能 | 崩溃转储记录 |
| 用法 | `crashlog [flush]` |
| 返回码 | 0, 1 |
| 来源 | `ocos/services/diag/src/svc_diag_l1.c` |

**实际执行示例**：

```
crashlog
crashlog: no exceptions recorded
ush> 
```

### crashtest

| 字段 | 值 |
|---|---|
| 名称 | crashtest |
| 分类 | 诊断 |
| 功能 | 崩溃处理测试触发器 |
| 用法 | `crashtest panic\|exc\|...\|policy ...; crashtest panic\|exc\|pf\|assert\|wd\|ovf\|dump\|policy [halt\|reboot]` |
| 隐藏参数 | 隐藏参数，非公开接口，兼容性不保证: `0`, `1`, `help` |
| 返回码 | 0, 1 |
| 来源 | `ocos/services/diag/src/svc_diag_l1.c` |

**实际执行示例**：

```
crashtest
usage: crashtest panic|exc|pf|assert|wd|ovf|dump|policy [halt|reboot]
  panic   : kernel_panic() direct
  exc     : ring-0 #UD exception
  pf      : ring-0 unmapped-address write fault
  assert  : OC_ASSERT(0)
  wd      : forced watchdog timeout
  ovf     : kernel stack overflow (guard page)
  dump    : capture a core dump without panicking
  policy  : show/set the after-panic policy
ush> 
```

### dumpinfo

| 字段 | 值 |
|---|---|
| 名称 | dumpinfo |
| 分类 | 诊断 |
| 功能 | 核心转储状态 |
| 用法 | `dumpinfo   : core dump status` |
| 返回码 | 0 |
| 来源 | `ocos/services/diag/src/svc_diag_l1.c` |

**实际执行示例**：

```
dumpinfo
core dump status:
  no dump captured yet (trigger: crashtest panic|exc|pf)
policy: HALT  watchdog: off (timeout 15000 ms)

Block devices:
drivers: ATA-DMA drives=0  AHCI drives=0  NVMe queues=0
ush> 
```

### exc

| 字段 | 值 |
|---|---|
| 名称 | exc |
| 分类 | 诊断 |
| 功能 | 异常报告状态 |
| 返回码 | 0 |
| 来源 | `ocos/services/diag/src/svc_diag_l1.c` |

**实际执行示例**：

```
es1370_test
[es1370_test] device not present in this environment => SKIPPED (hardware absent)
ush> 
```

### frag

| 字段 | 值 |
|---|---|
| 名称 | frag |
| 分类 | 诊断 |
| 功能 | PMM 碎片报告 |
| 用法 | `frag       : PMM fragments` |
| 返回码 | 0 |
| 来源 | `ocos/services/diag/src/svc_diag_l1.c` |

**实际执行示例**：

```
font
registered fonts:
  ascii-8x16     built-in screen_font8x16, 8x16
  gb2312-16x16   embedded GB2312, 6763+682 glyphs
  total live: 2
  active renderer: cn-8x16-16x16
ush> 
```

### fstest

| 字段 | 值 |
|---|---|
| 名称 | fstest |
| 分类 | 诊断 |
| 功能 | VFS 压力测试 |
| 用法 | `G10 range (chipset) - reserved, bodies land with the group` |
| 返回码 | 0 |
| 来源 | `ocos/services/diag/src/svc_diag_l1.c` |

**实际执行示例**：

```
free
              total        used        free
Mem:     536739840 11706368 525033472
Swap:            0           0            0
ush> 
```

### heap

| 字段 | 值 |
|---|---|
| 名称 | heap |
| 分类 | 诊断 |
| 功能 | 内核堆统计 |
| 用法 | `heap       : kernel heap stats` |
| 返回码 | 0 |
| 来源 | `ocos/services/diag/src/svc_diag_l1.c` |

**实际执行示例**：

```
hda_test
[hda_test] device not present in this environment => SKIPPED (hardware absent)
ush> 
```

### heapbounds

| 字段 | 值 |
|---|---|
| 名称 | heapbounds |
| 分类 | 诊断 |
| 功能 | 堆边界检查 |
| 返回码 | 0, 1 |
| 来源 | `ocos/services/diag/src/svc_diag_l1.c` |

**实际执行示例**：

```
head
ush> 
```

### heaptest

| 字段 | 值 |
|---|---|
| 名称 | heaptest |
| 分类 | 诊断 |
| 功能 | 堆压力测试 |
| 返回码 | 0 |
| 来源 | `ocos/services/diag/src/svc_diag_l1.c` |

**实际执行示例**：

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

### i2cdetect

| 字段 | 值 |
|---|---|
| 名称 | i2cdetect |
| 分类 | 诊断 |
| 功能 | 扫描 I2C 总线 |
| 返回码 | 0, 1 |
| 来源 | `ocos/services/diag/src/svc_diag_l1.c` |

**实际执行示例**：

```
history
1 grep
2 grub-install
3 halt_test
4 hda
5 hda_test
6 head
7 health
8 heap
9 heapbounds
10 heaptest
11 hello
12 help
13 help_a_test
14 help_default_test
15 help_wp_test
16 history
ush> 
```

### i2cget

| 字段 | 值 |
|---|---|
| 名称 | i2cget |
| 分类 | 诊断 |
| 功能 | 读取 I2C 寄存器 |
| 用法 | `; i2cget <bus> <addr> [reg]  (addr/reg in hex, 0x..` |
| 参数 | `bus`, `addr` |
| 返回码 | 0, 1 |
| 来源 | `ocos/services/diag/src/svc_diag_l1.c` |

**实际执行示例**：

```
hpet
HPET: base=fed00000 (ACPI table, GAS SystemMemory)
Table: block_id=8086a201 rev=1 comparators=163 counter64=0 legacy=1
Caps: rev=1 timers=3 64bit=1 legacy=1 period=10000000fs freq=100000000Hz
Main counter: 0000000000000000
IRQ count (legacy-route tests): 0
ush> 
```

### i2cset

| 字段 | 值 |
|---|---|
| 名称 | i2cset |
| 分类 | 诊断 |
| 功能 | 写入 I2C 寄存器 |
| 用法 | `; i2cset <bus> <addr> <reg> <val>  (hex values` |
| 参数 | `bus`, `addr`, `reg`, `val` |
| 返回码 | 0, 1 |
| 来源 | `ocos/services/diag/src/svc_diag_l1.c` |

**实际执行示例**：

```
hwinfo nope
hwinfo: unknown op: nope
usage: hwinfo            (interactive menu)
       hwinfo direct <op> [args]   (explicit passthrough)
       hwinfo <op> [args]          (classic op dispatch)
ops:  uname
  stats
  font
  encoding
  chinese
  rtc
  hpet
  pmtimer
  tsc
  lspci
  pcidump
  pcibars
  pcie
  acpi
  acpidev
  acpipow
  i2cdetect
  smbus
  usb
  usbdev
  uvc
  scsi
  gameport
  parallel
  ir
  touchpad
ush> 
```

### i2ctest

| 字段 | 值 |
|---|---|
| 名称 | i2ctest |
| 分类 | 诊断 |
| 功能 | I2C 总线测试 |
| 隐藏参数 | 隐藏参数，非公开接口，兼容性不保证: `dw-i2c` |
| 来源 | `ocos/services/diag/src/svc_diag_l1.c` |

**实际执行示例**：

```
i2cdetect
i2c-0 (piix4): -- -- -- -- -- -- -- -- -- -- -- -- -- 
  -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
  -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
  -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
  -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
  50 51 52 53 54 55 56 57 -- -- -- -- -- -- -- -- 
  -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
  -- -- -- -- -- -- -- -- 
cÒ¸H
ush> 
```

### irqabitest

| 字段 | 值 |
|---|---|
| 名称 | irqabitest |
| 分类 | 诊断 |
| 功能 | IRQ 亲和性测试 |
| 返回码 | 0, 1 |
| 来源 | `ocos/services/diag/src/svc_diag_l1.c` |

**实际执行示例**：

```
int3_user
int3_user: about to int3

*** EXCEPTION 3: #BP Breakpoint ***
  error_code=0x0000000000000000  rip=0x0000000000400019  cs=0x0000000000000033  rflags=0x0000000000010202
  rsp=0x000000003FFFFEE0  ss=0x000000000000003B
*** KILLING user process (pid=67) due to exception 3, kernel continues ***
[user] process killed by exception (vector=3), kernel continues
*** IST-exit: vector=3 exit-stack rsp=0x00000000003E1FD0 [off-IST2 OK]
ush> 
```

### mem

| 字段 | 值 |
|---|---|
| 名称 | mem |
| 分类 | 诊断 |
| 功能 | 物理内存统计 |
| 用法 | `mem        : PMM stats` |
| 返回码 | 0 |
| 来源 | `ocos/services/diag/src/svc_diag_l1.c` |

**实际执行示例**：

```
mem
Physical memory:
  total: 536739840 bytes (131040 pages)
  used:  10371072 bytes (2532 pages)
  free:  526368768 bytes (128508 pages)
  fragments: 4
  allocs: 821  frees: 392  failures: 0
ush> 
```

### memtest

| 字段 | 值 |
|---|---|
| 名称 | memtest |
| 分类 | 诊断 |
| 功能 | 内存服务测试组 |
| 返回码 | 0 |
| 来源 | `ocos/services/diag/src/svc_diag_l1.c` |

**实际执行示例**：

```
memtest
PMM test:
  alloc 10 pages: OK
  free 10 pages: OK
Heap test:
  kmalloc 5 blocks: OK
  krealloc to 512: OK
  kfree all: OK
  double-free detection: OK (no crash)
  PASS
ush> 
```

### msitest

| 字段 | 值 |
|---|---|
| 名称 | msitest |
| 分类 | 诊断 |
| 功能 | MSI 中断测试 |
| 来源 | `ocos/services/diag/src/svc_diag_l1.c` |

**实际执行示例**：

```
msitest
LAPIC: present, id=0
  self-IPI vector 0x50: fired 1 time(s)  PASS
MSI-capable devices: 0 of 5
  e1000 not present: MSI device-fire skipped
msitest: PASS
ush> 
```

### multi

| 字段 | 值 |
|---|---|
| 名称 | multi |
| 分类 | 诊断 |
| 功能 | 多核唤醒测试 |
| 返回码 | 0 |
| 来源 | `ocos/services/diag/src/svc_diag_l1.c` |

**实际执行示例**：

```
multi
Spawning 3 tasks...
3 tasks spawned (same priority). Output should interleave.
  [task 1] iter 0
  [task 2] iter 0
  [task 3] iter 0
  [task 1] iter 1
  [task 2] iter 1
  [task 3] iter 1
ush> 
```

### nice

| 字段 | 值 |
|---|---|
| 名称 | nice |
| 分类 | 诊断 |
| 功能 | 优先级演示 |
| 用法 | `; nice <tid> <prio> (0=highest..31=lowest` |
| 参数 | `tid`, `prio` |
| 返回码 | 0, 1 |
| 来源 | `ocos/services/diag/src/svc_diag_l1.c` |

**实际执行示例**：

```
nice
usage: nice <tid> <prio> (0=highest..31=lowest)
ush> 
```

### pcibars

| 字段 | 值 |
|---|---|
| 名称 | pcibars |
| 分类 | 诊断 |
| 功能 | PCI BAR 分配检查 |
| 返回码 | 0 |
| 来源 | `ocos/services/diag/src/svc_diag_l1.c` |

**实际执行示例**：

```
pcibars
PCI devices: 5
 00:00.0 vid=0x8086 did=0x1237 class=0x060000
 00:01.0 vid=0x8086 did=0x7000 class=0x060100
 00:01.1 vid=0x8086 did=0x7010 class=0x010180
   BAR4: base=0x0000c000 size=0x00000010 io
 00:01.3 vid=0x8086 did=0x7113 class=0x068000
 00:02.0 vid=0x1234 did=0x1111 class=0x030000
   BAR0: base=0xfd000000 size=0x01000000 mem prefetch
   BAR2: base=0xfebf0000 size=0x00001000 mem
ush> 
```

### pcidump

| 字段 | 值 |
|---|---|
| 名称 | pcidump |
| 分类 | 诊断 |
| 功能 | 转储 PCI 配置 |
| 用法 | `; pcidump <bus:dev.fn>  e.g. pcidump 0:1f.3` |
| 返回码 | 0, 1 |
| 来源 | `ocos/services/diag/src/svc_diag_l1.c` |

**实际执行示例**：

```
pcidump
usage: pcidump <bus:dev.fn>  e.g. pcidump 0:1f.3
ush> 
```

### pcie

| 字段 | 值 |
|---|---|
| 名称 | pcie |
| 分类 | 诊断 |
| 功能 | PCIe 链路状态 |
| 返回码 | 0 |
| 来源 | `ocos/services/diag/src/svc_diag_l1.c` |

**实际执行示例**：

```
pcie
MMCONFIG: not active (MCFG absent) - legacy CF8/CFC only
capabilities per device:
  00:00.0 caps: (legacy)
  00:01.0 caps: (legacy)
  00:01.1 caps: (legacy)
  00:01.3 caps: (legacy)
  00:02.0 caps: (legacy)
ush> 
```

### pftest

| 字段 | 值 |
|---|---|
| 名称 | pftest |
| 分类 | 诊断 |
| 功能 | 缺页处理测试 |
| 返回码 | 0 |
| 来源 | `ocos/services/diag/src/svc_diag_l1.c` |

**实际执行示例**：

```
pftest
Page fault test:
  test 1: illegal-PF-on-high-address (counter test)...
    faults delta=1 legal_delta=1
    OK (L1 handler caught the high-address PF)
  test 2: illegal PF (unmapped address)...
    faults before=2 after=3
    legal=2 illegal=1 stack=0 heap=0
    L1 handler ran, page mapped, write verified, 1 legal PFs counted
  PASS
ush> 
```

### pmmrace

| 字段 | 值 |
|---|---|
| 名称 | pmmrace |
| 分类 | 诊断 |
| 功能 | PMM 竞态检测 |
| 返回码 | 1 |
| 来源 | `ocos/services/diag/src/svc_diag_l1.c` |

**实际执行示例**：

```
pmmrace
```

(备注: long-running race detector; the transcript is a 150 s excerpt of the run)

### sched

| 字段 | 值 |
|---|---|
| 名称 | sched |
| 分类 | 诊断 |
| 功能 | 调度器统计 |
| 用法 | `sched      : scheduler stats` |
| 返回码 | 0 |
| 来源 | `ocos/services/diag/src/svc_diag_l1.c` |

**实际执行示例**：

```
sched
switches=262 preemptions=120 current_tid=3 active_tasks=4
PED (no ISA SB16 (QEMU: add -device sb16,audiodev=snd0))
ush> 
```

### smbus

| 字段 | 值 |
|---|---|
| 名称 | smbus |
| 分类 | 诊断 |
| 功能 | SMBus 状态 |
| 返回码 | 0, 1 |
| 来源 | `ocos/services/diag/src/svc_diag_l1.c` |

**实际执行示例**：

```
smbus
SMBus devices: 8
  0x50  EEPROM  first bytes: 00 00 00 00 00 00 00 00 
  0x51  EEPROM  first bytes: 00 00 00 00 00 00 00 00 
  0x52  EEPROM  first bytes: 00 00 00 00 00 00 00 00 
  0x53  EEPROM  first bytes: 00 00 00 00 00 00 00 00 
  0x54  EEPROM  first bytes: 00 00 00 00 00 00 00 00 
  0x55  EEPROM  first bytes: 00 00 00 00 00 00 00 00 
  0x56  EEPROM  first bytes: 00 00 00 00 00 00 00 00 
  0x57  EEPROM  first bytes: 00 00 00 00 00 00 00 00 
ush> 
```

### smbustest

| 字段 | 值 |
|---|---|
| 名称 | smbustest |
| 分类 | 诊断 |
| 功能 | SMBus 测试 |
| 返回码 | 0, 1 |
| 来源 | `ocos/services/diag/src/svc_diag_l1.c` |

**实际执行示例**：

```
smbustest
smbustest: byte/word/block round-trips  PASS
ush> 
```

### spawn

| 字段 | 值 |
|---|---|
| 名称 | spawn |
| 分类 | 诊断 |
| 功能 | 后台派生演示 |
| 返回码 | 0 |
| 来源 | `ocos/services/diag/src/svc_diag_l1.c` |

**实际执行示例**：

```
spawn
spawned tid=4

  (none)
  [test thread] iteration 0
  [test thread] iteration 1
ush> 
```

### syncstat

| 字段 | 值 |
|---|---|
| 名称 | syncstat |
| 分类 | 诊断 |
| 功能 | 同步原语统计 |
| 用法 | `syncstat   : sync primitive stats` |
| 返回码 | 0 |
| 来源 | `ocos/services/diag/src/svc_diag_l1.c` |

**实际执行示例**：

```
syncstat
spin_locks=8915322 spin_unlocks=8915322
sem_waits=0 sem_posts=0
mutex_locks=0 mutex_unlocks=0
cond_waits=0 cond_signals=0
ush> 
```

### synctest

| 字段 | 值 |
|---|---|
| 名称 | synctest |
| 分类 | 诊断 |
| 功能 | 同步原语测试 |
| 返回码 | 0 |
| 来源 | `ocos/services/diag/src/svc_diag_l1.c` |

**实际执行示例**：

```
synctest
=== Spinlock test (3 tasks x 1000) ===
  counter=3000 (expected 3000) PASS
=== Mutex test (3 tasks x 1000) ===
  counter=3000 (expected 3000) PASS
=== Semaphore test (3 tasks x 1000) ===
  counter=3000 (expected 3000) PASS
ush> 
```

### timer

| 字段 | 值 |
|---|---|
| 名称 | timer |
| 分类 | 诊断 |
| 功能 | 定时器测试 |
| 返回码 | 0 |
| 来源 | `ocos/services/diag/src/svc_diag_l1.c` |

**实际执行示例**：

```
timer
Registered 500ms one-shot timer
arser=ok synrcvd-recycle=ok sack-map=ok; live negotiation covered by HTTPS E2E)
ush> 
```

### vmkernelpt

| 字段 | 值 |
|---|---|
| 名称 | vmkernelpt |
| 分类 | 诊断 |
| 功能 | 内核页表转储 |
| 返回码 | 0, 1 |
| 来源 | `ocos/services/diag/src/svc_diag_l1.c` |

**实际执行示例**：

```
vmkernelpt
VMM kernel-PT test (BUG-0136 PT_OWNER):
  user AS created (copies split PD0)
  user AS destroyed
  pmm failures=0
  PASS
ush> 
```

### vmmap

| 字段 | 值 |
|---|---|
| 名称 | vmmap |
| 分类 | 诊断 |
| 功能 | 地址空间映射转储 |
| 返回码 | 0 |
| 来源 | `ocos/services/diag/src/svc_diag_l1.c` |

**实际执行示例**：

```
vmmap
Kernel address space (PML4 at 0x0000003d2000):
  PML4[0] vaddr=0x000000000000 -> 0x0000003d3000

Page faults: total=0 legal=0 illegal=0 stack=0 heap=0
T@
ush> 
```

### vmtest

| 字段 | 值 |
|---|---|
| 名称 | vmtest |
| 分类 | 诊断 |
| 功能 | VMM 单元测试 |
| 返回码 | 0, 1 |
| 来源 | `ocos/services/diag/src/svc_diag_l1.c` |

**实际执行示例**：

```
vmtest
VMM test:
  created AS at 0x000000782000
  map_page(0x000100000000 -> 0x000000784000) = 0
  is_mapped = 1 phys=0x000000784000
  protect(read-only) = 0
  unmap returned 0x000000784005
  is_mapped after unmap = 0
  destroyed AS
  PASS

ush> 
```
