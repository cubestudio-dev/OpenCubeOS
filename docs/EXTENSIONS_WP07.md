<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 cubestudio-dev <cubestudio@qq.com> -->

# Open Cube OS — WP-07 Extension Interfaces

**WP-07 adds 6 new extension points (items 27-32). Total L1 surface is now 32.**

WP-07 unifies storage I/O behind a single block-device abstraction. ATA, virtio-blk, NVMe, and any future block driver all expose the same `read_sectors` / `write_sectors` interface through `drivers/block/driver_block_blk.h`. A 32 KiB LRU disk-block cache with write-back policy sits transparently in front of every block device, so file systems (FAT32, exFAT, ext4) no longer need to implement their own caching. A partition-table parser handles both MBR (legacy) and GPT (modern) layouts.

WP-07 also completes FAT32 write support (file creation, cluster allocation, chain extension, directory entry updates, file deletion) — see `EXTENSIONS_WP05.md` section 21; the WP-05 API (`fs_fat32_init`, `fs_fat32_mount`, `fs_fat32_get_stats`) is unchanged.

All WP-01..WP-06 interfaces remain unchanged. WP-07 interfaces are additive. The header `l1/l1_wp7.h` re-exports `blk.h`, `driver_block_cache.h`, and `part.h` so L1 has one place to look.

---

## Interface 27: Block device register / unregister

### Signature

```c
#include "driver_block_blk.h"

void driver_block_init(void);

int driver_block_register_device  (const char *name, driver_block_type_t type,
                          u64 sectors, u32 sector_size,
                          const driver_block_ops_t *ops, void *priv);
int driver_block_unregister_device(int index);

int          driver_block_find_device(const char *name);
driver_block_device_t*driver_block_get_device (int index);
void         driver_block_list_devices(void);
int          driver_block_num_devices(void);
```

### Purpose

The block-device registry is the central catalogue of all block devices known to the kernel. Each driver (ATA, virtio-blk, NVMe) calls `driver_block_register_device()` once per detected device at boot, passing its `name` (e.g. `"hda"`, `"vda"`, `"nvme0"`), `type`, capacity (`sectors`), `sector_size` (almost always 512), a pointer to its `driver_block_ops_t` read/write table, and a `priv` pointer for driver-private state.

The registry returns a small integer device index (`0 .. BLK_MAX_DEVICES-1`) that subsequent `driver_block_read_sectors()` / `driver_block_write_sectors()` calls use to address the device. File systems, the partition parser, and the `lsblk` shell command all talk to block devices through this layer — never directly to a driver.

`driver_block_unregister_device()` is provided for symmetry and for hot-unplug support in future WPs. It marks the slot as not-present; cached dirty blocks for the device should be flushed (`driver_block_cache_flush_dev()`) before unregistration to avoid data loss.

### `driver_block_device_t` and `driver_block_ops_t`

```c
typedef enum {
    BLK_TYPE_ATA    = 1,
    BLK_TYPE_VIRTIO = 2,
    BLK_TYPE_NVME   = 3,
    BLK_TYPE_AHCI   = 4,
} driver_block_type_t;

typedef struct driver_block_ops {
    int (*read )(driver_block_device_t *dev, u64 lba, u32 count, void *buf);
    int (*write)(driver_block_device_t *dev, u64 lba, u32 count, const void *buf);
} driver_block_ops_t;

struct driver_block_device {
    char     name[BLK_DEV_NAME_LEN];   /* "hda", "vda", "nvme0" (BLK_DEV_NAME_LEN = 32) */
    driver_block_type_t type;
    u64      sectors;                  /* total sectors (capacity) */
    u32      sector_size;              /* almost always 512 */
    u8       present;                  /* 1 = usable */
    u8       bus, dev, func;           /* PCI address (for virtio/nvme) */
    const driver_block_ops_t *ops;              /* driver read/write */
    void    *priv;                     /* driver private data */
};
```

### Parameters

