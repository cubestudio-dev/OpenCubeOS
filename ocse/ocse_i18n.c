/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP06a-ocse
 * File: ocse/ocse_i18n.c
 * Purpose: en/zh message catalog + console/serial output helpers.
 * zh messages are UTF-8 literals; ocse_out() converts them to GB2312
 * for the console_cn renderer via the vendored encoding engine.
 */
#include "ocse_i18n.h"
#include "encoding.h"
#include "screen_console.h"
#include "lib_string.h"

/* Declared in ocse_main.c (kept next to the hotkey plumbing). */
void ocse_serial_puts_raw(const char* s);

static int g_lang = OCSE_LANG_EN;

/* Message catalog: [key][lang]. zh entries are UTF-8. */
static const char* const g_msgs[OCSE_T_COUNT_][2] = {
    [OCSE_T_TITLE]      = { "OpenCubeOS Setup",        "OpenCubeOS 安装环境" },
    [OCSE_T_SUBTITLE]   = { "3.0.0-WP06a installer environment",
                            "3.0.0-WP06a 安装环境" },
    [OCSE_T_HW_HEAD]    = { "Hardware check",         "硬件检测" },
    [OCSE_T_CHECK_CPU]  = { "CPU (long mode)",        "处理器（长模式）" },
    [OCSE_T_CHECK_MEM]  = { "Memory",                 "内存" },
    [OCSE_T_CHECK_DISP] = { "Display (framebuffer)",  "显示（帧缓冲）" },
    [OCSE_T_CHECK_DISK] = { "Storage (disk present)", "存储（磁盘）" },
    [OCSE_T_PASS]       = { "PASS",                   "通过" },
    [OCSE_T_FAIL]       = { "FAIL",                   "未通过" },
    [OCSE_T_DISK_HEAD]  = { "Disks",                  "磁盘" },
    [OCSE_T_NO_DISK]    = { "No disk found - attach one and re-check",
                            "未发现磁盘，接入后重新检测" },
    [OCSE_T_SPACE_LOW]  = { "low space",              "空间不足" },
    [OCSE_T_BTN_CLI]    = { "Command-line install",   "命令行安装" },
    [OCSE_T_BTN_LANG]   = { "Language",               "语言" },
    [OCSE_T_BTN_CHECK]  = { "Re-check",               "重新检测" },
    [OCSE_T_GUI_HINT]   = { "Click a button. Ctrl+Alt+F2 = command line",
                            "点击按钮，Ctrl+Alt+F2 进入命令行" },
    [OCSE_T_CLI_BANNER] = { "OCSECL - the OpenCubeOS setup command line.",
                            "OCSECL —— OpenCubeOS 安装命令行。" },
    [OCSE_T_CLI_HELP]   = { "Type 'help' to list commands, 'help <cmd>' for"
                            " one command.",
                            "输入 help 列出全部命令，help <命令> 查看单条帮助。" },
    [OCSE_T_LANG_NOW]   = { "language: English",      "语言：中文" },
    [OCSE_T_DONE]       = { "done",                   "完成" },
    [OCSE_T_HALT]       = { "halting...",             "正在关机..." },
    [OCSE_T_REBOOTING]  = { "rebooting...",          "正在重启..." },
    [OCSE_T_POWEROFF]   = { "powering off...",        "正在关机..." },
    [OCSE_T_POWEROFF_NOACPI] =
        { "power-off unsupported here - halting", "不支持关机，改为停机" },
    [OCSE_T_P_NODISK]   = { "disk not found: ",       "未找到磁盘：" },
    [OCSE_T_P_NOPART]   = { "no partitions on this disk", "该磁盘没有分区" },
    [OCSE_T_P_BADPART]  = { "partition not found",   "未找到分区" },
    [OCSE_T_P_NOTABLE]  = { "no partition table found", "未找到分区表" },
    [OCSE_T_P_NOFREE]   = { "No free space, please clean up or shrink "
                            "partitions manually.",
                            "无可用空间，请手动清理或缩小分区。" },
    [OCSE_T_P_OVERLAP]  = { "space overlaps an existing partition",
                            "空间与已有分区重叠" },
    [OCSE_T_P_TABLEFULL] = { "partition table is full (4 MBR slots)",
                            "分区表已满（MBR 仅 4 个主分区）" },
    [OCSE_T_P_CREATED]  = { "partition created",      "分区已创建" },
    [OCSE_T_P_REMOVED]  = { "partition removed",     "分区已删除" },
    [OCSE_T_P_FLAGSET]  = { "flag updated",          "标志已更新" },
    [OCSE_T_P_BADFLAG]  = { "unknown flag (supported: boot)",
                            "未知标志（支持：boot）" },
    [OCSE_T_P_MNT_WARN] = { "partition is mounted - umount first",
                            "分区已挂载，请先卸载" },
    [OCSE_T_P_BADRANGE] = { "invalid range (need start < end <= disk size)",
                            "范围无效（需 start < end ≤ 磁盘容量）" },
    /* WP-06b b6: installer journey (zh tails compose at the same
     * position as the English fragments - the zh word order reads
     * naturally with the same skeleton). */
    [OCSE_T_M_WARN]      = { "WARNING: formatting DESTROYS ALL DATA on the target device!",
                              "警告：格式化将销毁目标设备上的全部数据！" },
    [OCSE_T_M_CONFIRMED] = { "! Nothing was written.",
                              "！未写入任何数据。" },
    [OCSE_T_M_NOWRITE]   = { "Nothing was written.",
                              "未写入任何数据。" },
    [OCSE_T_M_MOUNTED]   = { " is mounted - umount it first",
                              " 已挂载——请先卸载" },
    [OCSE_T_M_FORMATTED] = { "formatted",
                              "已格式化" },
    [OCSE_T_M_FMTFAIL]   = { "format failed",
                              "格式化失败" },
    [OCSE_T_M_NOFS]      = { " does not carry a mountable filesystem",
                              " 上没有可挂载的文件系统" },
    [OCSE_T_M_MNTAT]     = { ") at ",
                              "）于 " },
    [OCSE_T_M_MOUNTEDW]  = { "mounted ",
                              "已挂载 " },
    [OCSE_T_M_UNMOUNTEDW]= { "unmounted ",
                              "已卸载 " },
    [OCSE_T_M_UMNTFAIL]  = { " failed (busy or not a mount point)",
                              " 失败（忙或非挂载点）" },
    [OCSE_T_I_MEDIUMCHK] = { "install: checking the install medium ...",
                              "install: 正在检查安装介质……" },
    [OCSE_T_I_MFATAL]    = { "install: FATAL - install medium incomplete (OCOS.TAR / OCBLMBR.BIN)",
                              "install: 致命错误——安装介质不完整（OCOS.TAR / OCBLMBR.BIN）" },
    [OCSE_T_I_MEDIUMOK]  = { "install: medium ok (ocos.tar ",
                              "install: 介质就绪（ocos.tar " },
    [OCSE_T_I_TARGETW]   = { " (fat32) mounted at ",
                              "（fat32）已挂载于 " },
    [OCSE_T_I_NOTFAT]    = { "' is not FAT32 (OCBL loads the kernel from FAT volumes;",
                              " 不是 FAT32（OCBL 只能从 FAT 卷加载内核；" },
    [OCSE_T_I_NOTFAT2]   = { "install:               format it with: mkfs.fat32 <device> confirm)",
                              "install:               请先用 mkfs.fat32 <设备> confirm 格式化）" },
    [OCSE_T_I_NOTMOUNTED]= { " is not mounted (mount it first, e.g. mount <device> /mnt)",
                              " 未挂载（请先挂载，例如 mount <设备> /mnt）" },
    [OCSE_T_I_GATE]      = { "install: this OVERWRITES the boot area and /boot on the target - append 'confirm'",
                              "install: 这将覆盖目标上的引导区与 /boot——请追加 'confirm'" },
    [OCSE_T_I_EXTRACT]   = { "install: extracting OCOS.TAR ...",
                              "install: 正在解包 OCOS.TAR……" },
    [OCSE_T_I_KERNELIN]  = { "install: kernel in place (",
                              "install: 内核就位（" },
    [OCSE_T_I_BOOTCODE]  = { "install: writing the boot code ...",
                              "install: 正在写入引导代码……" },
    [OCSE_T_I_BOOTABLE]  = { " marked bootable",
                              " 已标记为可引导" },
    [OCSE_T_I_COMPLETE]  = { "Installation complete.",
                              "安装完成。" },
    [OCSE_T_I_REMOVE]    = { "Remove the installation medium and boot from the disk,",
                              "移除安装介质并从磁盘引导，" },
    [OCSE_T_I_REBOOT]    = { "or run: reboot",
                              "或运行：reboot" },
    [OCSE_T_O_BOOTCODE]  = { "ocbl-install: boot code written (stage1 @LBA0, core ",
                              "ocbl-install: 引导代码已写入（stage1 @LBA0，core " },
    [OCSE_T_O_VERIFIED]  = { ", partition table preserved, verified)",
                              "，分区表已保留，已校验）" },
    [OCSE_T_O_NOPART]    = { "ocbl-install: disk has no partition table - create a partition first",
                              "ocbl-install: 磁盘没有分区表——请先创建分区" },
    [OCSE_T_C_INVALID]   = { "config: invalid key or value",
                              "config: 无效的键或值" },
    [OCSE_T_C_KEYS]      = { "config: keys: hostname <name> | lang en|zh | boot.slot A|B",
                              "config: 键：hostname <名称> | lang en|zh | boot.slot A|B" },
    [OCSE_T_C_NOMOUNT]   = { "config: no installed system mounted (mount the FAT32 target first)",
                              "config: 未挂载已安装的系统（请先挂载 FAT32 目标）" },
    [OCSE_T_C_WRITTEN]   = { " written",
                              " 已写入" },
    [OCSE_T_S_FLUSHED]   = { "sync: block cache flushed",
                              "sync: 块缓存已刷写" },
};

