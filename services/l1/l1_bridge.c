/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 2.0.0 - services/l1 desktop SVC bridge.
 *
 * v2.0.0 moved 133 shell commands from the L0 recovery shell into the
 * ring-3 ush service surface (SYS_L1SVC).  The L0 TUI desktop kept its
 * v1 bridge pages (Panel > Storage > mounts/fsck, Panel > Network >
 * dhcp/ip/route/dns/firewall/tcpstats, the Update page's checkupdate,
 * the per-disk rw_test entries) - those pages run their command line
 * through shell_execute_captured(), whose L0 registry lookup now misses.
 *
 * This file restores them WITHOUT widening the oc> recovery surface:
 * shell_execute_captured() (the captured path - TUI pages, the command
 * bridge page, sshd exec) consults l1_bridge_try() only AFTER the L0
 * registry lookup misses, so:
 *   - names that stayed L0-declared keep their L0 handler (halt/reboot/
 *     ps/cat/... - the L0-first order also settles the power family,
 *     which exists on BOTH surfaces);
 *   - names that live only in the ring-3 ush table dispatch into the
 *     SAME kernel-side service entry SYS_L1SVC reaches
 *     (core_registry_svc_dispatch -> the service's l1_dispatch ->
 *     shell_run_fn_captured - the identical command bodies, capture
 *     machinery included);
 *   - the interactive oc> prompt (shell_execute_line) is deliberately
 *     NOT hooked: the recovery environment keeps its documented
 *     99-command surface (INTERFACES.md Part 1a).
 *
 * The name->(cat,op) map below is a VERBATIM COPY of the ush SVC_CMDS
 * table (l1apps/ush.c) - the ush side is the source of truth; keep
 * both in sync when a service op is added (same discipline as the
 * l1svc_defs.h copies).
 */
#include "l1svc_defs.h"
#include "core_registry.h"
#include "lib_string.h"

struct l1_bridge_cmd {
    const char *name;
    int cat;
    int op;
};

