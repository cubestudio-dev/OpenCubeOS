/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 2.0.0 - L1 service call surface numbering.
 *
 * SYS_L1SVC(110) routes a ring-3 call to the owning L0 service:
 *     syscall(SYS_L1SVC, cat, op, arg, out, cap)
 * `cat` selects the services/<category>/ folder (the category string
 * and the folder name are one-to-one by rule); `op` is the operation
 * inside that service; `arg` is a NUL-terminated user string (or 0);
 * `out/cap` receive a NUL-terminated text result.  Every op returns
 * plain ASCII text: the ring-3 side formats nothing, the kernel
 * service owns the data and its rendering.
 *
 * The kernel side includes this header; the ring-3 side (l1apps/ush.c)
 * carries a verbatim copy of the constant block below with a comment
 * pointing back here.  Renumber only by appending.
 */
#ifndef OC_L1SVC_DEFS_H
#define OC_L1SVC_DEFS_H

/* ---- Service categories (== services/ folder names) ---------------- */
#define L1SVC_CAT_TIME      1   /* services/time    */
#define L1SVC_CAT_BLOCK     2   /* services/block   */
#define L1SVC_CAT_NIC       3   /* services/nic     */
#define L1SVC_CAT_SND       4   /* services/snd     */
#define L1SVC_CAT_POWER     5   /* services/power   */
#define L1SVC_CAT_OTA       6   /* services/ota     */
#define L1SVC_CAT_FS        7   /* services/fs      */
#define L1SVC_CAT_NET       8   /* services/net     */
#define L1SVC_CAT_HEALTH    9   /* services/health  */
#define L1SVC_CAT_KLOG     10   /* services/klog    */
#define L1SVC_CAT_WATCHDOG 11   /* services/watchdog*/
#define L1SVC_CAT_CRYPTO   12   /* services/crypto  */
#define L1SVC_CAT_L1       13   /* services/l1      */
/* v2.0.0 group M: display/input service categories (appended). */
#define L1SVC_CAT_DISPLAY  14   /* services/display */
#define L1SVC_CAT_INPUT    15   /* services/input   */

/* Category number -> folder name (kernel side; used by SYS_L1SVC). */
#define L1SVC_CAT_NAME(n) \
    ((n) == L1SVC_CAT_TIME     ? "time"     : \
     (n) == L1SVC_CAT_BLOCK    ? "block"    : \
     (n) == L1SVC_CAT_NIC      ? "nic"      : \
     (n) == L1SVC_CAT_SND      ? "snd"      : \
     (n) == L1SVC_CAT_POWER    ? "power"    : \
     (n) == L1SVC_CAT_OTA      ? "ota"      : \
     (n) == L1SVC_CAT_FS       ? "fs"       : \
     (n) == L1SVC_CAT_NET      ? "net"      : \
     (n) == L1SVC_CAT_HEALTH   ? "health"   : \
     (n) == L1SVC_CAT_KLOG     ? "klog"     : \
     (n) == L1SVC_CAT_WATCHDOG ? "watchdog" : \
     (n) == L1SVC_CAT_CRYPTO   ? "crypto"   : \
     (n) == L1SVC_CAT_L1       ? "l1"       : \
     (n) == L1SVC_CAT_DISPLAY  ? "display"  : \
     (n) == L1SVC_CAT_INPUT    ? "input"    : 0)

/* ---- time operations (services/time) -------------------------------
 * Mirrors the v1 oc> commands rtc / hpet / pmtimer / tsc. */
#define L1SVC_TIME_RTC      1   /* rtc [uie N]        : CMOS RTC status  */
#define L1SVC_TIME_HPET     2   /* hpet               : HPET status      */
#define L1SVC_TIME_PMTIMER  3   /* pmtimer            : PM timer status  */
#define L1SVC_TIME_TSC      4   /* tsc                : TSC status       */

/* ---- fs operations (services/fs) -----------------------------------
 * Mirrors the v1 oc> commands mount / mounts / umount / fatmount /
 * fatstat / tree.  (cat/ls/cp/mv/rm/mkdir/... already run in ring 3
 * through the classic file syscalls; these six are the mount-table
 * operations the classic surface never exposed.) */
#define L1SVC_FS_MOUNT      1   /* mount [type dev p] : list or mount    */
#define L1SVC_FS_MOUNTS     2   /* mounts             : list (alias)     */
#define L1SVC_FS_UMOUNT     3   /* umount <path>      : unmount          */
#define L1SVC_FS_FATMOUNT   4   /* fatmount [dev p]   : mount FAT32      */
#define L1SVC_FS_FATSTAT    5   /* fatstat            : FAT32 stats      */
#define L1SVC_FS_TREE       6   /* tree [path]        : directory tree   */

