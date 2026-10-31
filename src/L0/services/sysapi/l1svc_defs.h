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
/* v2.0.0 group N: camera/usb service categories (appended). */
#define L1SVC_CAT_CAMERA   16   /* services/camera  */
#define L1SVC_CAT_USB      17   /* services/usb      */
/* 3.0.0-WP03 G4: the OCIRE embed channel (services/ocembed).  The
 * numbering is a VERBATIM CONTRACT with the OCIRE log agent
 * (ocire/agent/ocire_agent.c compiles against these numbers - an
 * ABI, not a source dependency): cat 18, ops 4/5 guard-only. */
#define L1SVC_CAT_OCIRE    18   /* services/ocembed  */
/* 3.0.0-WP04: the diagnostics home - the v2.2.0 chipset/memory/crash
 * diagnostic commands that fit none of the nine root-service classes.
 * Category string equals the services/diag folder name. */
#define L1SVC_CAT_DIAG     19   /* services/diag     */
/* 3.0.0-WP04 G11c: the full-screen TUI family (v2.2.0 tui/), revived
 * as the services/tui service.  Category string equals the folder
 * name (the category-number rule). */
#define L1SVC_CAT_TUI      20   /* services/tui      */

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
     (n) == L1SVC_CAT_INPUT    ? "input"    : \
     (n) == L1SVC_CAT_CAMERA   ? "camera"   : \
     (n) == L1SVC_CAT_USB      ? "usb"      : \
     (n) == L1SVC_CAT_OCIRE    ? "ocire"    : \
     (n) == L1SVC_CAT_DIAG     ? "diag"     : \
     (n) == L1SVC_CAT_TUI      ? "tui"      : 0)

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
/* 3.0.0-WP04 G11b: the IME pair is REVIVED (engine back in
 * services/display/ime_engine.c, bodies in disp_cmds_cn.c).
 * The numbers keep their v2.2.0 places - WP-03 retired them
 * without renumbering, and G11b re-occupies them exactly. */
#define L1SVC_DISP_IME        11   /* ime [-i]                */
#define L1SVC_DISP_IME_TEST   12   /* ime-test                */

/* ---- input operations (services/input) -----------------------------
 * Bodies: recovery/shell_cmds_chipset.c (touchpad/touchtest). */
#define L1SVC_INP_TOUCHPAD     1   /* touchpad                */
#define L1SVC_INP_TOUCHTEST    2   /* touchtest               */

/* ---- camera operations (services/camera) ---------------------------
 * v2.0.0 group N: the camera + legacy-port diagnostics family (the
 * v1 "camera" command class: UVC camera, SCSI, gameport, parallel
 * port, IR).  Bodies stay in their v2 homes (recovery/
 * shell_cmds_camera.c, shell_cmds_scsi.c, shell_cmds_pargame.c,
 * shell_cmds_ir.c - all exported via their headers), referenced by
 * the op table. */
#define L1SVC_CAM_UVC           1   /* uvc                     */
#define L1SVC_CAM_UVCTEST       2   /* uvctest                 */
#define L1SVC_CAM_SCSI          3   /* scsi                    */
#define L1SVC_CAM_SCSITEST      4   /* scsitest                */
#define L1SVC_CAM_GAMEPORT      5   /* gameport                */
#define L1SVC_CAM_GAMEPORTTEST  6   /* gameporttest            */
#define L1SVC_CAM_PARALLEL      7   /* parallel                */
#define L1SVC_CAM_PARALLELTEST  8   /* paralleltest            */
#define L1SVC_CAM_IR            9   /* ir                      */
#define L1SVC_CAM_IRTEST       10   /* irtest                  */

/* ---- usb operations (services/usb) ----------------------------------
 * v2.0.0 group N: the USB family (host/device status + class driver
 * tests).  Bodies stay in recovery/shell_cmds_usb_test.c (exported
 * by group N; the poll thread stays kernel-side and is started by
 * the shell module init). */
