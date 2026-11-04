/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP06c-installer_gui
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
    [OCSE_T_BTN_INSTALL] = { "Install now",            "立即安装" },
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
    [OCSE_T_C_KEYS2]     = { "config: keys: hostname <name> | lang en|zh |"
                            " boot.slot A|B | update_url <http...> |"
                            " auto_check yes|no",
                            "config: 键：hostname <名称> | lang en|zh |"
                            " boot.slot A|B | update_url <http…> |"
                            " auto_check yes|no" },
    /* WP-06c: GUI wizard. zh fragments compose at the same skeleton
     * positions as the English ones (the b6 rule). */
    [OCSE_W_TITLE]       = { "Install OpenCubeOS",
                             "安装 OpenCubeOS" },
    [OCSE_W_WELCOME1]    = { "This wizard installs OpenCubeOS onto a disk."
                            " Choose the language first.",
                             "本向导将 OpenCubeOS 安装到磁盘。请先选择语言。" },
    [OCSE_W_WELCOME2]    = { "Every install action runs the same OCSECL"
                            " commands the command line offers"
                            " (Ctrl+Alt+F2).",
                             "每个安装动作都运行命令行（Ctrl+Alt+F2）提供的"
                            " OCSECL 命令。" },
    [OCSE_W_BTN_NEXT]    = { "Next",        "下一步" },
    [OCSE_W_BTN_BACK]    = { "Back",        "上一步" },
    [OCSE_W_BTN_CANCEL]  = { "Cancel",      "取消" },
    [OCSE_W_BTN_RETRY]   = { "Retry",       "重试" },
    [OCSE_W_BTN_LANG]    = { "Language",    "语言" },
    [OCSE_W_BTN_FINISH]  = { "Finish",      "完成" },
    [OCSE_W_BTN_REBOOT]  = { "Reboot now",  "立即重启" },
    [OCSE_W_BTN_CREATE]  = { "Create partition", "创建分区" },
    [OCSE_W_BTN_FORMAT]  = { "Format as FAT32", "格式化为 FAT32" },
    [OCSE_W_BTN_RESCAN]  = { "Rescan",      "重新扫描" },
    [OCSE_W_BTN_START]   = { "Start installation", "开始安装" },
    [OCSE_W_STEP_LANG]   = { "Welcome / language", "欢迎 / 语言" },
    [OCSE_W_STEP_DISK]   = { "Target disk",      "目标磁盘" },
    [OCSE_W_STEP_PART]   = { "Partitioning",     "分区" },
    [OCSE_W_STEP_FMT]    = { "Formatting",       "格式化" },
    [OCSE_W_STEP_USER]   = { "User account",     "用户账户" },
    [OCSE_W_STEP_SYS]    = { "System configuration", "系统配置" },
    [OCSE_W_STEP_CONFIRM]= { "Review",           "确认" },
    [OCSE_W_STEP_INSTALL]= { "Installing",       "正在安装" },
    [OCSE_W_STEP_DONE]   = { "Finished",         "完成" },
    [OCSE_W_LANG_HEAD]   = { "Choose the installer language:",
                             "请选择安装界面语言：" },
    [OCSE_W_DISK_HEAD]   = { "Choose the disk to install onto:",
                             "请选择要安装的磁盘：" },
    [OCSE_W_DISK_NONE]   = { "No disk found - attach one and press Rescan.",
                             "未发现磁盘——请接入后点击重新扫描。" },
    [OCSE_W_DISK_NOOVER] = { "The installer never overwrites an existing"
                            " OpenCubeOS system.",
                             "安装器不会覆盖已有的 OpenCubeOS 系统。" },
    [OCSE_W_PART_HEAD]   = { "Partition the disk:", "对磁盘进行分区：" },
    [OCSE_W_PART_AUTO]   = { "Use the largest free gap (recommended)",
                             "使用最大空闲空间（推荐）" },
    [OCSE_W_PART_GAP]    = { "Largest free gap: ", "最大空闲空间：" },
    [OCSE_W_PART_NOGAP]  = { "No free space, please clean up or shrink"
                            " partitions manually.",
                             "无可用空间，请手动清理或缩小分区。" },
    [OCSE_W_PART_MADE]   = { "Partition created: ", "分区已创建：" },
    [OCSE_W_PART_REUSE]  = { "The partition created above will be used.",
                             "将使用上面创建的分区。" },
    [OCSE_W_FMT_HEAD]    = { "Format the new partition:",
                             "格式化新分区：" },
    [OCSE_W_FMT_ONLY]    = { "FAT32 is the filesystem the bootloader loads"
                            " the kernel from; exFAT/ext4 are offered by the"
                            " command line but the boot target must be FAT32.",
                             "引导加载器只从 FAT32 卷加载内核；命令行也提供"
                            " exFAT/ext4，但安装目标必须是 FAT32。" },
    [OCSE_W_FMT_WARN]    = { "Formatting DESTROYS ALL DATA on the selected"
                            " partition.",
                             "格式化将销毁所选分区上的全部数据！" },
    [OCSE_W_FMT_DONE]    = { "Formatted.", "格式化完成。" },
    [OCSE_W_USER_HEAD]   = { "Create the primary user:", "创建主用户：" },
    [OCSE_W_USER_NAME]   = { "Login name:", "登录名：" },
    [OCSE_W_USER_FULL]   = { "Full name:", "全名：" },
    [OCSE_W_USER_PASS]   = { "Password:", "密码：" },
    [OCSE_W_USER_NOTE]   = { "The password is stored as a SHA-256 hash -"
                            " never in clear text.",
                             "密码以 SHA-256 哈希存储——绝不保存明文。" },
    [OCSE_W_USER_SHORT]  = { "Login name: use 1-31 letters, digits, . _ -",
                             "登录名：1-31 个字母、数字、点、下划线或连字符" },
    [OCSE_W_SYS_HEAD]    = { "System configuration:", "系统配置：" },
    [OCSE_W_SYS_HOST]    = { "Hostname:", "主机名：" },
    [OCSE_W_SYS_UPD]     = { "Update check URL (empty = default):",
                             "更新检查 URL（留空 = 默认）：" },
    [OCSE_W_SYS_AUTO]    = { "Automatic update check (yes/no):",
                             "自动检查更新（yes/no）：" },
    [OCSE_W_CONF_HEAD]   = { "Review your choices:", "请确认您的选择：" },
    [OCSE_W_CONF_DISK]   = { "Disk: ",       "磁盘：" },
    [OCSE_W_CONF_PART]   = { "Partition: ",  "分区：" },
    [OCSE_W_CONF_USER]   = { "User: ",       "用户：" },
    [OCSE_W_CONF_HOST]   = { "Hostname: ",   "主机名：" },
    [OCSE_W_CONF_GO]     = { "Starting runs the real OCSECL commands"
                            " (mkfs, install, config, ...).",
                             "开始后将运行真实的 OCSECL 命令"
                            "（mkfs、install、config 等）。" },
    [OCSE_W_INST_PROG]   = { "Installing - stage ", "正在安装 - 阶段 " },
    [OCSE_W_INST_DONE]   = { "Installation complete.", "安装完成。" },
    [OCSE_W_INST_REMOVE] = { "Remove the installation medium and reboot.",
                             "请移除安装介质并重启。" },
    [OCSE_W_ERR_HEAD]    = { "A command failed:", "命令执行失败：" },
    [OCSE_W_ERR_CMD]     = { "Command: ", "命令：" },
    [OCSE_W_ERR_OUT]     = { "Output:", "输出：" },
    [OCSE_W_CANCEL_Q]    = { "Cancel the installation?", "要取消安装吗？" },
    [OCSE_W_CANCEL_CLEAN]= { "Nothing has been written yet - safe to"
                            " cancel.",
                             "尚未写入任何数据——可以安全取消。" },
    [OCSE_W_CANCEL_WROT] = { "Data has already been written to the disk -"
                            " cancel anyway?",
                             "磁盘上已有写入的数据——仍要取消吗？" },
    [OCSE_W_YES]         = { "Yes", "是" },
    [OCSE_W_NO]          = { "No",  "否" },
    /* 3.0.0-WP06f: Advanced options (GUI/OCSECL parity). */
    [OCSE_W_ADV_TOGGLE]  = { "Advanced options: off",
                             "高级选项：关" },
    [OCSE_W_ADV_ON]      = { "Advanced options: on",
                             "高级选项：开" },
    [OCSE_W_ADV_DEL]     = { "Delete partition", "删除分区" },
    [OCSE_W_ADV_DELQ]    = { "Delete the selected partition?",
                             "要删除所选分区吗？" },
    [OCSE_W_ADV_DELW]    = { "ALL DATA on it will be lost. This cannot "
                            "be undone.",
                             "其上的全部数据将丢失，且无法撤销。" },
    [OCSE_W_ADV_NONE]    = { "Select a partition in the list first.",
                             "请先在列表中选择一个分区。" },
    [OCSE_W_ADV_EXIST]   = { "An existing system was detected - with "
                            "Advanced on you may delete its partition "
                            "below.",
                             "检测到已有系统——开启高级选项后可在下方删"
                            "除其分区。" },
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