/* ---- block operations (services/block) -----------------------------
 * Mirrors the v1 oc> block commands (23; `write` moved to the fs
 * surface as a ush builtin).  Command bodies live in
 * services/block/blk_cmds*.c, executed captured. */
#define L1SVC_BLK_AHCI          1   /* ahci                    */
#define L1SVC_BLK_NVME          2   /* nvme                    */
#define L1SVC_BLK_ATA           3   /* ata                     */
#define L1SVC_BLK_DSKSTAT       4   /* dskstat                 */
#define L1SVC_BLK_LSBLK         5   /* lsblk                   */
#define L1SVC_BLK_PARTED        6   /* parted [dev]            */
#define L1SVC_BLK_MKFS_FAT32    7   /* mkfs.fat32 <dev>        */
#define L1SVC_BLK_MKFS_EXFAT    8   /* mkfs.exfat <dev>        */
#define L1SVC_BLK_MKFS_EXT4     9   /* mkfs.ext4 <dev>         */
#define L1SVC_BLK_FSCK         10   /* fsck                    */
#define L1SVC_BLK_SYNC         11   /* sync                    */
#define L1SVC_BLK_AHCI_TEST    12   /* ahci_test               */
#define L1SVC_BLK_NVME_TEST    13   /* nvme_test               */
#define L1SVC_BLK_ATA_DMA_TEST 14   /* ata_dma_test            */
#define L1SVC_BLK_VIRTIO_TEST  15   /* virtio_blk_test         */
#define L1SVC_BLK_RW_TEST      16   /* disk_rw_test <dev>      */
#define L1SVC_BLK_PART_TEST    17   /* partition_test [dev]    */
#define L1SVC_BLK_FS_MOUNT_TEST 18  /* fs_mount_test <dev>     */
#define L1SVC_BLK_REAL_HW_TEST 19   /* real_hw_test            */
#define L1SVC_BLK_ABDISK       20   /* abdisk <dev>            */
#define L1SVC_BLK_INSTALL      21   /* install <dev>           */
#define L1SVC_BLK_GRUB_INSTALL 22   /* grub-install <dev>      */
#define L1SVC_BLK_ABCFG        23   /* abcfg                   */

/* ---- nic operations (services/nic) ---------------------------------
 * Mirrors the v1 oc> NIC commands (18; bodies in services/nic/
 * nic_cmds.c). */
#define L1SVC_NIC_E1000E        1   /* e1000e                  */
#define L1SVC_NIC_IGB           2   /* igb                     */
#define L1SVC_NIC_IXGBE         3   /* ixgbe                   */
#define L1SVC_NIC_RTL8139       4   /* rtl8139                 */
#define L1SVC_NIC_RTL8168       5   /* rtl8168                 */
#define L1SVC_NIC_RTL8125       6   /* rtl8125                 */
#define L1SVC_NIC_RTL810X       7   /* rtl810x                 */
#define L1SVC_NIC_BCM57XX       8   /* bcm57xx                 */
#define L1SVC_NIC_E1000E_TEST   9   /* e1000e_test             */
#define L1SVC_NIC_IGB_TEST     10   /* igb_test                */
#define L1SVC_NIC_IXGBE_TEST   11   /* ixgbe_test              */
#define L1SVC_NIC_RTL8139_TEST 12   /* rtl8139_test            */
#define L1SVC_NIC_RTL8168_TEST 13   /* rtl8168_test            */
#define L1SVC_NIC_RTL8125_TEST 14   /* rtl8125_test            */
#define L1SVC_NIC_RTL810X_TEST 15   /* rtl810x_test            */
#define L1SVC_NIC_BCM57XX_TEST 16   /* bcm57xx_test            */
#define L1SVC_NIC_OTHER_TEST   17   /* other_nic_test          */
#define L1SVC_NIC_RW_TEST      18   /* nic_rw_test             */
#define L1SVC_NIC_WIFI         19   /* wifi [status|scan|...]  */
#define L1SVC_NIC_BT           20   /* bt [status|usb|test]    */

/* ---- snd operations (services/snd) ---------------------------------
 * Mirrors the v1 oc> sound commands (17; bodies in services/snd/
 * snd_cmds.c). */
#define L1SVC_SND_SOUND        1   /* sound                   */
#define L1SVC_SND_HDA          2   /* hda                     */
#define L1SVC_SND_AC97         3   /* ac97                    */
#define L1SVC_SND_SB16         4   /* sb16                    */
#define L1SVC_SND_ES1370       5   /* es1370                  */
#define L1SVC_SND_VIRTIOSND    6   /* virtiosnd               */
#define L1SVC_SND_USBAUDIO     7   /* usbaudio                */
#define L1SVC_SND_PLAY         8   /* play <file>             */
#define L1SVC_SND_VOLUME       9   /* volume [0-100]          */
#define L1SVC_SND_HDA_TEST    10   /* hda_test                */
#define L1SVC_SND_AC97_TEST   11   /* ac97_test               */
#define L1SVC_SND_SB16_TEST   12   /* sb16_test               */
#define L1SVC_SND_ES1370_TEST 13   /* es1370_test             */
#define L1SVC_SND_VIRTIO_TEST 14   /* virtio_snd_test         */
#define L1SVC_SND_USB_TEST    15   /* usb_audio_test          */
#define L1SVC_SND_RW_TEST     16   /* audio_rw_test           */
#define L1SVC_SND_RATE_TEST   17   /* sample_rate_test        */

