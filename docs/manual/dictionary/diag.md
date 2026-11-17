<!-- SPDX-License-Identifier: Apache-2.0 -->
# Application dictionary - Diagnostics (39)

### acpi

| Field | Value |
|---|---|
| Name | acpi |
| Category | Diagnostics |
| Function | ACPI table summary |
| Return codes | 0, 1 |
| Source | `ocos/services/diag/src/svc_diag_l1.c` |

**Executed example**:

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

| Field | Value |
|---|---|
| Name | acpidev |
| Category | Diagnostics |
| Function | ACPI device tree |
| Return codes | 0, 1 |
| Source | `ocos/services/diag/src/svc_diag_l1.c` |

**Executed example**:

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

| Field | Value |
|---|---|
| Name | acpipow |
| Category | Diagnostics |
| Function | ACPI power states |
| Return codes | 0, 1 |
| Source | `ocos/services/diag/src/svc_diag_l1.c` |

**Executed example**:

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

| Field | Value |
|---|---|
| Name | acpitest |
| Category | Diagnostics |
| Function | ACPI parser test |
| Source | `ocos/services/diag/src/svc_diag_l1.c` |

**Executed example**:

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

| Field | Value |
|---|---|
| Name | cr3test |
| Category | Diagnostics |
| Function | CR3 register audit |
| Return codes | 0, 1 |
| Source | `ocos/services/diag/src/svc_diag_l1.c` |

**Executed example**:

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

| Field | Value |
|---|---|
| Name | crashlog |
| Category | Diagnostics |
| Function | crash dump records |
| Usage | `crashlog [flush]` |
| Return codes | 0, 1 |
| Source | `ocos/services/diag/src/svc_diag_l1.c` |

**Executed example**:

```
crashlog
crashlog: no exceptions recorded
ush> 
```

### crashtest

| Field | Value |
|---|---|
| Name | crashtest |
| Category | Diagnostics |
| Function | crash handling test triggers |
| Usage | `crashtest panic\|exc\|...\|policy ...; crashtest panic\|exc\|pf\|assert\|wd\|ovf\|dump\|policy [halt\|reboot]` |
| Hidden parameters | hidden parameter, not a public interface; compatibility is not guaranteed: `0`, `1`, `help` |
| Return codes | 0, 1 |
| Source | `ocos/services/diag/src/svc_diag_l1.c` |

**Executed example**:

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

| Field | Value |
|---|---|
| Name | dumpinfo |
| Category | Diagnostics |
| Function | core dump status |
| Usage | `dumpinfo   : core dump status` |
| Return codes | 0 |
| Source | `ocos/services/diag/src/svc_diag_l1.c` |

**Executed example**:

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

| Field | Value |
|---|---|
| Name | exc |
| Category | Diagnostics |
| Function | exception report status |
| Return codes | 0 |
| Source | `ocos/services/diag/src/svc_diag_l1.c` |

**Executed example**:

```
exc
Running exception self-test (#DE, #UD, #PF)...
passed 3 exceptions, kernel alive
ush> 
```

### frag

| Field | Value |
|---|---|
| Name | frag |
| Category | Diagnostics |
| Function | PMM fragment report |
| Usage | `frag       : PMM fragments` |
| Return codes | 0 |
| Source | `ocos/services/diag/src/svc_diag_l1.c` |

**Executed example**:

```
frag
PMM: free_pages=128363 fragments=6 avg_frag=21393
Heap: free_blocks=62 free_bytes=554880
penCube shi)

ush> 
```

### fstest

| Field | Value |
|---|---|
| Name | fstest |
| Category | Diagnostics |
| Function | VFS stress test |
| Usage | `G10 range (chipset) - reserved, bodies land with the group` |
| Return codes | 0 |
| Source | `ocos/services/diag/src/svc_diag_l1.c` |

**Executed example**:

```
fstest
vfs: cannot unmount the root filesystem
VFS self-test:
  mkdir /tmp/vfstest: OK
  open(O_CREAT): OK
  write: OK
  read: OK -> "hello from ramfs"
  ls /tmp/vfstest:
    hello.txt
  ramfs: 13 nodes, 3740 bytes
  p2 regressions:
    BUG-0178 dual-sided: PASS
    BUG-0179 dual-sided: PASS
    BUG-0180 hole is zeros: PASS
    BUG-0182 dual-sided: PASS
    BUG-0184 umount / refused: PASS
    p2 regressions: ALL PASS
  p1fix4 regressions:
    BUG-0061 dual-sided: PASS
    p1fix4 regressions: ALL PASS
ush> 
```

### heap

| Field | Value |
|---|---|
| Name | heap |
| Category | Diagnostics |
| Function | kernel heap statistics |
| Usage | `heap       : kernel heap stats` |
| Return codes | 0 |
| Source | `ocos/services/diag/src/svc_diag_l1.c` |

**Executed example**:

```
heap
Kernel heap:
  size:  3477504 bytes
  alloc: 1551568 bytes (132 blocks)
  free:  1915440 bytes (196 blocks)
  overhead: 10496 bytes
  total allocs: 350  total frees: 218

ush> 
```

