<!-- SPDX-License-Identifier: Apache-2.0 -->
# Driver model: self-registration, families, and probe order

The driver layer is `ocos/drivers/` (11 entries: 9 class folders plus
the `drv_framework.{c,h}` pair). The model is the NAK architecture's
"everything is extensible by adding a file": a hardware driver
declares its identity **in the file that implements it**, and the
service-side family managers find it at boot. The syscall/service side
of the story is [syscall-ring3.md](syscall-ring3.md); per-family
details are under [../drivers/](../drivers/index.md).

## The registration mechanism (`drv_framework.{c,h}`)

```c
/* drv_framework.h — verbatim */
typedef struct {
    const char *family;
    const void *ops;        /* const drv_*_ops_t* (typed per family) */
    const char *name;
    const char *desc;
} oc_driver_decl_t;

#define OC_DRIVER_DECL(fam, nm, opsp, dsc) \
    static const oc_driver_decl_t OC_DECL_UID(__ocdv_) \
    __attribute__((used, section(".oc_drvs"))) = { (fam), (opsp), (nm), (dsc) }
```

`OC_DRIVER_DECL` places a const descriptor into the `.oc_drvs` linker
section (boundaries `__oc_drvs_start/_end` exported by
`linker.ld`); at boot `drv_framework_collect()` walks the section into
a runtime table of **up to 256 entries** and kmain logs
`drivers: file-local OC_DRIVER_DECL collection ready`. Adding a driver
= one `.c` file in the right `ocos/drivers/<class>/` folder + one
`OC_DRIVER_DECL` line - no global table edit, no registration call
site, no core file change.

Families in use: `block`, `nic`, `snd`, `usb`, `wireless`, `sdio`,
`placeholder` (the last is a live test of the mechanism - see below).
The sibling mechanisms `.oc_svcs` (services) and `.oc_apps` (L1
programs) work identically
([hidden-mechanisms.md](hidden-mechanisms.md)).

## The family ops structures (verbatim)

**Block** (`drv_storage_api.h`) - the unified device API every storage
driver exposes; fs drivers and the partition parser talk to this
layer, never to a driver:

```c
#define BLK_MAX_DEVICES   8
#define BLK_SECTOR_SIZE   512

typedef enum {
    BLK_TYPE_ATA    = 1,
    BLK_TYPE_VIRTIO = 2,
    BLK_TYPE_NVME   = 3,
    BLK_TYPE_AHCI   = 4,
    BLK_TYPE_USB    = 5,   /* WP-10d: USB Mass Storage (BOT + SCSI) */
    BLK_TYPE_SCSI   = 6,   /* WP-10-camera_misc: LSI 53C895A HBA    */
} driver_block_type_t;

typedef struct driver_block_ops {
    int (*read)(driver_block_device_t *dev, u64 lba, u32 count, void *buf);
    int (*write)(driver_block_device_t *dev, u64 lba, u32 count, const void *buf);
    int (*flush)(driver_block_device_t *dev);
} driver_block_ops_t;

struct driver_block_device {
    char     name[BLK_DEV_NAME_LEN];   /* e.g. "sda", "hda", "vda", "nvme0" */
    driver_block_type_t type;
    u64      sectors;
    u32      sector_size;
    u8       present;
    u8       bus, dev, func;           /* PCI address (for virtio/nvme) */
    const driver_block_ops_t *ops;
    void    *priv;
    int      index;
};

typedef struct {
    int (*probe)(void);   /* 0 = at least one device was added */
} blk_drv_ops_t;
```

Kernel-internal (not L1 surface since v2.0.0): `blk_dev_add()`,
`blk_dev_add_ptr()`, `blk_dev_remove()`, `blk_dev_set_ops()` (used by
ATA to **upgrade a PIO drive to Bus-Master DMA in place**),
`driver_block_find_device/get_device/num_devices`. On top: a 64-slot
LRU write-back cache (`svc_block_cache.c`) and the MBR/GPT partition
parser (`svc_block_part.c`) creating `<disk>pN` children with
**partition-bounds enforcement on both read and write** (BUG-0310).
Whole-disk protection for OCIRE adds the
`driver_block_read/write_sectors_trusted` kernel-only channel.

**NIC** (`drv_net_api.h`):

```c
typedef enum {
    NIC_TYPE_E1000E  = 1,  NIC_TYPE_IGB     = 2,  NIC_TYPE_IXGBE   = 3,
    NIC_TYPE_RTL8139 = 4,  NIC_TYPE_RTL8168 = 5,  NIC_TYPE_RTL8125 = 6,
    NIC_TYPE_RTL810X = 7,  NIC_TYPE_BCM57XX = 8,  NIC_TYPE_OTHER   = 9,
} driver_nic_type_t;

typedef struct driver_nic_ops {
    int (*send)(driver_nic_device_t *dev, const void *buf, int len);
    int (*recv)(driver_nic_device_t *dev, void *buf, int maxlen);
    int (*link_status)(driver_nic_device_t *dev);
    int (*get_mac)(driver_nic_device_t *dev, u8 mac[6]);
} driver_nic_ops_t;
```

(`NIC_MAX_DEVICES 8`.) **Sound** (`drv_audio_api.h`):

```c
typedef enum {
    SND_TYPE_HDA = 1,  SND_TYPE_AC97 = 2,  SND_TYPE_SB16 = 3,
    SND_TYPE_ES1370 = 4,  SND_TYPE_VIRTIO = 5,  SND_TYPE_USB_AUDIO = 6,
} driver_snd_type_t;

typedef struct driver_snd_ops {
    int (*play)(driver_snd_device_t *dev, const void *buf, int len);
    int (*stop)(driver_snd_device_t *dev);
    int (*set_rate)(driver_snd_device_t *dev, u32 rate);
    int (*set_volume)(driver_snd_device_t *dev, u32 vol);
    int (*get_caps)(driver_snd_device_t *dev, driver_snd_caps_t *caps);
} driver_snd_ops_t;
```

