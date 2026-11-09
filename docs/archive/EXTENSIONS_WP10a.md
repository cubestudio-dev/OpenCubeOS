<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 cubestudio-dev <cubestudio@qq.com> -->

# WP-10a — Storage Drivers (AHCI / NVMe / ATA Bus-Master DMA / virtio-blk)

WP-10a gives Open Cube OS mainstream bare-metal storage: SATA disks/SSDs
through AHCI, NVMe SSDs, classic IDE drives through PCI Bus-Master DMA
(with the WP-05 PIO path kept as fallback), and virtio-blk under QEMU.
All four drivers register with the block-device layer (`drivers/block/driver_block_blk.h`),
so file systems and the partition parser work on every device type
without knowing which driver is underneath.

## 1. Drivers

### AHCI SATA (`drivers/block/driver_block_ahci.c`, `drivers/block/driver_block_ahci.h`)

- Enumerates AHCI controllers by EXACT PCI class `0x010601`
  (`driver_pci_find_class_exact`), up to 2 controllers.
- Programs GHC.AE (interrupts stay off — polled model), reads
  CAP.NP / CAP.PI / CAP.S64A / CAP.SNCQ, and walks every implemented
  port: stops the engine, programs PxCLB/PxFB (command list + FIS
  receive area), clears PxSERR, starts FRE+ST, and detects a device by
  `PxSSTS.DET == 3` + `PxSIG == 0x00000101` (ATA).
- IDENTIFY DEVICE via a Register H2D FIS; 48-bit capacity from words
  100/101.
- Reads/writes with READ/WRITE DMA EXT through a per-port 4 KiB
  page-aligned bounce buffer, split into chunks of at most 8 sectors
  (one PRDT entry per chunk). FLUSH CACHE EXT implements the driver
  flush hook.
- Registers every drive with the blk layer as `sda`, `sdb`, ...

### NVMe (`drivers/block/driver_block_nvme.c`, `drivers/block/driver_block_nvme.h`)

- Probes class `0x010802` (EXACT triple — the old base-class probe
  latched onto IDE controllers when one was present).
- BAR0 MMIO, CAP (MQES/DSTRD), CC.EN with IOSQES=6 / IOCQES=4, admin
  queue pair (AQA/ASQ/ACQ), Identify Controller (CNS=1) + Identify
  Namespace 1 (CNS=0) for capacity and LBADS.
- WP-10a brings up **two** fully working I/O queue pairs (qid=1,2,
  created CQ-before-SQ per spec) and round-robins I/O across them.
- I/O: Read (0x02) / Write (0x01) with 64-bit PRP, one page bounce
  buffer; Flush (0x0A) implements the driver flush hook.
- Registers `nvme0`.

### ATA Bus-Master DMA (`drivers/block/driver_block_ata_dma.c`, `drivers/block/driver_block_ata_dma.h`)

- Enumerates IDE controllers by class `0x0101` with ANY prog-if
  (`driver_pci_find_class_mask`, mask `0xFFFF00` — QEMU PIIX3 reports 0x80,
  real chips 0x00/0x8A/...).
- Programs the Bus-Master registers at BAR4: builds the Physical Region
  Descriptor Table (PRDT), sets the PRDT pointer, starts/stops via the
  BMR command register, and waits for the BMR status interrupt bit.
- Same drive numbering as the PIO driver (`hda`..`hdd`); drives the
  taskfile through the legacy PIO registers, then hands off to DMA.
- **PIO fallback preserved**: if a controller has no BMDMA BAR or a DMA
  transfer fails, the WP-05 PIO path in `drivers/block/driver_block_ata.c` still serves the
  drive. `driver_block_ata_dma_available(n)` reports which drives are DMA-backed.
- DMA vs PIO timing is printed by `ata_dma_test`.

### virtio-blk (`drivers/block/driver_block_virtio_blk.c`, `drivers/block/driver_block_virtio_blk.h`)

- Modern + legacy PCI probing, virtqueue setup, request/reply
  descriptors, capacity read (legacy config offset 0x14 — this fixes
  the old "capacity = 0" bug), read/write/flush through the blk layer.
- Registers `vda`.

## 2. L1 extension interfaces added by WP-10a

All declarations live in the headers below with full doc comments;
default implementations are compiled into L0 and exercised by the
`*_test` shell commands.

```c
/* drivers/block/driver_block_blk.h — pointer-based block-device API */
int driver_block_register(driver_block_device_t *dev, const driver_block_ops_t *ops);
int driver_block_read (driver_block_device_t *dev, u64 lba, u32 count, void *buf);
int driver_block_write(driver_block_device_t *dev, u64 lba, u32 count, const void *buf);
int driver_block_flush(driver_block_device_t *dev);
int driver_block_set_ops(int dev_idx, const driver_block_ops_t *ops);   /* PIO→DMA upgrade */

/* drivers/pci/driver_pci.h — class-based enumeration */
int driver_pci_find_class_exact(u32 class_code, int nth, u8 *bus, u8 *dev, u8 *func);
int driver_pci_find_class_mask (u32 class_code, u32 mask, int nth,
                         u8 *bus, u8 *dev, u8 *func);
typedef struct driver_pci_dev { u8 bus, dev, func; } driver_pci_dev_t;

/* drivers/block/driver_block_ahci.h / drivers/block/driver_block_nvme.h / drivers/block/driver_block_ata_dma.h — driver init.
 * pdev == NULL enumerates the PCI bus (boot path); pdev != NULL
 * brings up exactly that PCI function (extension path). */
int driver_block_ahci_init    (driver_pci_dev_t *pdev);
int driver_block_nvme_init    (driver_pci_dev_t *pdev);
int driver_block_ata_dma_init (driver_pci_dev_t *pdev);

/* drivers/block/driver_block_ata.h — capacity query used by the DMA driver */
int driver_block_ata_identify_capacity(int drive, u32 *sectors_out);
```