#define L1SVC_USB_STATUS        1   /* usb                     */
#define L1SVC_USB_DEV           2   /* usbdev [slot]           */
#define L1SVC_USB_CORE_TEST     3   /* usb_core_test           */
#define L1SVC_USB_KBD_TEST      4   /* usb_kbd_test            */
#define L1SVC_USB_MOUSE_TEST    5   /* usb_mouse_test          */
#define L1SVC_USB_STORAGE_TEST  6   /* usb_storage_test        */
#define L1SVC_USB_SERIAL_TEST   7   /* usb_serial_test         */
#define L1SVC_USB_HOTPLUG_TEST  8   /* usb_hotplug_test [...]  */
#define L1SVC_USB_HUB_TEST      9   /* usb_hub_test            */

/* ---- net operations (services/net) ----------------------------------
 * v2.0.0 group O: the diagnostic half of the v1 net command class.
 * ifconfig/ping/netstat/wget stay L0-declared: the ush built-ins of
 * the same names reach them through the SYS_NETCMD bridge, which
 * executes the L0 command line verbatim.  ssh/sshd stay L0 as
 * interactive-session commands (request/response L1SVC semantics do
 * not map to a bidirectional shell channel). */
#define L1SVC_NET_ARP           1   /* arp                     */
#define L1SVC_NET_DHCP          2   /* dhcp                    */
#define L1SVC_NET_DNS           3   /* dns <host>              */
#define L1SVC_NET_DNSTEST       4   /* dnstest                 */
#define L1SVC_NET_FIREWALL      5   /* firewall [...]          */
#define L1SVC_NET_IP            6   /* ip                      */
#define L1SVC_NET_LSPCI         7   /* lspci                   */
#define L1SVC_NET_NF_TEST       8   /* nf_test                 */
#define L1SVC_NET_ROUTE         9   /* route                   */
#define L1SVC_NET_TCPCC_TEST   10   /* tcpcc_test              */
#define L1SVC_NET_TCPSTATS     11   /* tcpstats                */
#define L1SVC_NET_TCPTEST      12   /* tcptest                 */
/* 3.0.0-WP04 G11a: the ssh / sshd commands return as ring-3 apps.
 * The protocol engines (net_ssh.c / net_sshd.c, resurrected verbatim
 * from v2.2.0) stay L0-side next to the TCP stack; the apps reach
 * them through these two ops.  sshd's interactive session prints
 * console-direct through the engine's own logging. */
#define L1SVC_NET_SSH           13   /* ssh <ip> [port] [user] [pass] */
#define L1SVC_NET_SSHD         14   /* sshd [port] [user] [pass]    */

/* ---- crypto operations (services/crypto) ------------------------------
 * v2.0.0 group O: the self-test family (NIST vectors + DH modexp
 * goldens).  ssh/sshd are interactive and stay in L0. */
#define L1SVC_CRYPTO_CRYPTOTEST 1   /* cryptotest              */
#define L1SVC_CRYPTO_DHTEST     2   /* dhtest                  */

/* ---- ocire operations (services/ocembed) ------------------------------
 * 3.0.0-WP03 G4: the OCIRE embed channel, consumed by the injected
 * log agent ONLY.  Both ops check the caller's ocire_guard process
 * flag - any other ring-3 caller is refused (there is deliberately
 * no code path that could turn the agent into a general "access
 * OCIRE" door).  Numbering per the agent contract (verbatim). */
#define L1SVC_OCIRE_LOG_APPEND    4   /* guard-only: append text to the
                                      * boot-log ring on the OCIRE
                                      * volume (arg = payload)      */
#define L1SVC_OCIRE_KLOG_SNAPSHOT 5   /* guard-only: kernel log ring
                                      * text into out (cap)         */

/* ---- health operations (services/health) ------------------------------
 * 3.0.0-WP04: the v2.2.0 L0 command `health` (with its `watch` and
 * `export` subcommands) migrates to L1.  The category number 9 has
 * been reserved since v2.0.0; these are its first ops.  SNAPSHOT
 * renders the fresh-sample health report; EXPORT writes the JSON to
 * arg (default /var/log/health.json); WATCH runs the interactive
 * 1 s refresh loop (console output, ENTER exits - the v2.2.0 body). */
