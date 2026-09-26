# Open Cube OS — WP-07 Extension Interfaces

**WP-07 adds 6 new extension points (items 27-32). Total L1 surface is now 32.**

WP-07 unifies storage I/O behind a single block-device abstraction. ATA, virtio-blk, NVMe, and any future block driver all expose the same `read_sectors` / `write_sectors` interface through `kernel/blk.h`. A 32 KiB LRU disk-block cache with write-back policy sits transparently in front of every block device, so file systems (FAT32, exFAT, ext4) no longer need to implement their own caching. A partition-table parser handles both MBR (legacy) and GPT (modern) layouts.

WP-07 also completes FAT32 write support (file creation, cluster allocation, chain extension, directory entry updates, file deletion) — see `EXTENSIONS_WP05.md` section 21; the WP-05 API (`fat32_init`, `fat32_mount`, `fat32_get_stats`) is unchanged.

All WP-01..WP-06 interfaces remain unchanged. WP-07 interfaces are additive. The header `kernel/ext_wp7.h` re-exports `blk.h`, `blk_cache.h`, and `part.h` so L1 has one place to look.

---

## Interface 27: Block device register / unregister

### Signature

```c
#include "blk.h"

void blk_init(void);

int blk_register_device  (const char *name, blk_type_t type,
                          u64 sectors, u32 sector_size,
                          const blk_ops_t *ops, void *priv);
int blk_unregister_device(int index);

int          blk_find_device(const char *name);
blk_device_t*blk_get_device (int index);
void         blk_list_devices(void);
int          blk_num_devices(void);
```

### Purpose

The block-device registry is the central catalogue of all block devices known to the kernel. Each driver (ATA, virtio-blk, NVMe) calls `blk_register_device()` once per detected device at boot, passing its `name` (e.g. `"hda"`, `"vda"`, `"nvme0"`), `type`, capacity (`sectors`), `sector_size` (almost always 512), a pointer to its `blk_ops_t` read/write table, and a `priv` pointer for driver-private state.

The registry returns a small integer device index (`0 .. BLK_MAX_DEVICES-1`) that subsequent `blk_read_sectors()` / `blk_write_sectors()` calls use to address the device. File systems, the partition parser, and the `lsblk` shell command all talk to block devices through this layer — never directly to a driver.

`blk_unregister_device()` is provided for symmetry and for hot-unplug support in future WPs. It marks the slot as not-present; cached dirty blocks for the device should be flushed (`blk_cache_flush_dev()`) before unregistration to avoid data loss.

### `blk_device_t` and `blk_ops_t`

```c
typedef enum {
    BLK_TYPE_ATA    = 1,
    BLK_TYPE_VIRTIO = 2,
    BLK_TYPE_NVME   = 3,
    BLK_TYPE_AHCI   = 4,
} blk_type_t;

typedef struct blk_ops {
    int (*read )(blk_device_t *dev, u64 lba, u32 count, void *buf);
    int (*write)(blk_device_t *dev, u64 lba, u32 count, const void *buf);
} blk_ops_t;

struct blk_device {
    char     name[BLK_DEV_NAME_LEN];   /* "hda", "vda", "nvme0" (BLK_DEV_NAME_LEN = 32) */
    blk_type_t type;
    u64      sectors;                  /* total sectors (capacity) */
    u32      sector_size;              /* almost always 512 */
    u8       present;                  /* 1 = usable */
    u8       bus, dev, func;           /* PCI address (for virtio/nvme) */
    const blk_ops_t *ops;              /* driver read/write */
    void    *priv;                     /* driver private data */
};
```

### Parameters