static const struct l1_bridge_cmd L1_BRIDGE_CMDS[] = {
    { "rtc", L1SVC_CAT_TIME, L1SVC_TIME_RTC },
    { "hpet", L1SVC_CAT_TIME, L1SVC_TIME_HPET },
    { "pmtimer", L1SVC_CAT_TIME, L1SVC_TIME_PMTIMER },
    { "tsc", L1SVC_CAT_TIME, L1SVC_TIME_TSC },
    { "mount", L1SVC_CAT_FS, L1SVC_FS_MOUNT },
    { "mounts", L1SVC_CAT_FS, L1SVC_FS_MOUNTS },
    { "umount", L1SVC_CAT_FS, L1SVC_FS_UMOUNT },
    { "fatmount", L1SVC_CAT_FS, L1SVC_FS_FATMOUNT },
    { "fatstat", L1SVC_CAT_FS, L1SVC_FS_FATSTAT },
    { "tree", L1SVC_CAT_FS, L1SVC_FS_TREE },
    { "ahci", L1SVC_CAT_BLOCK, L1SVC_BLK_AHCI },
    { "nvme", L1SVC_CAT_BLOCK, L1SVC_BLK_NVME },
    { "ata", L1SVC_CAT_BLOCK, L1SVC_BLK_ATA },
    { "dskstat", L1SVC_CAT_BLOCK, L1SVC_BLK_DSKSTAT },
    { "lsblk", L1SVC_CAT_BLOCK, L1SVC_BLK_LSBLK },
    { "parted", L1SVC_CAT_BLOCK, L1SVC_BLK_PARTED },
    { "mkfs.fat32", L1SVC_CAT_BLOCK, L1SVC_BLK_MKFS_FAT32 },
    { "mkfs.exfat", L1SVC_CAT_BLOCK, L1SVC_BLK_MKFS_EXFAT },
    { "mkfs.ext4", L1SVC_CAT_BLOCK, L1SVC_BLK_MKFS_EXT4 },
    { "fsck", L1SVC_CAT_BLOCK, L1SVC_BLK_FSCK },
    { "sync", L1SVC_CAT_BLOCK, L1SVC_BLK_SYNC },
    { "ahci_test", L1SVC_CAT_BLOCK, L1SVC_BLK_AHCI_TEST },
    { "nvme_test", L1SVC_CAT_BLOCK, L1SVC_BLK_NVME_TEST },
    { "ata_dma_test", L1SVC_CAT_BLOCK, L1SVC_BLK_ATA_DMA_TEST },
    { "virtio_blk_test", L1SVC_CAT_BLOCK, L1SVC_BLK_VIRTIO_TEST },
    { "disk_rw_test", L1SVC_CAT_BLOCK, L1SVC_BLK_RW_TEST },
    { "partition_test", L1SVC_CAT_BLOCK, L1SVC_BLK_PART_TEST },
    { "fs_mount_test", L1SVC_CAT_BLOCK, L1SVC_BLK_FS_MOUNT_TEST },
    { "real_hw_test", L1SVC_CAT_BLOCK, L1SVC_BLK_REAL_HW_TEST },
    { "abdisk", L1SVC_CAT_BLOCK, L1SVC_BLK_ABDISK },
    { "install", L1SVC_CAT_BLOCK, L1SVC_BLK_INSTALL },
    { "grub-install", L1SVC_CAT_BLOCK, L1SVC_BLK_GRUB_INSTALL },
    { "abcfg", L1SVC_CAT_BLOCK, L1SVC_BLK_ABCFG },
    { "e1000e", L1SVC_CAT_NIC, L1SVC_NIC_E1000E },
    { "igb", L1SVC_CAT_NIC, L1SVC_NIC_IGB },
    { "ixgbe", L1SVC_CAT_NIC, L1SVC_NIC_IXGBE },
    { "rtl8139", L1SVC_CAT_NIC, L1SVC_NIC_RTL8139 },
    { "rtl8168", L1SVC_CAT_NIC, L1SVC_NIC_RTL8168 },
    { "rtl8125", L1SVC_CAT_NIC, L1SVC_NIC_RTL8125 },
    { "rtl810x", L1SVC_CAT_NIC, L1SVC_NIC_RTL810X },
    { "bcm57xx", L1SVC_CAT_NIC, L1SVC_NIC_BCM57XX },
    { "e1000e_test", L1SVC_CAT_NIC, L1SVC_NIC_E1000E_TEST },
    { "igb_test", L1SVC_CAT_NIC, L1SVC_NIC_IGB_TEST },
    { "ixgbe_test", L1SVC_CAT_NIC, L1SVC_NIC_IXGBE_TEST },
    { "rtl8139_test", L1SVC_CAT_NIC, L1SVC_NIC_RTL8139_TEST },
    { "rtl8168_test", L1SVC_CAT_NIC, L1SVC_NIC_RTL8168_TEST },
    { "rtl8125_test", L1SVC_CAT_NIC, L1SVC_NIC_RTL8125_TEST },
    { "rtl810x_test", L1SVC_CAT_NIC, L1SVC_NIC_RTL810X_TEST },
    { "bcm57xx_test", L1SVC_CAT_NIC, L1SVC_NIC_BCM57XX_TEST },
    { "other_nic_test", L1SVC_CAT_NIC, L1SVC_NIC_OTHER_TEST },
    { "nic_rw_test", L1SVC_CAT_NIC, L1SVC_NIC_RW_TEST },
    { "wifi", L1SVC_CAT_NIC, L1SVC_NIC_WIFI },
    { "bt", L1SVC_CAT_NIC, L1SVC_NIC_BT },
    { "sound", L1SVC_CAT_SND, L1SVC_SND_SOUND },
    { "hda", L1SVC_CAT_SND, L1SVC_SND_HDA },
    { "ac97", L1SVC_CAT_SND, L1SVC_SND_AC97 },
    { "sb16", L1SVC_CAT_SND, L1SVC_SND_SB16 },
    { "es1370", L1SVC_CAT_SND, L1SVC_SND_ES1370 },
    { "virtiosnd", L1SVC_CAT_SND, L1SVC_SND_VIRTIOSND },
    { "usbaudio", L1SVC_CAT_SND, L1SVC_SND_USBAUDIO },
    { "play", L1SVC_CAT_SND, L1SVC_SND_PLAY },
    { "volume", L1SVC_CAT_SND, L1SVC_SND_VOLUME },
    { "hda_test", L1SVC_CAT_SND, L1SVC_SND_HDA_TEST },
    { "ac97_test", L1SVC_CAT_SND, L1SVC_SND_AC97_TEST },
    { "sb16_test", L1SVC_CAT_SND, L1SVC_SND_SB16_TEST },
    { "es1370_test", L1SVC_CAT_SND, L1SVC_SND_ES1370_TEST },
    { "virtio_snd_test", L1SVC_CAT_SND, L1SVC_SND_VIRTIO_TEST },
    { "usb_audio_test", L1SVC_CAT_SND, L1SVC_SND_USB_TEST },
    { "audio_rw_test", L1SVC_CAT_SND, L1SVC_SND_RW_TEST },
    { "sample_rate_test", L1SVC_CAT_SND, L1SVC_SND_RATE_TEST },
    { "checkupdate", L1SVC_CAT_OTA, L1SVC_OTA_CHECKUPDATE },
    { "config", L1SVC_CAT_OTA, L1SVC_OTA_CONFIG },
    { "config_test", L1SVC_CAT_OTA, L1SVC_OTA_CONFIG_TEST },
    { "checkupdate_test", L1SVC_CAT_OTA, L1SVC_OTA_CHECKUPDATE_TEST },
    { "update", L1SVC_CAT_OTA, L1SVC_OTA_UPDATE },
    { "rollback", L1SVC_CAT_OTA, L1SVC_OTA_ROLLBACK },
    { "update_pkg_test", L1SVC_CAT_OTA, L1SVC_OTA_PKG_TEST },
    { "ab_partition_test", L1SVC_CAT_OTA, L1SVC_OTA_AB_TEST },
    { "update_check_test", L1SVC_CAT_OTA, L1SVC_OTA_CHECK_TEST },
    { "update_download_test", L1SVC_CAT_OTA, L1SVC_OTA_DOWNLOAD_TEST },
    { "update_verify_test", L1SVC_CAT_OTA, L1SVC_OTA_VERIFY_TEST },
    { "update_install_test", L1SVC_CAT_OTA, L1SVC_OTA_INSTALL_TEST },
    { "update_rollback_test", L1SVC_CAT_OTA, L1SVC_OTA_ROLLBACK_TEST },
    { "update_local_test", L1SVC_CAT_OTA, L1SVC_OTA_LOCAL_TEST },
    { "update_status_test", L1SVC_CAT_OTA, L1SVC_OTA_STATUS_TEST },
    { "real_update_test", L1SVC_CAT_OTA, L1SVC_OTA_REAL_TEST },
    { "shutdown", L1SVC_CAT_POWER, L1SVC_PWR_SHUTDOWN },
    { "poweroff", L1SVC_CAT_POWER, L1SVC_PWR_SHUTDOWN },
    { "reboot", L1SVC_CAT_POWER, L1SVC_PWR_REBOOT },
    { "halt", L1SVC_CAT_POWER, L1SVC_PWR_HALT },
    { "sleep", L1SVC_CAT_POWER, L1SVC_PWR_SLEEP },
    { "poweroff_test", L1SVC_CAT_POWER, L1SVC_PWR_POWEROFF_TEST },
    { "suspend_test", L1SVC_CAT_POWER, L1SVC_PWR_SUSPEND_TEST },
    { "halt_test", L1SVC_CAT_POWER, L1SVC_PWR_HALT_TEST },
    { "reboot_test", L1SVC_CAT_POWER, L1SVC_PWR_REBOOT_TEST },
    { "stats", L1SVC_CAT_DISPLAY, L1SVC_DISP_STATS },
    { "uname", L1SVC_CAT_DISPLAY, L1SVC_DISP_UNAME },
    { "clear", L1SVC_CAT_DISPLAY, L1SVC_DISP_CLEAR },
    { "lang", L1SVC_CAT_DISPLAY, L1SVC_DISP_LANG },
    { "chinese", L1SVC_CAT_DISPLAY, L1SVC_DISP_CHINESE },
    { "chinesetest", L1SVC_CAT_DISPLAY, L1SVC_DISP_CHINESETEST },
    { "font", L1SVC_CAT_DISPLAY, L1SVC_DISP_FONT },
    { "font-test", L1SVC_CAT_DISPLAY, L1SVC_DISP_FONT_TEST },
    { "encoding", L1SVC_CAT_DISPLAY, L1SVC_DISP_ENCODING },
    { "encoding-test", L1SVC_CAT_DISPLAY, L1SVC_DISP_ENC_TEST },
    { "ime", L1SVC_CAT_DISPLAY, L1SVC_DISP_IME },
    { "ime-test", L1SVC_CAT_DISPLAY, L1SVC_DISP_IME_TEST },
    { "touchpad", L1SVC_CAT_INPUT, L1SVC_INP_TOUCHPAD },
    { "touchtest", L1SVC_CAT_INPUT, L1SVC_INP_TOUCHTEST },
    { "uvc", L1SVC_CAT_CAMERA, L1SVC_CAM_UVC },
    { "uvctest", L1SVC_CAT_CAMERA, L1SVC_CAM_UVCTEST },
    { "scsi", L1SVC_CAT_CAMERA, L1SVC_CAM_SCSI },
    { "scsitest", L1SVC_CAT_CAMERA, L1SVC_CAM_SCSITEST },
    { "gameport", L1SVC_CAT_CAMERA, L1SVC_CAM_GAMEPORT },
    { "gameporttest", L1SVC_CAT_CAMERA, L1SVC_CAM_GAMEPORTTEST },
    { "parallel", L1SVC_CAT_CAMERA, L1SVC_CAM_PARALLEL },
    { "paralleltest", L1SVC_CAT_CAMERA, L1SVC_CAM_PARALLELTEST },
    { "ir", L1SVC_CAT_CAMERA, L1SVC_CAM_IR },
    { "irtest", L1SVC_CAT_CAMERA, L1SVC_CAM_IRTEST },
    { "usb", L1SVC_CAT_USB, L1SVC_USB_STATUS },
    { "usbdev", L1SVC_CAT_USB, L1SVC_USB_DEV },
    { "usb_core_test", L1SVC_CAT_USB, L1SVC_USB_CORE_TEST },
    { "usb_kbd_test", L1SVC_CAT_USB, L1SVC_USB_KBD_TEST },
    { "usb_mouse_test", L1SVC_CAT_USB, L1SVC_USB_MOUSE_TEST },
    { "usb_storage_test", L1SVC_CAT_USB, L1SVC_USB_STORAGE_TEST },
    { "usb_serial_test", L1SVC_CAT_USB, L1SVC_USB_SERIAL_TEST },
    { "usb_hotplug_test", L1SVC_CAT_USB, L1SVC_USB_HOTPLUG_TEST },
    { "usb_hub_test", L1SVC_CAT_USB, L1SVC_USB_HUB_TEST },
    { "arp", L1SVC_CAT_NET, L1SVC_NET_ARP },
    { "dhcp", L1SVC_CAT_NET, L1SVC_NET_DHCP },
    { "dns", L1SVC_CAT_NET, L1SVC_NET_DNS },
    { "dnstest", L1SVC_CAT_NET, L1SVC_NET_DNSTEST },
    { "firewall", L1SVC_CAT_NET, L1SVC_NET_FIREWALL },
    { "ip", L1SVC_CAT_NET, L1SVC_NET_IP },
    { "lspci", L1SVC_CAT_NET, L1SVC_NET_LSPCI },
    { "nf_test", L1SVC_CAT_NET, L1SVC_NET_NF_TEST },
    { "route", L1SVC_CAT_NET, L1SVC_NET_ROUTE },
    { "tcpcc_test", L1SVC_CAT_NET, L1SVC_NET_TCPCC_TEST },
    { "tcpstats", L1SVC_CAT_NET, L1SVC_NET_TCPSTATS },
    { "tcptest", L1SVC_CAT_NET, L1SVC_NET_TCPTEST },
    { "cryptotest", L1SVC_CAT_CRYPTO, L1SVC_CRYPTO_CRYPTOTEST },
    { "dhtest", L1SVC_CAT_CRYPTO, L1SVC_CRYPTO_DHTEST },
};

