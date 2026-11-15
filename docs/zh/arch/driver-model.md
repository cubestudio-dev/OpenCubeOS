<!-- SPDX-License-Identifier: Apache-2.0 -->
# 驱动模型：自注册、家族与探测顺序

驱动层是 `ocos/drivers/`（11 个条目：9 个类别目录外加
`drv_framework.{c,h}` 一对）。该模型即 NAK 架构的“一切皆可通过
增加一个文件来扩展”：硬件驱动**在实现它的文件里**声明自己的身份，
服务侧的家族管理器在引导时找到它。故事的系统调用/服务一侧见
[syscall-ring3.md](syscall-ring3.md)；按家族的细节见
[../drivers/](../drivers/index.md)。

## 注册机制（`drv_framework.{c,h}`）

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

`OC_DRIVER_DECL` 把一个 const 描述符放进 `.oc_drvs` 链接段（边界
`__oc_drvs_start/_end` 由 `linker.ld` 导出）；引导时
`drv_framework_collect()` 遍历该段收进**最多 256 条目**的运行时表，
kmain 记录日志 `drivers: file-local OC_DRIVER_DECL collection ready`。
增加一个驱动 = 在正确的 `ocos/drivers/<class>/` 目录里加一个
`.c` 文件 + 一行 `OC_DRIVER_DECL`——无需改全局表、无需注册调用点、
无需改核心文件。

使用中的家族：`block`、`nic`、`snd`、`usb`、`wireless`、`sdio`、
`placeholder`（最后一个是对机制的有意实测——见下文）。兄弟机制
`.oc_svcs`（服务）与 `.oc_apps`（L1 程序）工作方式完全相同
（见 [hidden-mechanisms.md](hidden-mechanisms.md)）。

## 家族 ops 结构（逐字）

**Block**（`drv_storage_api.h`）——每个存储驱动都暴露的统一设备
API；fs 驱动与分区解析器只与本层对话，绝不直连驱动：

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

内核内部（自 v2.0.0 起不再是 L1 面）：`blk_dev_add()`、
`blk_dev_add_ptr()`、`blk_dev_remove()`、`blk_dev_set_ops()`（ATA
用它**把 PIO 驱动器原地升级为 Bus-Master DMA**）、
`driver_block_find_device/get_device/num_devices`。其上：64 槽位 LRU
写回缓存（`svc_block_cache.c`）与 MBR/GPT 分区解析器
（`svc_block_part.c`），后者创建 `<disk>pN` 子设备并对读写都施加
**分区边界强制**（BUG-0310）。面向 OCIRE 的整盘保护追加了
`driver_block_read/write_sectors_trusted` 仅内核通道。

**NIC**（`drv_net_api.h`）：

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

（`NIC_MAX_DEVICES 8`。）**Sound**（`drv_audio_api.h`）：

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

（`SND_MAX_DEVICES 8`；`driver_snd_caps_t` 携带速率位图、声道范围、
位宽、当前速率/音量。）**USB 主机**（`drv_usb_core.h`）携带
`control/bulk/interrupt/iso_out/iso_in/port_count/port_status/ port_reset/poll` 外加 `bulk_max`（512 UHCI/OHCI、4096 EHCI、
16 KiB XHCI）与 `USB_HOSTF_NATIVE_ADDR` 标志（XHCI 自行执行
SET_ADDRESS）。**Wireless** 家族声明 `wl_family_ops_t`
（probe_pci/attach/fw_load/scan_start/tx_frame/selftest）。**bus**
家族没有 ops 表——PCI/PCIe/ACPI/I2C 被所有人直接调用。

## 探测顺序——硬编码的清单

服务侧的家族管理器按固定的名字顺序遍历收集来的表（全部位于
`ocos/services/*/src/svc_*_drivers.c`）：

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