void ocse_i18n_init(void) {
    g_lang = OCSE_LANG_EN;
}

int ocse_i18n_lang(void) {
    return g_lang;
}

void ocse_i18n_set(int lang) {
    if (lang == OCSE_LANG_EN || lang == OCSE_LANG_ZH)
        g_lang = lang;
}

const char* ocse_tr(int key) {
    if (key < 0 || key >= OCSE_T_COUNT_)
        return "";
    return g_msgs[key][g_lang];
}

const char* ocse_lang_name(int lang) {
    return lang == OCSE_LANG_ZH ? "\xe4\xb8\xad\xe6\x96\x87" : "English";
}

void ocse_out(const char* s) {
    if (g_lang == OCSE_LANG_ZH) {
        static char conv[512];
        u32 n = 0;
        int rc = encoding_convert((const u8*)s, (u32)strlen(s),
                                  ENC_UTF8, ENC_GB2312,
                                  (u8*)conv, sizeof(conv) - 1, &n);
        if (rc == 0 && n > 0) {
            conv[n] = 0;
            screen_console_puts(conv);
            ocse_serial_puts_raw(conv);
            return;
        }
        /* fall through on conversion failure: raw bytes are better
         * than silence. */
    }
    screen_console_puts(s);
    ocse_serial_puts_raw(s);
}

void ocse_outln(const char* s) {
    ocse_out(s);
    screen_console_putc('\n');
    ocse_serial_puts_raw("\r\n");
}