/* Returns 0 = dispatched through the bridge (out holds the service
 * text), -1 = the first word is not a bridge name (caller falls back
 * to the L0 path), 1 = name matched but the service refused the op. */
int l1_bridge_try(const char *line, char *out, int out_cap)
{
    if (!line || !out || out_cap <= 0) return -1;
    while (*line == ' ' || *line == '\t') line++;
    char word[40];
    int n = 0;
    while (line[n] && line[n] != ' ' && line[n] != '\t' && n < 39) {
        word[n] = line[n];
        n++;
    }
    word[n] = 0;
    if (!word[0]) return -1;
    const char *args = line + n;
    while (*args == ' ' || *args == '\t') args++;

    for (int i = 0; i < (int)(sizeof(L1_BRIDGE_CMDS) / sizeof(L1_BRIDGE_CMDS[0])); i++) {
        if (strcmp(L1_BRIDGE_CMDS[i].name, word) != 0) continue;
        const char *cat_name = L1SVC_CAT_NAME(L1_BRIDGE_CMDS[i].cat);
        if (!cat_name) return 1;
        out[0] = 0;
        int rc = core_registry_svc_dispatch(cat_name, (u32)L1_BRIDGE_CMDS[i].op,
                                           args, out, out_cap);
        if (rc < 0 && out[0] == 0) {
            strcpy(out, "l1 bridge: service refused op ");
            strcat(out, word);
            strcat(out, "\n");
            return 1;
        }
        return rc >= 0 ? rc : 0;
    }
    return -1;
}