#define L1SVC_HEALTH_SNAPSHOT 1   /* health                  */
#define L1SVC_HEALTH_EXPORT   2   /* health export [path]    */
#define L1SVC_HEALTH_WATCH    3   /* health watch (loop, ENTER exits) */

/* ---- klog operations (services/klog) -----------------------------------
 * 3.0.0-WP04: dmesg / logcat / logctl migrate to L1 (category 10,
 * reserved since v2.0.0).  DMESG takes "[N]" in arg (default 30);
 * LOGCAT takes the full v2.2.0 option string (-l -m -k -t -n -f);
 * LOGCTL takes the subcommand line (status|level|enable|disable|
 * clear|flush|rotate|export|filter).  Interactive follow (-f) runs
 * the v2.2.0 loop console-side inside the op. */
#define L1SVC_KLOG_DMESG  1   /* dmesg [N]                    */
#define L1SVC_KLOG_LOGCAT 2   /* logcat -l lvl -m mod -k key -t sec -n N [-f] */
#define L1SVC_KLOG_LOGCTL 3   /* logctl status|level|...      */

/* ---- watchdog operations (services/watchdog) ---------------------------
 * 3.0.0-WP04: the v2.2.0 `watchdog` command migrates to L1.  Category
 * 11 has been reserved since v2.0.0; the op carries the whole
 * subcommand line in arg (status|slots|hw ...|start|stop|feed|
 * timeout|register|test) - the subcommand words are arguments of the
 * ONE registered command name, dispatched by the service body exactly
 * as the v2.2.0 shell command did. */
#define L1SVC_WDT_CMD 1     /* watchdog [subcommand ...]     */

/* ---- l1 operations (services/l1) ---------------------------------------
 * 3.0.0-WP04: the L1-domain commands migrate to ring 3 under their
 * long-reserved category 13 ("l1").  The folder services/l1/ is
 * rebuilt as the category's face: it consumes the loader/IPC
 * mechanisms (services/loader, services/ipc) and the generated
 * OC_USERPROGS registry - the v2.2.0 hardcoded program tables (run's
 * 30-name if-else, ldd's 20-name chain) are gone; every registered
 * app is reachable by name.
 *   APPS   - the registry listing (name lines, A-Z, with the count):
 *            the help/run surface; the generator emits it sorted, the
 *            help_*_test apps assert that invariant.
 *   L1TEST - the v2.2.0 l1test body: job_create/job_list/job_control
 *            (steps 1-6), ext_wp8b self-test (step 7), the CPL0
 *            int-0x80 wrapper smoke (step 8).  The v2.2.0 steps 9/10
 *            (g7test/g8test sched+clock regressions) do not exist in
 *            3.0.0 (their suites were removed with the L0 shell) and
 *            are dropped with this declaration.
 *   LDD    - the v2.2.0 ldd body over the registry: PT_INTERP +
 *            DT_NEEDED introspection of a registered program. */
#define L1SVC_L1_APPS   1   /* registry listing; fix1: arg selects
                                 * the mode - "" plain A-Z listing,
                                 * "-a" full A-Z with wp tags, "-w"
                                 * grouped by work package */
#define L1SVC_L1_L1TEST 2   /* l1test (job + wp8b + CPL0 smoke)  */
#define L1SVC_L1_LDD    3   /* ldd <program>                     */

/* ---- diag operations (services/diag) -------------------------------------
 * 3.0.0-WP04: the v2.2.0 chipset/memory/crash diagnostics migrate to L1
 * under ONE new category "diag" (the WP-03 9-class root services had
 * no home for them; the category string equals the services/diag
 * folder name).  Every op maps 1:1 to a former command - numbered,
 * no string dispatch.  The full numbering is frozen here up front
 * (append-only ABI); the three migration groups fill the bodies:
 *   G8  1-8   vitals (mem/frag/heap/sched/syncstat) + crash family
 *   G9  9-26  the ring-0 self-test commands (cr3test..fstest)
 *   G10 27-39 the chipset family (acpi, pci, i2c, smbus commands)
 * Note: crashtest's panic|exc|pf|assert|wd|ovf variants REALLY crash
 * the kernel (v2.2.0 behaviour); policy/dump/usage are safe. */