`driver_block_ops_t` gained an optional third hook:

```c
typedef struct driver_block_ops {
    int (*read) (driver_block_device_t *dev, u64 lba, u32 count, void *buf);
    int (*write)(driver_block_device_t *dev, u64 lba, u32 count, const void *buf);
    int (*flush)(driver_block_device_t *dev);   /* WP-10a: optional, may be NULL */
} driver_block_ops_t;
```

`driver_block_flush(dev)` first pushes the OS write-back cache for that device
(`driver_block_cache_flush_dev`) and then invokes the driver flush hook — so a
flush reaches the medium, not just the cache.

## 3. Shell commands

Driver status:

- `ahci` — controllers (BDF, BAR5, CAP, PI, NCQ), per-port SSTS/TFD,
  registered drive count.
- `nvme` — controller state, sector size, namespace capacity, live
  I/O queue pairs, blk device index.
- `ata` — BMDMA controllers (BDF, BAR4), per-drive DMA/PIO state.
- `dskstat` — now also prints a one-line driver summary
  (`drivers: ATA-DMA drives=N AHCI drives=N NVMe queues=N`).

Test suite (each prints `input / expect / actual => PASS|FAIL`):

- `ahci_test` — init + port discovery + capacity + rw round-trip.
- `nvme_test` — init + Identify + queue pairs + rw round-trip.
- `ata_dma_test` — BMDMA + PRDT + rw round-trip + DMA-vs-PIO timing.
- `virtio_blk_test` — capacity nonzero + rw round-trip.
- `disk_rw_test <dev>` — rw round-trips + cached-write/driver_block_flush/
  raw-verify consistency on one device.
- `partition_test [dev]` — writes MBR and GPT fixtures (restored
  afterwards), parses both back through `drivers/block/driver_block_part.c`.
- `fs_mount_test <dev>` — `mkfs.fs_fat32_device()` + FAT32 mount + file
  write/read round-trip through VFS + unmount (formats the device!).
- `real_hw_test` — CPUID hypervisor check; prints NOT RUN under a VM
  and a full driver report on bare metal.

## 4. FAT32 on every block device

`fs/fs_fat32.c` previously talked to the ATA PIO driver directly, so
volumes had to live on IDE drives. WP-10a routes all FAT32 sector I/O
through the blk layer:

```c
int driver_block_read_sectors_raw (ctx->dev_idx, lba, count, buf);
int driver_block_write_sectors_raw(ctx->dev_idx, lba, count, buf);
```

`fs_fat32_parse_device()` now resolves ANY registered block-device name
(`sda`, `nvme0`, `vda`, `hda`, ...) via `driver_block_find_device()`, with the
legacy forms (`ataN`, `hdX`, `N`) kept as aliases. The formatting logic
behind `mkfs.fat32` was extracted into `driver_block_mkfs_fat32_device(int dev_idx)`
(disk_cmds.h) so `fs_mount_test` exercises the same code path.

Verified on QEMU: FAT32 mount + file round-trip on `sda` (AHCI),
`nvme0` (NVMe), `vda` (virtio-blk) and `hda` (IDE DMA).

## 5. Regression fix: fork #PF (pmm physical window reservation)

Bringing up the four drivers shifts the physical allocation layout.
That exposed a latent assumption violation from the P3-1 security fix:
`create_user_address_space()` splits the user address space's
0x400000-0x600000 entry into an empty page table, but the kernel still
dereferences raw physical addresses (CR3 values, page-table frames,
fork's page copies) through the identity map. When a page-table frame
landed in 0x400000-0x600000 the access resolved through the REMAPPED
user window and crashed — fork_test died with #PF err=0x2 on every run
with the new drivers present.

Fix: `mem_pmm_init()` reserves the 512 physical frames 0x400000-0x600000
(2 MiB), so page tables, kernel stacks and all physical dereference
targets stay outside the remapped window (see the comment in
`kernel/mem/mem_pmm.c`). The WP-09 regression (18/18) passes again WITH all
four drivers attached.

## 6. Verification (QEMU, one 32 MiB disk per controller)

- BIOS (SeaBIOS) and UEFI (OVMF) boot both OK.
- `lsblk`: `hda [ATA]`, `vda [virtio]`, `sda [AHCI]`, `nvme0 [NVMe]` —
  each 65536 sectors = 32 MiB, virtio capacity correct (was 0).
- `ahci_test`, `nvme_test`, `ata_dma_test` (DMA=0 ticks vs PIO=1 tick),
  `virtio_blk_test`, `disk_rw_test` × 4 devices, `partition_test`
  (MBR + GPT), `fs_mount_test` × 4 devices — all PASS.
- WP-09 regression 18/18 in one session with all four disks attached.
- `dhtest` 5/5, boot exception self-test 3/3.
- `real_hw_test` reports NOT RUN under QEMU by design; on bare metal it
  prints the full driver report for real SATA/NVMe/IDE hardware.