### heapbounds

| Field | Value |
|---|---|
| Name | heapbounds |
| Category | Diagnostics |
| Function | heap boundary check |
| Return codes | 0, 1 |
| Source | `ocos/services/diag/src/svc_diag_l1.c` |

**Executed example**:

```
heapbounds
Heap bounds test (BUG-0140 wrap / BUG-0141 ceiling):
  PASS
ush> 
```

### heaptest

| Field | Value |
|---|---|
| Name | heaptest |
| Category | Diagnostics |
| Function | heap stress test |
| Return codes | 0 |
| Source | `ocos/services/diag/src/svc_diag_l1.c` |

**Executed example**:

```
heaptest
Heap overhead test (100 allocs):
  heap_size=3657728 allocated=1583392 overhead=14176
  requested=26542 waste=1556850 overhead%=0
  after free: alloc=134 free=216 total_allocs=462 total_frees=328
  PASS
ush> 
```

### i2cdetect

| Field | Value |
|---|---|
| Name | i2cdetect |
| Category | Diagnostics |
| Function | scan the I2C bus |
| Return codes | 0, 1 |
| Source | `ocos/services/diag/src/svc_diag_l1.c` |

**Executed example**:

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
ush> 
```

### i2cget

| Field | Value |
|---|---|
| Name | i2cget |
| Category | Diagnostics |
| Function | read an I2C register |
| Usage | `; i2cget <bus> <addr> [reg]  (addr/reg in hex, 0x..` |
| Parameters | `bus`, `addr` |
| Return codes | 0, 1 |
| Source | `ocos/services/diag/src/svc_diag_l1.c` |

**Executed example**:

```
i2cget
usage: i2cget <bus> <addr> [reg]  (addr/reg in hex, 0x..)
ush> 
```

### i2cset

| Field | Value |
|---|---|
| Name | i2cset |
| Category | Diagnostics |
| Function | write an I2C register |
| Usage | `; i2cset <bus> <addr> <reg> <val>  (hex values` |
| Parameters | `bus`, `addr`, `reg`, `val` |
| Return codes | 0, 1 |
| Source | `ocos/services/diag/src/svc_diag_l1.c` |

**Executed example**:

```
i2cset
usage: i2cset <bus> <addr> <reg> <val>  (hex values)
ush> 
```

### i2ctest

| Field | Value |
|---|---|
| Name | i2ctest |
| Category | Diagnostics |
| Function | I2C bus test |
| Hidden parameters | hidden parameter, not a public interface; compatibility is not guaranteed: `dw-i2c` |
| Source | `ocos/services/diag/src/svc_diag_l1.c` |

**Executed example**:

```
i2ctest
I2C core: 1 bus(es) registered
controller: PIIX4 (8086:7113) base 0x0700 xfers=80 errs=202
  diag: cfg0x90=0x00000701 sts=0x00
DesignWare: not present on this platform (honest skip)
10-bit addressing: supported on DesignWare bus (none present)
i2ctest: PASS
ush> 
```

### irqabitest

| Field | Value |
|---|---|
| Name | irqabitest |
| Category | Diagnostics |
| Function | IRQ affinity test |
| Return codes | 0, 1 |
| Source | `ocos/services/diag/src/svc_diag_l1.c` |

**Executed example**:

```
irqabitest
IRQ-entry ABI test (BUG-0137 align / BUG-0138 DF):
  timer-IRQ bits=0x3  int0x80 bits=0x3
  PASS
probe runs against the secondary 16550 at boot)
ush> 
```

### mem

| Field | Value |
|---|---|
| Name | mem |
| Category | Diagnostics |
| Function | physical memory statistics |
| Usage | `mem        : PMM stats` |
| Return codes | 0 |
| Source | `ocos/services/diag/src/svc_diag_l1.c` |

**Executed example**:

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

| Field | Value |
|---|---|
| Name | memtest |
| Category | Diagnostics |
| Function | memory service test battery |
| Return codes | 0 |
| Source | `ocos/services/diag/src/svc_diag_l1.c` |

**Executed example**:

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

| Field | Value |
|---|---|
| Name | msitest |
| Category | Diagnostics |
| Function | MSI interrupt test |
| Source | `ocos/services/diag/src/svc_diag_l1.c` |

**Executed example**:

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

| Field | Value |
|---|---|
| Name | multi |
| Category | Diagnostics |
| Function | multi-core wake test |
| Return codes | 0 |
| Source | `ocos/services/diag/src/svc_diag_l1.c` |

**Executed example**:

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

| Field | Value |
|---|---|
| Name | nice |
| Category | Diagnostics |
| Function | priority demo |
| Usage | `; nice <tid> <prio> (0=highest..31=lowest` |
| Parameters | `tid`, `prio` |
| Return codes | 0, 1 |
| Source | `ocos/services/diag/src/svc_diag_l1.c` |

**Executed example**:

```
nice
usage: nice <tid> <prio> (0=highest..31=lowest)
ush> 
```

### pcibars

| Field | Value |
|---|---|
| Name | pcibars |
| Category | Diagnostics |
| Function | PCI BAR assignment check |
| Return codes | 0 |
| Source | `ocos/services/diag/src/svc_diag_l1.c` |

**Executed example**:

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

| Field | Value |
|---|---|
| Name | pcidump |
| Category | Diagnostics |
| Function | dump PCI configuration |
| Usage | `; pcidump <bus:dev.fn>  e.g. pcidump 0:1f.3` |
| Return codes | 0, 1 |
| Source | `ocos/services/diag/src/svc_diag_l1.c` |

**Executed example**:

```
pcidump
usage: pcidump <bus:dev.fn>  e.g. pcidump 0:1f.3
ush> 
```

### pcie

| Field | Value |
|---|---|
| Name | pcie |
| Category | Diagnostics |
| Function | PCIe link status |
| Return codes | 0 |
| Source | `ocos/services/diag/src/svc_diag_l1.c` |

**Executed example**:

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

| Field | Value |
|---|---|
| Name | pftest |
| Category | Diagnostics |
| Function | page-fault handler test |
| Return codes | 0 |
| Source | `ocos/services/diag/src/svc_diag_l1.c` |

**Executed example**:

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

| Field | Value |
|---|---|
| Name | pmmrace |
| Category | Diagnostics |
| Function | PMM race detector |
| Return codes | 1 |
| Source | `ocos/services/diag/src/svc_diag_l1.c` |

**Executed example**:

```
pmmrace
```

(Note: long-running race detector; the transcript is a 150 s excerpt of the run)

### sched

| Field | Value |
|---|---|
| Name | sched |
| Category | Diagnostics |
| Function | scheduler statistics |
| Usage | `sched      : scheduler stats` |
| Return codes | 0 |
| Source | `ocos/services/diag/src/svc_diag_l1.c` |

**Executed example**:

```
sched
switches=262 preemptions=120 current_tid=3 active_tasks=4
PED (no ISA SB16 (QEMU: add -device sb16,audiodev=snd0))
ush> 
```

### smbus

| Field | Value |
|---|---|
| Name | smbus |
| Category | Diagnostics |
| Function | SMBus status |
| Return codes | 0, 1 |
| Source | `ocos/services/diag/src/svc_diag_l1.c` |

**Executed example**:

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

| Field | Value |
|---|---|
| Name | smbustest |
| Category | Diagnostics |
| Function | SMBus test |
| Return codes | 0, 1 |
| Source | `ocos/services/diag/src/svc_diag_l1.c` |

**Executed example**:

```
smbustest
smbustest: byte/word/block round-trips  PASS
ush> 
```

### spawn

| Field | Value |
|---|---|
| Name | spawn |
| Category | Diagnostics |
| Function | background spawn demo |
| Return codes | 0 |
| Source | `ocos/services/diag/src/svc_diag_l1.c` |

**Executed example**:

```
spawn
spawned tid=4

  (none)
  [test thread] iteration 0
  [test thread] iteration 1
ush> 
```

### syncstat

| Field | Value |
|---|---|
| Name | syncstat |
| Category | Diagnostics |
| Function | sync primitive statistics |
| Usage | `syncstat   : sync primitive stats` |
| Return codes | 0 |
| Source | `ocos/services/diag/src/svc_diag_l1.c` |

**Executed example**:

```
syncstat
spin_locks=8915322 spin_unlocks=8915322
sem_waits=0 sem_posts=0
mutex_locks=0 mutex_unlocks=0
cond_waits=0 cond_signals=0
ush> 
```

### synctest

| Field | Value |
|---|---|
| Name | synctest |
| Category | Diagnostics |
| Function | sync primitive test |
| Return codes | 0 |
| Source | `ocos/services/diag/src/svc_diag_l1.c` |

**Executed example**:

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

| Field | Value |
|---|---|
| Name | timer |
| Category | Diagnostics |
| Function | timer test |
| Return codes | 0 |
| Source | `ocos/services/diag/src/svc_diag_l1.c` |

**Executed example**:

```
timer
Registered 500ms one-shot timer
arser=ok synrcvd-recycle=ok sack-map=ok; live negotiation covered by HTTPS E2E)
ush> 
```

### vmkernelpt

| Field | Value |
|---|---|
| Name | vmkernelpt |
| Category | Diagnostics |
| Function | kernel page-table dump |
| Return codes | 0, 1 |
| Source | `ocos/services/diag/src/svc_diag_l1.c` |

**Executed example**:

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

| Field | Value |
|---|---|
| Name | vmmap |
| Category | Diagnostics |
| Function | address-space map dump |
| Return codes | 0 |
| Source | `ocos/services/diag/src/svc_diag_l1.c` |

**Executed example**:

```
vmmap
Kernel address space (PML4 at 0x0000003d2000):
  PML4[0] vaddr=0x000000000000 -> 0x0000003d3000

Page faults: total=0 legal=0 illegal=0 stack=0 heap=0
T@
ush> 
```

### vmtest

| Field | Value |
|---|---|
| Name | vmtest |
| Category | Diagnostics |
| Function | VMM unit test |
| Return codes | 0, 1 |
| Source | `ocos/services/diag/src/svc_diag_l1.c` |

**Executed example**:

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