/* ---- ota operations (services/ota) ---------------------------------
 * Mirrors the v1 oc> update-family commands (17; `reboot` belongs to
 * the power surface).  Bodies live in services/ota/ota_ab.c,
 * ota_update.c, core/lib/lib_config.c (config family) and
 * services/ota/ota_cmds_test.c. */
#define L1SVC_OTA_CHECKUPDATE      1  /* checkupdate           */
#define L1SVC_OTA_CONFIG           2  /* config list|get|set   */
#define L1SVC_OTA_CONFIG_TEST      3  /* config_test           */
#define L1SVC_OTA_CHECKUPDATE_TEST 4  /* checkupdate_test      */
#define L1SVC_OTA_UPDATE           5  /* update [...]          */
#define L1SVC_OTA_ROLLBACK         6  /* rollback              */
#define L1SVC_OTA_PKG_TEST         7  /* update_pkg_test       */
#define L1SVC_OTA_AB_TEST          8  /* ab_partition_test     */
#define L1SVC_OTA_CHECK_TEST       9  /* update_check_test     */
#define L1SVC_OTA_DOWNLOAD_TEST   10  /* update_download_test  */
#define L1SVC_OTA_VERIFY_TEST     11  /* update_verify_test    */
#define L1SVC_OTA_INSTALL_TEST    12  /* update_install_test   */
#define L1SVC_OTA_ROLLBACK_TEST   13  /* update_rollback_test  */
#define L1SVC_OTA_LOCAL_TEST      14  /* update_local_test     */
#define L1SVC_OTA_STATUS_TEST     15  /* update_status_test    */
#define L1SVC_OTA_REAL_TEST       16  /* real_update_test      */

/* ---- power operations (services/power) -----------------------------
 * L0 keeps the survival controls (halt shutdown poweroff reboot) for
 * the recovery environment; the SAME four + the full family are the
 * L1 surface (minimal-L0 + full-L1 rule).  Bodies:
 * services/power/core_power.c, core/main.c (halt), pwr_cmds_test.c. */
#define L1SVC_PWR_SHUTDOWN        1  /* shutdown / poweroff   */
#define L1SVC_PWR_REBOOT          2  /* reboot                */
#define L1SVC_PWR_HALT            3  /* halt                  */
#define L1SVC_PWR_SLEEP           4  /* sleep <sec>           */
#define L1SVC_PWR_POWEROFF_TEST   5  /* poweroff_test         */
#define L1SVC_PWR_SUSPEND_TEST    6  /* suspend_test          */
#define L1SVC_PWR_HALT_TEST       7  /* halt_test             */
#define L1SVC_PWR_REBOOT_TEST     8  /* reboot_test           */

/* ---- display operations (services/display) ------------------------
 * The rendering/locale family.  Bodies: chinese/chinese_cmd.c
 * (locale), recovery/shell_lang.c (lang), core/main.c
 * (stats/uname/clear).  The full-screen TUI stays in L0 (it drives
 * the kernel console directly); chipset diagnostics (acpi/pci/i2c)
 * stay in L0 as recovery-env tooling. */
#define L1SVC_DISP_STATS       1   /* stats                   */
#define L1SVC_DISP_UNAME       2   /* uname [-a|-s|-r|-m]     */
#define L1SVC_DISP_CLEAR       3   /* clear                   */
#define L1SVC_DISP_LANG        4   /* lang [en|zh]            */
#define L1SVC_DISP_CHINESE     5   /* chinese                 */
#define L1SVC_DISP_CHINESETEST 6   /* chinesetest             */
#define L1SVC_DISP_FONT        7   /* font                    */
#define L1SVC_DISP_FONT_TEST   8   /* font-test               */
#define L1SVC_DISP_ENCODING    9   /* encoding                */
#define L1SVC_DISP_ENC_TEST   10   /* encoding-test           */
#define L1SVC_DISP_IME        11   /* ime                     */
#define L1SVC_DISP_IME_TEST   12   /* ime-test                */

/* ---- input operations (services/input) -----------------------------
 * Bodies: recovery/shell_cmds_chipset.c (touchpad/touchtest). */
#define L1SVC_INP_TOUCHPAD     1   /* touchpad                */
#define L1SVC_INP_TOUCHTEST    2   /* touchtest               */

#endif /* OC_L1SVC_DEFS_H */