| Function | Description |
|---|---|
| `blk_init()` | Zero the device table and stats. Call once at boot before any driver registers. No return value. |
| `blk_register_device(name, type, sectors, sector_size, ops, priv)` | Add a device to the registry. `name` is copied (truncated at `BLK_DEV_NAME_LEN-1`). `sector_size` may be 0 — defaults to `BLK_SECTOR_SIZE` (512). `ops` is stored by reference (caller must keep it valid for the device's lifetime). `priv` is opaque to the block layer. Returns the new device index (`>= 0`) on success, -1 if the table is full (`BLK_MAX_DEVICES = 8`). |
| `blk_unregister_device(index)` | Mark the device at `index` as not-present. Does **not** flush dirty cache blocks — caller must `blk_cache_flush_dev(index)` first. Returns 0 on success, -1 on bad index. |
| `blk_find_device(name)` | Look up a device by name (e.g. `"hda"`). Returns the index, or -1 if not found. |
| `blk_get_device(index)` | Return a pointer to the `blk_device_t` at `index`, or NULL if out of range or not present. |
| `blk_list_devices()` | Print all present devices to the console (used by the `lsblk` shell command). |
| `blk_num_devices()` | Return the count of currently-present devices. |

### Return value

- `blk_init`, `blk_list_devices`: no return value.
- `blk_register_device`: device index `>= 0` on success, -1 on full.
- `blk_unregister_device`: 0 on success, -1 on bad index.
- `blk_find_device`: device index `>= 0` on success, -1 on not found.
- `blk_get_device`: `blk_device_t *` on success, NULL on bad index / not present.
- `blk_num_devices`: count of present devices (`>= 0`).

### Example — registering a driver's device

```c
#include "blk.h"

/* Driver-private state. */
typedef struct { int ata_drive; } my_ata_priv_t;
static my_ata_priv_t g_priv0 = { .ata_drive = 0 };

static int my_read (blk_device_t *dev, u64 lba, u32 count, void *buf) {
    my_ata_priv_t *p = dev->priv;
    int n = ata_read_sectors(p->ata_drive, lba, (int)count, buf);
    return (n == (int)count) ? 0 : -1;
}
static int my_write(blk_device_t *dev, u64 lba, u32 count, const void *buf) {
    my_ata_priv_t *p = dev->priv;
    int n = ata_write_sectors(p->ata_drive, lba, (int)count, buf);
    return (n == (int)count) ? 0 : -1;
}
static const blk_ops_t my_ops = { .read = my_read, .write = my_write };

void my_ata_register(void) {
    blk_init();   /* once at boot */
    /* Probe ATA drive 0; if present, register it. */
    if (ata_detect(0)) {
        /* 128 GiB max for LBA28; use a sensible default. */
        int idx = blk_register_device("hda", BLK_TYPE_ATA,
                                      268435455ULL, 512, &my_ops, &g_priv0);
        if (idx < 0) { /* table full */ }
    }
}
```

### Example — looking up and inspecting

```c
void list_my_disks(void) {
    blk_list_devices();   /* console output */

    int idx = blk_find_device("hda");
    if (idx < 0) return;
    blk_device_t *d = blk_get_device(idx);
    /* d->sectors, d->sector_size, d->type, d->name, ... */

    blk_stats_t st;
    blk_get_stats(idx, &st);
    /* st.reads, st.writes, st.read_sectors, st.write_sectors */
}
```

### Caveats

- **Fixed capacity**: `BLK_MAX_DEVICES = 8` is a compile-time constant. Increasing it is an ABI change.
- **`ops` stored by reference**: the driver must keep its `blk_ops_t` struct alive for the device's lifetime (typically a `static const`).
- **`name` collisions are not detected**: registering two devices with the same name will cause `blk_find_device()` to return the first match. Choose unique names.
- **`priv` is opaque**: the block layer never dereferences `priv`. Drivers can store anything there.
- **Unregister does not invalidate cached data**: always call `blk_cache_flush_dev(index)` before `blk_unregister_device(index)` if the device may have dirty cached blocks.

---

## Interface 28: Block I/O (read / write sectors)

### Signature

```c
#include "blk.h"

int blk_read_sectors (int dev_idx, u64 lba, u32 count, void *buf);
int blk_write_sectors(int dev_idx, u64 lba, u32 count, const void *buf);

/* Bypass the cache (used by the cache itself and by tools like mkfs). */
int blk_read_sectors_raw (int dev_idx, u64 lba, u32 count, void *buf);
int blk_write_sectors_raw(int dev_idx, u64 lba, u32 count, const void *buf);

void blk_get_stats(int dev_idx, blk_stats_t *out);
```

### Purpose

These are the I/O entry points that file systems and the partition parser use to talk to a block device. `blk_read_sectors` / `blk_write_sectors` go through the LRU disk-block cache (write-back): a read first checks the cache, and on a miss falls back to a raw single-sector read, then inserts the result into the cache. A write updates the cache and marks the block dirty; the dirty block is written back to disk lazily (on eviction, on `blk_cache_flush()`, or on `blk_cache_flush_dev()`).

`blk_read_sectors_raw` / `blk_write_sectors_raw` bypass the cache entirely and call the driver's `ops->read` / `ops->write` directly. They are intended for:

- The cache implementation itself (otherwise it would recurse).
- Tools that must see on-disk state directly, e.g. `mkfs`, `fsck`, partition-table writers.
- Reads of sectors that the cache should never hold (e.g. the GPT header at LBA 1 — the partition parser uses `_raw`).

`buf` must be at least `count * 512` bytes. Reads and writes are 512-byte-sector aligned; sub-sector I/O is not supported.

### Parameters

| Function | Description |
|---|---|
| `blk_read_sectors(dev_idx, lba, count, buf)` | Read `count` 512-byte sectors starting at `lba` from device `dev_idx` into `buf`. Goes through the cache (one sector at a time). Returns 0 on success, -1 on bad device / I/O error. |
| `blk_write_sectors(dev_idx, lba, count, buf)` | Write `count` 512-byte sectors from `buf` to device `dev_idx` starting at `lba`. Updates the cache and marks blocks dirty (write-back). Returns 0 on success. |
| `blk_read_sectors_raw(dev_idx, lba, count, buf)` | Direct uncached read. Bypasses the cache. Returns 0 on success, -1 on error. |
| `blk_write_sectors_raw(dev_idx, lba, count, buf)` | Direct uncached write. Bypasses the cache (does not invalidate cached copies — caller's responsibility). Returns 0 on success. |
| `blk_get_stats(dev_idx, out)` | Fill `*out` with cumulative per-device counters: `reads`, `writes`, `read_sectors`, `write_sectors`. No return value. |

### `blk_stats_t`

```c
typedef struct {
    u64 reads;
    u64 writes;
    u64 read_sectors;
    u64 write_sectors;
} blk_stats_t;
```

### Return value

- `blk_read_sectors`, `blk_write_sectors`, `blk_read_sectors_raw`, `blk_write_sectors_raw`: 0 on success, -1 on bad device or I/O error.
- `blk_get_stats`: no return value (writes through `out`).

### Example — file-system read pattern

```c
#include "blk.h"

/* Read the boot sector of device "hda" through the cache. */
void read_boot_sector(void) {
    int idx = blk_find_device("hda");
    if (idx < 0) return;

    u8 buf[512];
    if (blk_read_sectors(idx, 0, 1, buf) != 0) return;
    /* buf[510] == 0x55 && buf[511] == 0xAA for a valid MBR */
}
```

### Example — `mkfs`-style raw write

```c
/* Write a fresh MBR. Use _raw so we don't accidentally serve stale cached
 * data on the next read, and so we don't pollute the cache. */
void write_mbr(int dev_idx, const u8 mbr[512]) {
    blk_write_sectors_raw(dev_idx, 0, 1, mbr);
}
```

### Caveats

- **Cache is one-sector granularity**: even when `count > 1`, the cache loop processes one sector at a time. Multi-sector reads/writes from the underlying driver are not coalesced (yet).
- **Write-back, not write-through**: a successful `blk_write_sectors()` does NOT guarantee the data is on disk. Call `blk_cache_flush()` (or `blk_cache_flush_dev()`) before power-off or before unregistering a device.
- **`_raw` does not invalidate cached copies**: if a sector is in the cache and you `blk_write_sectors_raw()` it, a subsequent `blk_read_sectors()` may still return the stale cached copy. Use `_raw` only when you know the cache is cold (boot time, after a flush, or for sectors the cache never holds).
- **No scatter/gather**: `buf` must be a single contiguous buffer.
- **No async / IRQ-driven I/O**: all calls are synchronous and block until the driver completes.

---

## Interface 29: Partition table parse (MBR / GPT)

### Signature

```c
#include "part.h"

int part_parse    (int dev_idx, part_table_t *out);
int part_parse_mbr(int dev_idx, part_table_t *out);
int part_parse_gpt(int dev_idx, part_table_t *out);
```

### Purpose

Parse the on-disk partition table of a block device into an in-memory `part_table_t` structure. Both legacy MBR (4 primary entries, no GPT) and modern GPT (up to 128 entries) are supported.

`part_parse()` is the recommended entry point: it tries MBR first, and if the MBR contains a GPT protective entry (type `0xEE`), it transparently re-parses as GPT. `part_parse_mbr()` and `part_parse_gpt()` are convenience wrappers for callers that know which format to expect.

Both parsers use `blk_read_sectors_raw()` (the cache-bypassing variant) so they never pollute the cache with one-time boot-sector reads, and so they always see on-disk state.

### `part_table_t` and `partition_t`

```c
#define PART_MAX_PARTITIONS  16
#define PART_NAME_LEN        40

typedef enum {
    PART_TYPE_EMPTY = 0,
    PART_TYPE_MBR   = 1,
    PART_TYPE_GPT   = 2,
} part_table_type_t;

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
    part_table_type_t table_type;
    int              count;
    partition_t      parts[PART_MAX_PARTITIONS];
} part_table_t;
```

### Parameters

| Function | Description |
|---|---|
| `part_parse(dev_idx, out)` | Parse the partition table of device `dev_idx` into `*out`. Tries MBR first; if the MBR has a GPT protective entry, re-parses as GPT. Returns 0 on success, -1 on bad arg or I/O error. |
| `part_parse_mbr(dev_idx, out)` | Parse only the MBR. Sets `out->table_type = PART_TYPE_MBR`. Reads sector 0 raw; validates the `0xAA55` signature; iterates the 4 primary entries; skips type-0 entries. If a GPT protective entry (type `0xEE`) is found, transparently dispatches to `part_parse_gpt()`. Returns 0 on success, -1 on bad signature or I/O error. |
| `part_parse_gpt(dev_idx, out)` | Parse only the GPT. Sets `out->table_type = PART_TYPE_GPT`. Reads the GPT header at LBA 1, validates the `"EFI PART"` signature, reads up to 8 sectors of partition entries (enough for 128 entries), and copies up to `PART_MAX_PARTITIONS` used entries into `out->parts`. Returns 0 on success, -1 on bad signature or I/O error. |

### Return value

- 0 on success — `*out` is populated.
- -1 on bad arg, bad signature, or I/O error.

### Example

```c
#include "part.h"
#include "blk.h"

void part_demo(void) {
    int idx = blk_find_device("hda");
    if (idx < 0) return;

    part_table_t tbl;
    if (part_parse(idx, &tbl) != 0) {
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
#include "part.h"

partition_t *part_get_partition(part_table_t *tbl, int index);
void         part_print        (const part_table_t *tbl);
```

### Purpose

Once a `part_table_t` has been populated by `part_parse()`, these helpers let L1 index into it and pretty-print it.

`part_get_partition()` returns a pointer to the `index`-th partition (0-based), or NULL if `index` is out of range. Mutating the returned struct mutates the table — be careful.

`part_print()` writes a human-readable table to the console: partition number, start LBA, sector count, type byte, and name. Used by the `parted` shell command.

### Parameters

| Function | Description |
|---|---|
| `part_get_partition(tbl, index)` | Return a pointer to `tbl->parts[index]`, or NULL if `tbl` is NULL, `index < 0`, or `index >= tbl->count`. |
| `part_print(tbl)` | Print the partition table to the console in a fixed column format. No-op if `tbl` is NULL. |

### Return value

- `part_get_partition`: `partition_t *` on success, NULL on bad index.
- `part_print`: no return value.

### Example

```c
#include "part.h"
#include "console.h"

void part_query_demo(void) {
    int idx = blk_find_device("hda");
    if (idx < 0) return;

    part_table_t tbl;
    if (part_parse(idx, &tbl) != 0) return;

    /* Pretty-print the whole table. */
    part_print(&tbl);

    /* Index into it. */
    partition_t *p0 = part_get_partition(&tbl, 0);
    if (p0 && p0->present) {
        /* p0->start_lba is where partition 0 begins. */
        /* To mount a FAT32 file system on it, you would call
         * fat32_mount("hda", "/mnt/disk") and the FAT32 driver
         * would itself parse the partition table to find this LBA,
         * OR you can mount it directly via vfs_mount with the
         * partition-aware device name "hda1", "hda2", etc. */
    }
}
```

### Example console output of `part_print`

```
Partition table (GPT):
  #  Start          Sectors        Type  Name
  0  2048           1048576        0xEB  EFI System
  1  1050624        2095104        0x0C  Microsoft basic data
  2  3145728        4194304        0x83  Linux filesystem
```

### Caveats

- **Pointer aliasing**: `part_get_partition()` returns a pointer into the table, not a copy. Mutations are visible to subsequent callers.
- **No partition-name escaping**: `part_print` writes the raw `name` bytes to the console. Non-printable bytes in GPT names will appear as garbage.
- **No filtering**: `part_print` shows every `present` partition. There is no `--type` flag yet.

---

## Interface 31: Disk cache statistics

### Signature

```c
#include "blk_cache.h"

void blk_cache_init(void);
void blk_cache_get_stats(blk_cache_stats_t *out);

/* Cache primitives (used by blk_read_sectors / blk_write_sectors; also
 * callable directly by L1 if you need finer control). */
int blk_cache_read  (int dev_idx, u64 lba, void *buf);
int blk_cache_write (int dev_idx, u64 lba, const void *buf);
int blk_cache_insert(int dev_idx, u64 lba, const void *buf);
```

### Purpose

The disk-block cache is a 32 KiB LRU cache (64 slots × 512 bytes) sitting in front of every block device. It uses a write-back policy: writes update the cache and mark the block dirty; dirty blocks are written back to disk lazily (on eviction or on explicit `blk_cache_flush()`).

`blk_cache_get_stats()` returns cumulative counters that are useful for performance tuning and for the `cachestat` shell command:

- `hits` — cache hits on read.
- `misses` — cache misses on read (each miss triggers a 1-sector raw read).
- `evicts` — number of slots reclaimed by LRU eviction.
- `writebacks` — number of dirty blocks written back to disk.
- `used` — number of slots currently in use (0..64).

The cache primitives `blk_cache_read` / `blk_cache_write` / `blk_cache_insert` are exposed for L1 code that wants finer control than `blk_read_sectors` / `blk_write_sectors` offer — for example, a file system that wants to insert a freshly-read cluster into the cache without re-reading it through the block layer. Most L1 code should use the higher-level `blk_read_sectors` / `blk_write_sectors` instead.

### `blk_cache_slot_t` and `blk_cache_stats_t`

```c
#define BLK_CACHE_SLOTS  64   /* 64 * 512 = 32 KiB cache */

typedef struct {
    int  dev_idx;        /* -1 = empty slot */
    u64  lba;
    u8   data[512];
    u8   dirty;          /* 1 = needs write-back */
    u64  last_used;      /* tick count for LRU */
} blk_cache_slot_t;

typedef struct {
    u64 hits;
    u64 misses;
    u64 evicts;
    u64 writebacks;
    u32 used;            /* slots in use */
} blk_cache_stats_t;
```

### Parameters

| Function | Description |
|---|---|
| `blk_cache_init()` | Zero the cache (all slots `dev_idx = -1`, `dirty = 0`). Call once at boot before any block I/O. No return value. |
| `blk_cache_get_stats(out)` | Fill `*out` with the cumulative cache counters. No return value. |
| `blk_cache_read(dev_idx, lba, buf)` | Look up sector `(dev_idx, lba)` in the cache. On a hit, copy 512 bytes into `buf` and return 0. On a miss, return -1 (caller should fall back to a raw read and then `blk_cache_insert()`). |
| `blk_cache_write(dev_idx, lba, buf)` | Copy 512 bytes from `buf` into the cache slot for `(dev_idx, lba)`, marking it dirty. Allocates a slot (evicting the LRU entry, writing it back if dirty) if needed. Returns 0 on success, -1 on error. |
| `blk_cache_insert(dev_idx, lba, buf)` | Insert a freshly-read sector into the cache as a clean (non-dirty) entry. Useful after a raw read to warm the cache. Returns 0 on success, -1 on error. |

### Return value

- `blk_cache_init`, `blk_cache_get_stats`: no return value.
- `blk_cache_read`: 0 on hit, -1 on miss.
- `blk_cache_write`, `blk_cache_insert`: 0 on success, -1 on error.

### Example

```c
#include "blk_cache.h"

void cache_stats_demo(void) {
    blk_cache_stats_t st;
    blk_cache_get_stats(&st);

    /* st.hits, st.misses, st.evicts, st.writebacks, st.used */
    /* A healthy system has hits >> misses; if misses dominate,
     * the working set is larger than the 32 KiB cache. */
}
```

### Example — warming the cache after a raw read

```c
u8 buf[512];
if (blk_read_sectors_raw(dev_idx, lba, 1, buf) == 0) {
    blk_cache_insert(dev_idx, lba, buf);   /* don't re-read on next access */
}
```

### Caveats

- **32 KiB cache size is small**: with 64 slots, the working set of a multi-file file-system operation may exceed the cache. Tuning `BLK_CACHE_SLOTS` is a compile-time change (ABI-affecting).
- **Per-sector granularity**: the cache does not coalesce adjacent sectors into a single slot. Reading a 4 KiB cluster occupies 8 slots.
- **No per-device partitioning**: all devices share the same 64-slot cache. A hot ATA device can evict a hot virtio-blk device's blocks.
- **`blk_cache_write` does not write through to disk**: the data is in the cache only. It will be written back lazily — call `blk_cache_flush()` if you need it on disk now.

---

## Interface 32: Disk cache flush

### Signature

```c
#include "blk_cache.h"

int blk_cache_flush    (void);
int blk_cache_flush_dev(int dev_idx);
```

### Purpose

Write back all dirty cache blocks to the underlying block device.

- `blk_cache_flush()` flushes every dirty block in the cache, regardless of which device it belongs to. Use this before power-off, before a clean unmount of all file systems, or before a snapshot / checkpoint.
- `blk_cache_flush_dev(dev_idx)` flushes only the dirty blocks belonging to `dev_idx`. Use this before unmounting a single file system, before unregistering a device, or before any operation that needs on-disk consistency for one device.

Both functions walk the cache, find dirty slots, write each one back via `blk_write_sectors_raw()` (which bypasses the cache — otherwise it would recurse), and clear the dirty bit.

These are **synchronous** calls. They block until all dirty blocks are written.

### Parameters

| Function | Description |
|---|---|
| `blk_cache_flush()` | Walk every cache slot; for each dirty slot, write its 512 bytes back to its device via `blk_write_sectors_raw()`, then clear the dirty bit. Returns 0 on success, -1 if any write-back failed (the function continues past failures and returns -1 if any occurred). |
| `blk_cache_flush_dev(dev_idx)` | Same, but only for slots whose `dev_idx` matches. Returns 0 on success, -1 on bad `dev_idx` or any write-back failure. |

### Return value

- 0 on success (all dirty blocks written back, all dirty bits cleared).
- -1 if any write-back failed (the function still attempts the remaining slots).

### Example

```c
#include "blk_cache.h"

void clean_shutdown(void) {
    /* Flush all file systems' dirty blocks to disk. */
    if (blk_cache_flush() != 0) {
        /* log a warning — some block failed to write */
    }

    /* Now safe to power off. */
}

void unmount_one(int dev_idx) {
    /* Flush just this device's dirty blocks. */
    blk_cache_flush_dev(dev_idx);

    /* Safe to unregister the device. */
    blk_unregister_device(dev_idx);
}
```

### Example — `sync` shell command pattern

```c
/* The `sync` shell command is essentially: */
int cmd_sync(const char *args) {
    (void)args;
    int rc = blk_cache_flush();
    oc_console_puts(rc == 0 ? "sync: ok\n" : "sync: errors\n");
    return rc;
}
```

### Caveats

- **Synchronous**: blocks until all dirty blocks are written. On a slow ATA device with many dirty blocks, this can take hundreds of milliseconds.
- **No batching**: each dirty sector is written back as a separate 1-sector `blk_write_sectors_raw()` call. There is no coalescing of adjacent dirty sectors into multi-sector writes (future WP).
- **Failure handling**: if a write-back fails, the dirty bit is **not** cleared — the block stays dirty and will be retried on the next flush. The function returns -1 but continues with the remaining blocks.
- **Not IRQ-safe**: `blk_cache_flush()` calls `blk_write_sectors_raw()` which calls into the driver, which may sleep on the device. Do not call from an IRQ handler.
- **Does not invalidate clean entries**: flushing only writes back dirty blocks. Clean cached blocks remain in the cache. To invalidate the whole cache, call `blk_cache_init()` (which loses any dirty data — flush first).

---

## ABI Stability

All WP-07 functions, structs, and their typedefs are frozen:

- **Block device register / unregister (item 27)**: `blk_init`, `blk_register_device`, `blk_unregister_device`, `blk_find_device`, `blk_get_device`, `blk_list_devices`, `blk_num_devices` — signatures frozen. The `blk_device_t` struct layout, `blk_ops_t` struct layout, `blk_type_t` enum, and `blk_stats_t` struct layout are frozen. Capacity limits frozen: `BLK_MAX_DEVICES = 8`, `BLK_SECTOR_SIZE = 512`, `BLK_DEV_NAME_LEN = 32`.
- **Block I/O (item 28)**: `blk_read_sectors`, `blk_write_sectors`, `blk_read_sectors_raw`, `blk_write_sectors_raw`, `blk_get_stats` — signatures frozen. The 512-byte sector size and LBA addressing convention are frozen.
- **Partition parse (item 29)**: `part_parse`, `part_parse_mbr`, `part_parse_gpt` — signatures frozen. The `partition_t` struct layout, `part_table_t` struct layout, and `part_table_type_t` enum are frozen. Capacity limits frozen: `PART_MAX_PARTITIONS = 16`, `PART_NAME_LEN = 40`.
- **Partition query (item 30)**: `part_get_partition`, `part_print` — signatures frozen.
- **Disk cache stats (item 31)**: `blk_cache_init`, `blk_cache_get_stats`, `blk_cache_read`, `blk_cache_write`, `blk_cache_insert` — signatures frozen. The `blk_cache_slot_t` struct layout, `blk_cache_stats_t` struct layout, and `BLK_CACHE_SLOTS = 64` constant are frozen.
- **Disk cache flush (item 32)**: `blk_cache_flush`, `blk_cache_flush_dev` — signatures frozen.

The `blk_device_t.priv` pointer is driver-private and explicitly **not** part of the ABI — its layout depends on the registered driver. The `blk_ops_t.read` / `blk_ops_t.write` function-pointer signatures are part of the ABI (drivers must match them exactly).

The struct layouts are frozen with respect to the fields documented above. New fields may be appended at the end in future WPs; callers must zero-initialise structs before passing them in to allow this.

Future WPs may add new functions (e.g. `blk_register_irq_handler`, `blk_async_read`, `part_create`, `part_delete`, `blk_cache_resize`) but will not change existing ones incompatibly.

## Loading model

WP-07 still does not have a dynamic module loader — L1 is linked into the same binary as L0. The extension API is designed to survive the transition to loadable modules unchanged: a loadable block driver would just call `blk_register_device()` from its module-init function, and a loadable file-system driver would discover partitions via `part_parse()` and mount them via the WP-05 `vfs_mount()` API.