/* ---- WP-06c: output capture (the wizard command channel) ----
 * When g_cap is set, ocse_out/outln append into the buffer instead of
 * the console; the serial line still receives everything (the QEMU
 * battery greps the serial log for the wizard's command flow). */
static char* g_cap_buf = 0;
static int   g_cap_len = 0;
static int   g_cap_cap = 0;

void ocse_out_capture(char* buf, int cap) {
    g_cap_buf = buf;
    g_cap_cap = cap > 0 ? cap : 0;
    g_cap_len = 0;
    if (g_cap_buf && g_cap_cap > 0) g_cap_buf[0] = 0;
}

void ocse_out_capture_off(void) {
    g_cap_buf = 0;
    g_cap_cap = 0;
    g_cap_len = 0;
}

static void cap_puts(const char* s) {
    if (!g_cap_buf) return;
    while (*s && g_cap_len < g_cap_cap - 1)
        g_cap_buf[g_cap_len++] = *s++;
    g_cap_buf[g_cap_len] = 0;
}

void ocse_out(const char* s) {
    if (g_cap_buf) { cap_puts(s); ocse_serial_puts_raw(s); return; }
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
    if (g_cap_buf) {
        cap_puts(s);
        cap_puts("\n");
        ocse_serial_puts_raw(s);
        ocse_serial_puts_raw("\r\n");
        return;
    }
    ocse_out(s);
    screen_console_putc('\n');
    ocse_serial_puts_raw("\r\n");
}