#define L1SVC_DIAG_MEM       1   /* mem        : PMM stats             */
#define L1SVC_DIAG_FRAG      2   /* frag       : PMM fragments        */
#define L1SVC_DIAG_HEAP      3   /* heap       : kernel heap stats     */
#define L1SVC_DIAG_SCHED     4   /* sched      : scheduler stats      */
#define L1SVC_DIAG_SYNCSTAT  5   /* syncstat   : sync primitive stats  */
#define L1SVC_DIAG_CRASHLOG  6   /* crashlog [flush]                   */
#define L1SVC_DIAG_CRASHTEST 7   /* crashtest panic|exc|...|policy ... */
#define L1SVC_DIAG_DUMPINFO  8   /* dumpinfo   : core dump status      */
/* G9 range (ring-0 self-tests) - reserved, bodies land with the group */
#define L1SVC_DIAG_CR3TEST   9
#define L1SVC_DIAG_EXC      10
#define L1SVC_DIAG_PFTEST   11
#define L1SVC_DIAG_PMMRACE  12
#define L1SVC_DIAG_VMTEST   13
#define L1SVC_DIAG_VMKERNELPT 14
#define L1SVC_DIAG_VMMAP    15
#define L1SVC_DIAG_MEMTEST  16
#define L1SVC_DIAG_HEAPTEST 17
#define L1SVC_DIAG_HEAPBOUNDS 18
#define L1SVC_DIAG_IRQABITEST 19
#define L1SVC_DIAG_TIMER    20
#define L1SVC_DIAG_MSITEST  21
#define L1SVC_DIAG_SPAWN    22
#define L1SVC_DIAG_MULTI    23
#define L1SVC_DIAG_NICE     24
#define L1SVC_DIAG_SYNCTEST 25
#define L1SVC_DIAG_FSTEST   26
/* G10 range (chipset) - reserved, bodies land with the group */
#define L1SVC_DIAG_ACPI      27
#define L1SVC_DIAG_ACPIDEV   28
#define L1SVC_DIAG_ACPIPOW   29
#define L1SVC_DIAG_ACPITEST  30
#define L1SVC_DIAG_PCIDUMP   31
#define L1SVC_DIAG_PCIBARS   32
#define L1SVC_DIAG_PCIE      33
#define L1SVC_DIAG_I2CDETECT 34
#define L1SVC_DIAG_I2CGET    35
#define L1SVC_DIAG_I2CSET    36
#define L1SVC_DIAG_I2CTEST   37
#define L1SVC_DIAG_SMBUS     38
#define L1SVC_DIAG_SMBUSTEST 39

/* ---- tui operations (services/tui) ---------------------------------
 * 3.0.0-WP04 G11c: the v2.2.0 tui command family (tui_cmd.c), revived
 * as its own service category.  Bodies: services/tui/tui_cmd.c (the
 * v2.2.0 shell_cmd_tui_* set, exported).  Eight full-screen ops route
 * live (direct call, see tui_l1.c); tui-test serves its verdict block
 * through the capture.  tui-drv keeps its v2.2.0 alias role (same body
 * as tui-hw, own op number). */
#define L1SVC_TUI_TUI        1   /* tui      (desktop)       */
#define L1SVC_TUI_TEST       2   /* tui-test (selftests)     */
#define L1SVC_TUI_HW         3   /* tui-hw   [page]          */
#define L1SVC_TUI_NET        4   /* tui-net                  */
#define L1SVC_TUI_SYSINFO    5   /* tui-sysinfo [page]       */
#define L1SVC_TUI_TIME       6   /* tui-time                 */
#define L1SVC_TUI_DRV        7   /* tui-drv   [page]         */
#define L1SVC_TUI_SYS        8   /* tui-sys   [page]         */
#define L1SVC_TUI_EXT        9   /* tui-ext                  */

#endif /* OC_L1SVC_DEFS_H */