块设备：BMDMA 在先（可以带 DMA 操作集注册驱动器；PIO 遍随后跳过
已被接手的驱动器）。NIC：**第一个注册到网卡的家族获胜**——单 NIC
路由；无线随后作为 NIC 设备桥接进来。声音：与 nic 不同，**所有**
家族都会被探测——多卡共存。USB：`driver_usb_probe_all()` 探测全部
四个主机控制器再枚举根集线器；类驱动先被收集。引导 bring-up 顺序
（kmain）：block -> SCSI LSI -> USB -> 分区扫描 -> A/B -> 无线框架
-> SDHCI/SDIO -> 遗留端口 -> LAPIC -> 网络栈 -> 音频。

## 声明普查

| 家族 | 声明 |
|---|---|
| block | `ata` "ATA/ATAPI PIO (legacy ISA IDE)"、`ata_dma` "ATA Bus-Master DMA (PCI IDE)"、`ahci` "SATA AHCI"、`nvme` "NVMe SSD"、`virtio_blk` "virtio-blk storage"、`scsi` "SCSI disk (virtio-scsi passthrough)" |
| nic | `e1000e`、`igb`、`ixgbe`、`rtl8168` "Realtek 8168/8111"、`rtl8125`、`rtl810x`、`rtl8139`、`bcm57xx` "Broadcom 57xx (tg3)"、`other` "other/virtual NICs" |
| snd | `hda` "Intel HD Audio"、`ac97`、`es1370`、`sb16`、`virtio`、`usbaudio`（同时在 usb 下双重声明） |
| usb | `hid-kbd`、`hid-mouse`、`usb-msc`、`cdc-acm`、`ftdi-serial`、`usb-audio`、`uvc`、`btusb` |
| wireless | `iwlwifi`、`ath9k/ath10k/ath11k`、`b43`/`brcmfmac`、`rtlwifi` |
| sdio | `brcmfmac-sdio` |
| placeholder | `placeholder`——"NAK registration-mechanism test driver (collected, never probed)" |

bus/time/input/misc 类（PCI、PCIe、ACPI+AML、i2c/i801/smbus/
DesignWare、HPET/TSC/PM-timer/RTC、PS/2 键盘、串口 RX 桥、i2c-hid
触摸板、蓝牙栈、摄像头、红外、并口、游戏口、MJPEG）被直接调用
——它们早于或外在于家族管理器模式；`ocos/drivers/display/` 是一个
README 标记，为未来 GPU 驱动保留 NAK 槽位（显示流水线是
`ocos/kernel/core_console_*` 中的固件 framebuffer 控制台——见
[../drivers/display.md](../drivers/display.md)）。

## 横切的工程模式

- **存储/NIC/USB 一律轮询**；真正的 IRQ 只有 PS/2 键盘（IRQ1）、
  COM1（IRQ4）、COM2 H4（IRQ3）、音频卡（PCI INTx + SB16 IRQ5）、
  HPET legacy timer1 与 RTC（IRQ8）。MSI/MSI-X 可编程且可测
  （`msitest` 布防真实 e1000 MSI），但驱动数据路径不用它。
- **恒等映射假设**：驱动对 DMA 结构按 `phys == virt` 处理，并从堆
  超额分配 + 页对齐（约束见 [memory.md](memory.md)）。
- **诚实状态纪律**：每个纯数据手册驱动（ixgbe、rtl8169 家族、
  bcm57xx、virtio-snd、QEMU 上的 i2c-hid）都在其头部声明从未在
  真实硬件上演练；测试命令报告 SKIP 而非伪造输出。
- **BUG 编号的行内勘误**：修复以 QEMU 源文件与 Linux 版本标签的
  头文件为证据——这是仓库对正式驱动勘误文档的替代
  （[hidden-mechanisms.md](hidden-mechanisms.md) 收录了最大的几处）。
- **每驱动的 `print_state()` 转储**：每个主要驱动都导出一个寄存器/
  状态转储，供 L1 状态命令（`ahci`、`nvme`、`e1000e`、`hda`、
  `usbdev`……）与 `*_test` 测试组消费——L1 测试面调用的是引导路径
  所用的同一批入口点。