| Function | Description |
|---|---|
| `driver_block_init()` | Zero the device table and stats. Call once at boot before any driver registers. No return value. |
| `driver_block_register_device(name, type, sectors, sector_size, ops, priv)` | Add a device to the registry. `name` is copied (truncated at `BLK_DEV_NAME_LEN-1`). `sector_size` may be 0 — defaults to `BLK_SECTOR_SIZE` (512). `ops` is stored by reference (caller must keep it valid for the device's lifetime). `priv` is opaque to the block layer. Returns the new device index (`>= 0`) on success, -1 if the table is full (`BLK_MAX_DEVICES = 8`). |
| `driver_block_unregister_device(index)` | Mark the device at `index` as not-present. Does **not** flush dirty cache blocks — caller must `driver_block_cache_flush_dev(index)` first. Returns 0 on success, -1 on bad index. |
| `driver_block_find_device(name)` | Look up a device by name (e.g. `"hda"`). Returns the index, or -1 if not found. |
| `driver_block_get_device(index)` | Return a pointer to the `driver_block_device_t` at `index`, or NULL if out of range or not present. |
| `driver_block_list_devices()` | Print all present devices to the console (used by the `lsblk` shell command). |
| `driver_block_num_devices()` | Return the count of currently-present devices. |

### Return value

- `driver_block_init`, `driver_block_list_devices`: no return value.
- `driver_block_register_device`: device index `>= 0` on success, -1 on full.
- `driver_block_unregister_device`: 0 on success, -1 on bad index.
- `driver_block_find_device`: device index `>= 0` on success, -1 on not found.
- `driver_block_get_device`: `driver_block_device_t *` on success, NULL on bad index / not present.
- `driver_block_num_devices`: count of present devices (`>= 0`).

### Example — registering a driver's device

```c
#include "driver_block_blk.h"

/* Driver-private state. */
typedef struct { int driver_block_ata_drive; } my_ata_priv_t;
static my_ata_priv_t g_priv0 = { .driver_block_ata_drive = 0 };

static int my_read (driver_block_device_t *dev, u64 lba, u32 count, void *buf) {
    my_ata_priv_t *p = dev->priv;
    int n = driver_block_ata_read_sectors(p->driver_block_ata_drive, lba, (int)count, buf);
    return (n == (int)count) ? 0 : -1;
}
static int my_write(driver_block_device_t *dev, u64 lba, u32 count, const void *buf) {
    my_ata_priv_t *p = dev->priv;
    int n = driver_block_ata_write_sectors(p->driver_block_ata_drive, lba, (int)count, buf);
    return (n == (int)count) ? 0 : -1;
}
static const driver_block_ops_t my_ops = { .read = my_read, .write = my_write };

void my_ata_register(void) {
    driver_block_init();   /* once at boot */
    /* Probe ATA drive 0; if present, register it. */
    if (driver_block_ata_detect(0)) {
        /* 128 GiB max for LBA28; use a sensible default. */
        int idx = driver_block_register_device("hda", BLK_TYPE_ATA,
                                      268435455ULL, 512, &my_ops, &g_priv0);
        if (idx < 0) { /* table full */ }
    }
}
```

### Example — looking up and inspecting

```c
void list_my_disks(void) {
    driver_block_list_devices();   /* console output */

    int idx = driver_block_find_device("hda");
    if (idx < 0) return;
    driver_block_device_t *d = driver_block_get_device(idx);
    /* d->sectors, d->sector_size, d->type, d->name, ... */

    driver_block_stats_t st;
    driver_block_get_stats(idx, &st);
    /* st.reads, st.writes, st.read_sectors, st.write_sectors */
}
```

### Caveats

- **Fixed capacity**: `BLK_MAX_DEVICES = 8` is a compile-time constant. Increasing it is an ABI change.
- **`ops` stored by reference**: the driver must keep its `driver_block_ops_t` struct alive for the device's lifetime (typically a `static const`).
- **`name` collisions are not detected**: registering two devices with the same name will cause `driver_block_find_device()` to return the first match. Choose unique names.
- **`priv` is opaque**: the block layer never dereferences `priv`. Drivers can store anything there.
- **Unregister does not invalidate cached data**: always call `driver_block_cache_flush_dev(index)` before `driver_block_unregister_device(index)` if the device may have dirty cached blocks.

---

## Interface 28: Block I/O (read / write sectors)

### Signature

```c
#include "driver_block_blk.h"

int driver_block_read_sectors (int dev_idx, u64 lba, u32 count, void *buf);
int driver_block_write_sectors(int dev_idx, u64 lba, u32 count, const void *buf);

/* Bypass the cache (used by the cache itself and by tools like mkfs). */
int driver_block_read_sectors_raw (int dev_idx, u64 lba, u32 count, void *buf);
int driver_block_write_sectors_raw(int dev_idx, u64 lba, u32 count, const void *buf);

void driver_block_get_stats(int dev_idx, driver_block_stats_t *out);
```

### Purpose

These are the I/O entry points that file systems and the partition parser use to talk to a block device. `driver_block_read_sectors` / `driver_block_write_sectors` go through the LRU disk-block cache (write-back): a read first checks the cache, and on a miss falls back to a raw single-sector read, then inserts the result into the cache. A write updates the cache and marks the block dirty; the dirty block is written back to disk lazily (on eviction, on `driver_block_cache_flush()`, or on `driver_block_cache_flush_dev()`).

`driver_block_read_sectors_raw` / `driver_block_write_sectors_raw` bypass the cache entirely and call the driver's `ops->read` / `ops->write` directly. They are intended for:

- The cache implementation itself (otherwise it would recurse).
- Tools that must see on-disk state directly, e.g. `mkfs`, `fsck`, partition-table writers.
- Reads of sectors that the cache should never hold (e.g. the GPT header at LBA 1 — the partition parser uses `_raw`).

`buf` must be at least `count * 512` bytes. Reads and writes are 512-byte-sector aligned; sub-sector I/O is not supported.

### Parameters

| Function | Description |
|---|---|
| `driver_block_read_sectors(dev_idx, lba, count, buf)` | Read `count` 512-byte sectors starting at `lba` from device `dev_idx` into `buf`. Goes through the cache (one sector at a time). Returns 0 on success, -1 on bad device / I/O error. |
| `driver_block_write_sectors(dev_idx, lba, count, buf)` | Write `count` 512-byte sectors from `buf` to device `dev_idx` starting at `lba`. Updates the cache and marks blocks dirty (write-back). Returns 0 on success. |
| `driver_block_read_sectors_raw(dev_idx, lba, count, buf)` | Direct uncached read. Bypasses the cache. Returns 0 on success, -1 on error. |
| `driver_block_write_sectors_raw(dev_idx, lba, count, buf)` | Direct uncached write. Bypasses the cache (does not invalidate cached copies — caller's responsibility). Returns 0 on success. |
| `driver_block_get_stats(dev_idx, out)` | Fill `*out` with cumulative per-device counters: `reads`, `writes`, `read_sectors`, `write_sectors`. No return value. |

### `driver_block_stats_t`

```c
typedef struct {
    u64 reads;
    u64 writes;
    u64 read_sectors;
    u64 write_sectors;
} driver_block_stats_t;
```

### Return value

- `driver_block_read_sectors`, `driver_block_write_sectors`, `driver_block_read_sectors_raw`, `driver_block_write_sectors_raw`: 0 on success, -1 on bad device or I/O error.
- `driver_block_get_stats`: no return value (writes through `out`).

### Example — file-system read pattern

```c
#include "driver_block_blk.h"

/* Read the boot sector of device "hda" through the cache. */
void read_boot_sector(void) {
    int idx = driver_block_find_device("hda");
    if (idx < 0) return;

    u8 buf[512];
    if (driver_block_read_sectors(idx, 0, 1, buf) != 0) return;
    /* buf[510] == 0x55 && buf[511] == 0xAA for a valid MBR */
}
```

### Example — `mkfs`-style raw write

```c
/* Write a fresh MBR. Use _raw so we don't accidentally serve stale cached
 * data on the next read, and so we don't pollute the cache. */
void write_mbr(int dev_idx, const u8 mbr[512]) {
    driver_block_write_sectors_raw(dev_idx, 0, 1, mbr);
}
```

### Caveats

- **Cache is one-sector granularity**: even when `count > 1`, the cache loop processes one sector at a time. Multi-sector reads/writes from the underlying driver are not coalesced (yet).
- **Write-back, not write-through**: a successful `driver_block_write_sectors()` does NOT guarantee the data is on disk. Call `driver_block_cache_flush()` (or `driver_block_cache_flush_dev()`) before power-off or before unregistering a device.
- **`_raw` does not invalidate cached copies**: if a sector is in the cache and you `driver_block_write_sectors_raw()` it, a subsequent `driver_block_read_sectors()` may still return the stale cached copy. Use `_raw` only when you know the cache is cold (boot time, after a flush, or for sectors the cache never holds).
- **No scatter/gather**: `buf` must be a single contiguous buffer.
- **No async / IRQ-driven I/O**: all calls are synchronous and block until the driver completes.

---

## Interface 29: Partition table parse (MBR / GPT)

### Signature

```c
#include "driver_block_part.h"

int driver_block_part_parse    (int dev_idx, driver_block_part_table_t *out);
int driver_block_part_parse_mbr(int dev_idx, driver_block_part_table_t *out);
int driver_block_part_parse_gpt(int dev_idx, driver_block_part_table_t *out);
```

### Purpose

Parse the on-disk partition table of a block device into an in-memory `driver_block_part_table_t` structure. Both legacy MBR (4 primary entries, no GPT) and modern GPT (up to 128 entries) are supported.

`driver_block_part_parse()` is the recommended entry point: it tries MBR first, and if the MBR contains a GPT protective entry (type `0xEE`), it transparently re-parses as GPT. `driver_block_part_parse_mbr()` and `driver_block_part_parse_gpt()` are convenience wrappers for callers that know which format to expect.

Both parsers use `driver_block_read_sectors_raw()` (the cache-bypassing variant) so they never pollute the cache with one-time boot-sector reads, and so they always see on-disk state.

### `driver_block_part_table_t` and `partition_t`

```c
#define PART_MAX_PARTITIONS  16
#define PART_NAME_LEN        40

typedef enum {
    PART_TYPE_EMPTY = 0,
    PART_TYPE_MBR   = 1,
    PART_TYPE_GPT   = 2,
} driver_block_part_table_type_t;

typedef struct {
    u8  present;
    u8  bootable;
    u8  type;               /* MBR partition type byte, or GPT type GUID[0] */
    u64 start_lba;
    u64 sectors;
    char name[PART_NAME_LEN]; /* GPT name or "MBR-N" / "GPT-N" */
} partition_t;

typedef struct {
    int              dev_idx;
    driver_block_part_table_type_t table_type;
    int              count;
    partition_t      parts[PART_MAX_PARTITIONS];
} driver_block_part_table_t;
```

### Parameters

| Function | Description |
|---|---|
| `driver_block_part_parse(dev_idx, out)` | Parse the partition table of device `dev_idx` into `*out`. Tries MBR first; if the MBR has a GPT protective entry, re-parses as GPT. Returns 0 on success, -1 on bad arg or I/O error. |
| `driver_block_part_parse_mbr(dev_idx, out)` | Parse only the MBR. Sets `out->table_type = PART_TYPE_MBR`. Reads sector 0 raw; validates the `0xAA55` signature; iterates the 4 primary entries; skips type-0 entries. If a GPT protective entry (type `0xEE`) is found, transparently dispatches to `driver_block_part_parse_gpt()`. Returns 0 on success, -1 on bad signature or I/O error. |
| `driver_block_part_parse_gpt(dev_idx, out)` | Parse only the GPT. Sets `out->table_type = PART_TYPE_GPT`. Reads the GPT header at LBA 1, validates the `"EFI PART"` signature, reads up to 8 sectors of partition entries (enough for 128 entries), and copies up to `PART_MAX_PARTITIONS` used entries into `out->parts`. Returns 0 on success, -1 on bad signature or I/O error. |

### Return value

- 0 on success — `*out` is populated.
- -1 on bad arg, bad signature, or I/O error.

### Example

```c
#include "driver_block_part.h"
#include "driver_block_blk.h"

void driver_block_part_demo(void) {
    int idx = driver_block_find_device("hda");
    if (idx < 0) return;

    driver_block_part_table_t tbl;
    if (driver_block_part_parse(idx, &tbl) != 0) {
        /* no valid partition table on this disk */
        return;
    }

    /* tbl.table_type is PART_TYPE_MBR or PART_TYPE_GPT */
    /* tbl.count is the number of partitions found (0..PART_MAX_PARTITIONS) */

    for (int i = 0; i < tbl.count; i++) {
        partition_t *p = &tbl.parts[i];
        if (!p->present) continue;
        /* p->start_lba, p->sectors, p->type, p->bootable, p->name */
    }
}
```

### Caveats

- **MBR extended partitions are not followed**: only the 4 primary MBR entries are parsed. Logical partitions inside an extended partition are not enumerated (future WP).
- **GPT entry size is capped**: the parser reads at most 8 sectors of GPT entries (4096 bytes / 128 entries). Larger GPT tables are truncated at `PART_MAX_PARTITIONS = 16` entries.
- **GPT partition `type` is just the first byte of the type GUID**: full 16-byte type-GUID comparison is not yet exposed. Use the `name` field if you need to identify partitions.
- **GPT name is ASCII-truncated**: the on-disk name is UTF-16LE (72 bytes / 36 code units). The parser takes the low byte of each code unit and stops at the first null, so non-ASCII names are mangled.
- **No write support**: WP-07 only reads partition tables. Writing / creating / deleting partitions is future work.
- **Uses `_raw` reads**: the partition parser bypasses the cache. This means it sees on-disk state directly, but also means re-parsing the same device repeatedly will hit the disk every time.

---

## Interface 30: Partition query and print

### Signature

```c
#include "driver_block_part.h"

partition_t *driver_block_part_get_partition(driver_block_part_table_t *tbl, int index);
void         driver_block_part_print        (const driver_block_part_table_t *tbl);
```

### Purpose

Once a `driver_block_part_table_t` has been populated by `driver_block_part_parse()`, these helpers let L1 index into it and pretty-print it.

`driver_block_part_get_partition()` returns a pointer to the `index`-th partition (0-based), or NULL if `index` is out of range. Mutating the returned struct mutates the table — be careful.

`driver_block_part_print()` writes a human-readable table to the console: partition number, start LBA, sector count, type byte, and name. Used by the `parted` shell command.

### Parameters

| Function | Description |
|---|---|
| `driver_block_part_get_partition(tbl, index)` | Return a pointer to `tbl->parts[index]`, or NULL if `tbl` is NULL, `index < 0`, or `index >= tbl->count`. |
| `driver_block_part_print(tbl)` | Print the partition table to the console in a fixed column format. No-op if `tbl` is NULL. |

### Return value

- `driver_block_part_get_partition`: `partition_t *` on success, NULL on bad index.
- `driver_block_part_print`: no return value.

### Example

```c
#include "driver_block_part.h"
#include "screen_console.h"

void driver_block_part_query_demo(void) {
    int idx = driver_block_find_device("hda");
    if (idx < 0) return;

    driver_block_part_table_t tbl;
    if (driver_block_part_parse(idx, &tbl) != 0) return;

    /* Pretty-print the whole table. */
    driver_block_part_print(&tbl);

    /* Index into it. */
    partition_t *p0 = driver_block_part_get_partition(&tbl, 0);
    if (p0 && p0->present) {
        /* p0->start_lba is where partition 0 begins. */
        /* To mount a FAT32 file system on it, you would call
         * fs_fat32_mount("hda", "/mnt/disk") and the FAT32 driver
         * would itself parse the partition table to find this LBA,
         * OR you can mount it directly via fs_vfs_mount with the
         * partition-aware device name "hda1", "hda2", etc. */
    }
}
```

### Example console output of `driver_block_part_print`

```
Partition table (GPT):
  #  Start          Sectors        Type  Name
  0  2048           1048576        0xEB  EFI System
  1  1050624        2095104        0x0C  Microsoft basic data
  2  3145728        4194304        0x83  Linux filesystem
```

### Caveats

- **Pointer aliasing**: `driver_block_part_get_partition()` returns a pointer into the table, not a copy. Mutations are visible to subsequent callers.
- **No partition-name escaping**: `driver_block_part_print` writes the raw `name` bytes to the console. Non-printable bytes in GPT names will appear as garbage.
- **No filtering**: `driver_block_part_print` shows every `present` partition. There is no `--type` flag yet.

---

## Interface 31: Disk cache statistics

### Signature

```c
#include "driver_block_cache.h"

void driver_block_cache_init(void);
void driver_block_cache_get_stats(driver_block_cache_stats_t *out);

/* Cache primitives (used by driver_block_read_sectors / driver_block_write_sectors; also
 * callable directly by L1 if you need finer control). */
int driver_block_cache_read  (int dev_idx, u64 lba, void *buf);
int driver_block_cache_write (int dev_idx, u64 lba, const void *buf);
int driver_block_cache_insert(int dev_idx, u64 lba, const void *buf);
```

### Purpose

The disk-block cache is a 32 KiB LRU cache (64 slots × 512 bytes) sitting in front of every block device. It uses a write-back policy: writes update the cache and mark the block dirty; dirty blocks are written back to disk lazily (on eviction or on explicit `driver_block_cache_flush()`).

`driver_block_cache_get_stats()` returns cumulative counters that are useful for performance tuning and for the `cachestat` shell command:

- `hits` — cache hits on read.
- `misses` — cache misses on read (each miss triggers a 1-sector raw read).
- `evicts` — number of slots reclaimed by LRU eviction.
- `writebacks` — number of dirty blocks written back to disk.
- `used` — number of slots currently in use (0..64).

The cache primitives `driver_block_cache_read` / `driver_block_cache_write` / `driver_block_cache_insert` are exposed for L1 code that wants finer control than `driver_block_read_sectors` / `driver_block_write_sectors` offer — for example, a file system that wants to insert a freshly-read cluster into the cache without re-reading it through the block layer. Most L1 code should use the higher-level `driver_block_read_sectors` / `driver_block_write_sectors` instead.

### `driver_block_cache_slot_t` and `driver_block_cache_stats_t`

```c
#define BLK_CACHE_SLOTS  64   /* 64 * 512 = 32 KiB cache */

typedef struct {
    int  dev_idx;        /* -1 = empty slot */
    u64  lba;
    u8   data[512];
    u8   dirty;          /* 1 = needs write-back */
    u64  last_used;      /* tick count for LRU */
} driver_block_cache_slot_t;

typedef struct {
    u64 hits;
    u64 misses;
    u64 evicts;
    u64 writebacks;
    u32 used;            /* slots in use */
} driver_block_cache_stats_t;
```

### Parameters

| Function | Description |
|---|---|
| `driver_block_cache_init()` | Zero the cache (all slots `dev_idx = -1`, `dirty = 0`). Call once at boot before any block I/O. No return value. |
| `driver_block_cache_get_stats(out)` | Fill `*out` with the cumulative cache counters. No return value. |
| `driver_block_cache_read(dev_idx, lba, buf)` | Look up sector `(dev_idx, lba)` in the cache. On a hit, copy 512 bytes into `buf` and return 0. On a miss, return -1 (caller should fall back to a raw read and then `driver_block_cache_insert()`). |
| `driver_block_cache_write(dev_idx, lba, buf)` | Copy 512 bytes from `buf` into the cache slot for `(dev_idx, lba)`, marking it dirty. Allocates a slot (evicting the LRU entry, writing it back if dirty) if needed. Returns 0 on success, -1 on error. |
| `driver_block_cache_insert(dev_idx, lba, buf)` | Insert a freshly-read sector into the cache as a clean (non-dirty) entry. Useful after a raw read to warm the cache. Returns 0 on success, -1 on error. |

### Return value

- `driver_block_cache_init`, `driver_block_cache_get_stats`: no return value.
- `driver_block_cache_read`: 0 on hit, -1 on miss.
- `driver_block_cache_write`, `driver_block_cache_insert`: 0 on success, -1 on error.

### Example

```c
#include "driver_block_cache.h"

void cache_stats_demo(void) {
    driver_block_cache_stats_t st;
    driver_block_cache_get_stats(&st);

    /* st.hits, st.misses, st.evicts, st.writebacks, st.used */
    /* A healthy system has hits >> misses; if misses dominate,
     * the working set is larger than the 32 KiB cache. */
}
```

### Example — warming the cache after a raw read

```c
u8 buf[512];
if (driver_block_read_sectors_raw(dev_idx, lba, 1, buf) == 0) {
    driver_block_cache_insert(dev_idx, lba, buf);   /* don't re-read on next access */
}
```

### Caveats

- **32 KiB cache size is small**: with 64 slots, the working set of a multi-file file-system operation may exceed the cache. Tuning `BLK_CACHE_SLOTS` is a compile-time change (ABI-affecting).
- **Per-sector granularity**: the cache does not coalesce adjacent sectors into a single slot. Reading a 4 KiB cluster occupies 8 slots.
- **No per-device partitioning**: all devices share the same 64-slot cache. A hot ATA device can evict a hot virtio-blk device's blocks.
- **`driver_block_cache_write` does not write through to disk**: the data is in the cache only. It will be written back lazily — call `driver_block_cache_flush()` if you need it on disk now.

---

## Interface 32: Disk cache flush

### Signature

```c
#include "driver_block_cache.h"

int driver_block_cache_flush    (void);
int driver_block_cache_flush_dev(int dev_idx);
```

### Purpose

Write back all dirty cache blocks to the underlying block device.

- `driver_block_cache_flush()` flushes every dirty block in the cache, regardless of which device it belongs to. Use this before power-off, before a clean unmount of all file systems, or before a snapshot / checkpoint.
- `driver_block_cache_flush_dev(dev_idx)` flushes only the dirty blocks belonging to `dev_idx`. Use this before unmounting a single file system, before unregistering a device, or before any operation that needs on-disk consistency for one device.

Both functions walk the cache, find dirty slots, write each one back via `driver_block_write_sectors_raw()` (which bypasses the cache — otherwise it would recurse), and clear the dirty bit.

These are **synchronous** calls. They block until all dirty blocks are written.

### Parameters

| Function | Description |
|---|---|
| `driver_block_cache_flush()` | Walk every cache slot; for each dirty slot, write its 512 bytes back to its device via `driver_block_write_sectors_raw()`, then clear the dirty bit. Returns 0 on success, -1 if any write-back failed (the function continues past failures and returns -1 if any occurred). |
| `driver_block_cache_flush_dev(dev_idx)` | Same, but only for slots whose `dev_idx` matches. Returns 0 on success, -1 on bad `dev_idx` or any write-back failure. |

### Return value

- 0 on success (all dirty blocks written back, all dirty bits cleared).
- -1 if any write-back failed (the function still attempts the remaining slots).

### Example

```c
#include "driver_block_cache.h"

void clean_shutdown(void) {
    /* Flush all file systems' dirty blocks to disk. */
    if (driver_block_cache_flush() != 0) {
        /* log a warning — some block failed to write */
    }

    /* Now safe to power off. */
}

void unmount_one(int dev_idx) {
    /* Flush just this device's dirty blocks. */
    driver_block_cache_flush_dev(dev_idx);

    /* Safe to unregister the device. */
    driver_block_unregister_device(dev_idx);
}
```

### Example — `sync` shell command pattern

```c
/* The `sync` shell command is essentially: */
int shell_cmd_sync(const char *args) {
    (void)args;
    int rc = driver_block_cache_flush();
    screen_console_puts(rc == 0 ? "sync: ok\n" : "sync: errors\n");
    return rc;
}
```

### Caveats

- **Synchronous**: blocks until all dirty blocks are written. On a slow ATA device with many dirty blocks, this can take hundreds of milliseconds.
- **No batching**: each dirty sector is written back as a separate 1-sector `driver_block_write_sectors_raw()` call. There is no coalescing of adjacent dirty sectors into multi-sector writes (future WP).
- **Failure handling**: if a write-back fails, the dirty bit is **not** cleared — the block stays dirty and will be retried on the next flush. The function returns -1 but continues with the remaining blocks.
- **Not IRQ-safe**: `driver_block_cache_flush()` calls `driver_block_write_sectors_raw()` which calls into the driver, which may sleep on the device. Do not call from an IRQ handler.
- **Does not invalidate clean entries**: flushing only writes back dirty blocks. Clean cached blocks remain in the cache. To invalidate the whole cache, call `driver_block_cache_init()` (which loses any dirty data — flush first).

---

## ABI Stability

All WP-07 functions, structs, and their typedefs are frozen:

- **Block device register / unregister (item 27)**: `driver_block_init`, `driver_block_register_device`, `driver_block_unregister_device`, `driver_block_find_device`, `driver_block_get_device`, `driver_block_list_devices`, `driver_block_num_devices` — signatures frozen. The `driver_block_device_t` struct layout, `driver_block_ops_t` struct layout, `driver_block_type_t` enum, and `driver_block_stats_t` struct layout are frozen. Capacity limits frozen: `BLK_MAX_DEVICES = 8`, `BLK_SECTOR_SIZE = 512`, `BLK_DEV_NAME_LEN = 32`.
- **Block I/O (item 28)**: `driver_block_read_sectors`, `driver_block_write_sectors`, `driver_block_read_sectors_raw`, `driver_block_write_sectors_raw`, `driver_block_get_stats` — signatures frozen. The 512-byte sector size and LBA addressing convention are frozen.
- **Partition parse (item 29)**: `driver_block_part_parse`, `driver_block_part_parse_mbr`, `driver_block_part_parse_gpt` — signatures frozen. The `partition_t` struct layout, `driver_block_part_table_t` struct layout, and `driver_block_part_table_type_t` enum are frozen. Capacity limits frozen: `PART_MAX_PARTITIONS = 16`, `PART_NAME_LEN = 40`.
- **Partition query (item 30)**: `driver_block_part_get_partition`, `driver_block_part_print` — signatures frozen.
- **Disk cache stats (item 31)**: `driver_block_cache_init`, `driver_block_cache_get_stats`, `driver_block_cache_read`, `driver_block_cache_write`, `driver_block_cache_insert` — signatures frozen. The `driver_block_cache_slot_t` struct layout, `driver_block_cache_stats_t` struct layout, and `BLK_CACHE_SLOTS = 64` constant are frozen.
- **Disk cache flush (item 32)**: `driver_block_cache_flush`, `driver_block_cache_flush_dev` — signatures frozen.

The `driver_block_device_t.priv` pointer is driver-private and explicitly **not** part of the ABI — its layout depends on the registered driver. The `driver_block_ops_t.read` / `driver_block_ops_t.write` function-pointer signatures are part of the ABI (drivers must match them exactly).

The struct layouts are frozen with respect to the fields documented above. New fields may be appended at the end in future WPs; callers must zero-initialise structs before passing them in to allow this.

Future WPs may add new functions (e.g. `driver_block_register_irq_handler`, `driver_block_async_read`, `driver_block_part_create`, `driver_block_part_delete`, `driver_block_cache_resize`) but will not change existing ones incompatibly.

## Loading model

WP-07 still does not have a dynamic module loader — L1 is linked into the same binary as L0. The extension API is designed to survive the transition to loadable modules unchanged: a loadable block driver would just call `driver_block_register_device()` from its module-init function, and a loadable file-system driver would discover partitions via `driver_block_part_parse()` and mount them via the WP-05 `fs_vfs_mount()` API.