(`SND_MAX_DEVICES 8`; `driver_snd_caps_t` carries the rates bitmap,
channel range, bit widths, current rate/volume.) **USB host** (
`drv_usb_core.h`) carries `control/bulk/interrupt/iso_out/iso_in/ port_count/port_status/port_reset/poll` plus `bulk_max` (512 UHCI/OHCI,
4096 EHCI, 16 KiB XHCI) and the `USB_HOSTF_NATIVE_ADDR` flag (XHCI
does SET_ADDRESS itself). **Wireless** families declare
`wl_family_ops_t` (probe_pci/attach/fw_load/scan_start/tx_frame/
selftest). The **bus** family has no ops table - PCI/PCIe/ACPI/I2C are
called directly by everyone.

## Probe order - the hardcoded lists

The service-side family managers walk the collected table in fixed
name order (all in `ocos/services/*/src/svc_*_drivers.c`):

```c
/* block */
static const char *const order[] = {
    "ata_dma", "ata", "virtio_blk", "ahci", "nvme", "scsi",
};
/* nic */
static const char *const order[] = {
    "e1000e", "igb", "rtl8168", "rtl8125", "rtl810x",
    "rtl8139", "bcm57xx", "ixgbe", "other",
};
/* snd */
static const char *const s_snd_probe_order[] = {
    "hda", "ac97", "es1370", "usbaudio", "sb16", "virtio",
};
```

Block: BMDMA first (may register a drive with DMA ops; the PIO pass
then skips drives already taken). NIC: **first family that registers a
card wins** - single-NIC routing; wireless bridges in later as a NIC
device. Sound: unlike nic, **all** families are probed - multiple
cards coexist. USB: `driver_usb_probe_all()` probes all four host
controllers then enumerates root hubs; class drivers are collected
first. Boot bring-up order (kmain): block -> SCSI LSI -> USB ->
partition scan -> A/B -> wireless framework -> SDHCI/SDIO -> legacy
ports -> LAPIC -> net stack -> audio.

## The declaration census

| Family | Declarations |
|---|---|
| block | `ata` "ATA/ATAPI PIO (legacy ISA IDE)", `ata_dma` "ATA Bus-Master DMA (PCI IDE)", `ahci` "SATA AHCI", `nvme` "NVMe SSD", `virtio_blk` "virtio-blk storage", `scsi` "SCSI disk (virtio-scsi passthrough)" |
| nic | `e1000e`, `igb`, `ixgbe`, `rtl8168` "Realtek 8168/8111", `rtl8125`, `rtl810x`, `rtl8139`, `bcm57xx` "Broadcom 57xx (tg3)", `other` "other/virtual NICs" |
| snd | `hda` "Intel HD Audio", `ac97`, `es1370`, `sb16`, `virtio`, `usbaudio` (dual-declared under usb too) |
| usb | `hid-kbd`, `hid-mouse`, `usb-msc`, `cdc-acm`, `ftdi-serial`, `usb-audio`, `uvc`, `btusb` |
| wireless | `iwlwifi`, `ath9k/ath10k/ath11k`, `b43`/`brcmfmac`, `rtlwifi` |
| sdio | `brcmfmac-sdio` |
| placeholder | `placeholder` - "NAK registration-mechanism test driver (collected, never probed)" |

The bus/time/input/misc classes (PCI, PCIe, ACPI+AML, i2c/i801/
smbus/DesignWare, HPET/TSC/PM-timer/RTC, PS/2 keyboard, serial RX
bridge, i2c-hid touchpad, Bluetooth stack, camera, IR, parallel,
gameport, MJPEG) are called directly - they predate or sit outside the
family-manager pattern; `ocos/drivers/display/` is a README marker
reserving the NAK slot for future GPU drivers (the display pipeline is
the firmware-framebuffer console in `ocos/kernel/core_console_*` -
[../drivers/display.md](../drivers/display.md)).

## Cross-cutting engineering patterns

- **Polling everywhere for storage/NIC/USB**; real IRQs only for PS/2
  kbd (IRQ1), COM1 (IRQ4), COM2 H4 (IRQ3), audio cards (PCI INTx +
  SB16 IRQ5), HPET legacy timer1 and RTC (IRQ8). MSI/MSI-X is
  programmed and testable (`msitest` arms a real e1000 MSI) but unused
  by driver data paths.
- **Identity mapping assumption**: drivers treat `phys == virt` for
  DMA structures and over-allocate + page-align from the heap
  ([memory.md](memory.md) for the constraints).
- **Honest-state discipline**: every datasheet-only driver (ixgbe,
  rtl8169 family, bcm57xx, virtio-snd, i2c-hid on QEMU) states in its
  header that it was never exercised on real hardware; the test
  commands report SKIP rather than fake output.
- **BUG-numbered inline errata**: fixes cite QEMU source files and
  Linux version-tagged headers as evidence - the repo's substitute for
  a formal driver errata doc
  ([hidden-mechanisms.md](hidden-mechanisms.md) collects the biggest).
- **Per-driver `print_state()` dumps**: every major driver exports a
  register/state dump consumed by the L1 status commands (`ahci`,
  `nvme`, `e1000e`, `hda`, `usbdev`, ...) and the `*_test` batteries -
  the L1 test surface calls the same entry points the boot path uses.
