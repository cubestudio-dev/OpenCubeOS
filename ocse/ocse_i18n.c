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
    [OCSE_T_CLI_BANNER] = { "OCSECL - the OCSE command line. Type 'help'.",
                            "OCSECL —— OCSE 命令行，输入 help 查看命令。" },
    [OCSE_T_CLI_HELP]   = { "commands: help | lang [en|zh] | hwcheck |"
                            " disks | gui | clear | halt",
                            "命令：help | lang [en|zh] | hwcheck | disks |"
                            " gui | clear | halt" },
    [OCSE_T_LANG_NOW]   = { "language: English",      "语言：中文" },
    [OCSE_T_DONE]       = { "done",                   "完成" },
    [OCSE_T_HALT]       = { "halting...",             "正在关机..." },
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
