/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP02-ocire_recovery
 * File: ocire/ocire_shell.c
 * Purpose: the OCIRE full recovery shell.
 *
 * WP-02 command set:
 *   help         - list available commands
 *   logread      - dump the boot-log ring (the OCOS agent's record)
 *   syscheck     - ten-point integrity self-check
 *   repair quick - fix metadata + restore damaged system files
 *   repair full  - reformat + full golden rewrite (asks YES)
 *   return       - reboot into OpenCubeOS (normal-boot mailbox)
 *   shutdown     - power off (ACPI S5, with legacy port fallbacks)
 *   reboot       - reset the machine (8042, ACPI RESET_REG, 0xCF9)
 *
 * Line editing supports backspace.  Anything unrecognised prints an
 * honest "unknown command" hint - never a silent drop.
 */

#include "ocire_shell.h"
#include "ocire_console.h"
#include "ocire_input.h"
#include "ocire_io.h"
#include "ocire_util.h"
#include "ocire_acpi.h"
#include "ocire_recover.h"

#define OCIRE_PROMPT "ocire> "
#define LINE_MAX 96

/* ---- WP06g3-4.8: the bilingual shell --------------------------------
 * The recovery shell is a SYSTEM surface: commands and their names
 * stay English (the system-string rule); the HELP DESCRIPTIONS and
 * the notices carry a zh column so a Chinese-speaking user can read
 * what each recovery action does before running it.  OCIRE is an
 * independent kernel - no i18n framework rides along, just the same
 * flat {en, zh} table discipline OCSE uses (ocse_tr). */
static int g_shell_zh;

enum {
    SH_TITLE, SH_HELP, SH_LOGREAD, SH_SYSCHECK, SH_RQUICK, SH_RFULL,
    SH_RETURN, SH_SHUTDOWN, SH_REBOOT, SH_LANG_NOW, SH_UNKNOWN,
    SH_USAGE_REPAIR, SH_COUNT_
};
static const char* const g_shell_msgs[SH_COUNT_][2] = {
    [SH_TITLE]    = { "OCIRE commands:\n",                        "OCIRE 命令：\n" },
    [SH_HELP]     = { "  help         - list available commands\n",
                      "  help         - 列出可用命令\n" },
    [SH_LOGREAD]  = { "  logread      - dump the boot-log ring (OCOS agent record)\n",
                      "  logread      - 查看启动日志环（OCOS 代理记录）\n" },
    [SH_SYSCHECK] = { "  syscheck     - ten-point integrity self-check\n",
                      "  syscheck     - 十项完整性自检\n" },
    [SH_RQUICK]   = { "  repair quick - fix metadata + restore damaged system files\n",
                      "  repair quick - 修复元数据并还原受损系统文件\n" },
    [SH_RFULL]    = { "  repair full  - reformat the system volume + golden rewrite\n",
                      "  repair full  - 重格式化系统卷并完整重写黄金镜像\n" },
    [SH_RETURN]   = { "  return       - reboot into OpenCubeOS (normal-boot mailbox)\n",
                      "  return       - 重启进入 OpenCubeOS（正常启动邮箱）\n" },
    [SH_SHUTDOWN] = { "  shutdown     - power off the machine (ACPI S5)\n",
                      "  shutdown     - 关机（ACPI S5）\n" },
    [SH_REBOOT]   = { "  reboot       - reset the machine\n",
                      "  reboot       - 重启机器\n" },
    [SH_LANG_NOW] = { "  lang         - show or set the language: lang [en|zh]\n",
                      "  lang         - 显示或设置语言：lang [en|zh]\n" },
    [SH_UNKNOWN]  = { "Unknown command: ",                         "未知命令：" },
    [SH_USAGE_REPAIR] = { "usage: repair quick | repair full\n",  "用法：repair quick | repair full\n" },
};

static const char* sh_tr(int key) {
    return g_shell_msgs[key][g_shell_zh ? 1 : 0];
}

static void cmd_help(void) {
    ocire_puts(sh_tr(SH_TITLE));
    ocire_puts(sh_tr(SH_HELP));
    ocire_puts(sh_tr(SH_LOGREAD));
    ocire_puts(sh_tr(SH_SYSCHECK));
    ocire_puts(sh_tr(SH_RQUICK));
    ocire_puts(sh_tr(SH_RFULL));
    ocire_puts(sh_tr(SH_RETURN));
    ocire_puts(sh_tr(SH_SHUTDOWN));
    ocire_puts(sh_tr(SH_REBOOT));
    ocire_puts(sh_tr(SH_LANG_NOW));
}

static void cmd_lang(const char* arg) {
    if (arg[0] == 'z' && arg[1] == 'h' && arg[2] == 0) g_shell_zh = 1;
    else if (arg[0] == 'e' && arg[1] == 'n' && arg[2] == 0) g_shell_zh = 0;
    ocire_puts(g_shell_zh ? "\xe8\xaf\xad\xe8\xa8\x80\xef\xbc\x9a\xe4\xb8\xad\xe6\x96\x87\n"
                          : "language: english\n");
}

static void cmd_shutdown(void) {
    ocire_puts("Shutting down...\n");
    /* Mainstream ACPI S5 first. */
    if (ocire_acpi_enter_s5() != 0) {
        /* Legacy port fallbacks (QEMU/PIIX4, Bochs, APM). */
        oc_outw(0x604, 0x2000);    /* QEMU PIIX4 PM1a_CNT SLP_EN|SLP_TYP */
        oc_outw(0xB004, 0x2000);   /* Bochs */
        oc_outw(0xF000, 0x0000);   /* legacy APM */
        for (volatile int i = 0; i < 2000000; i++) { }
        ocire_puts("Power off not supported on this machine; halting.\n");
        oc_halt();
    }
    /* unreachable when power off works */
    oc_halt();
}

static void cmd_reboot(void) {
    ocire_puts("Rebooting...\n");
    /* 8042 keyboard-controller pulse first (mainstream PC reset). */
    for (int i = 0; i < 100000; i++) {
        if (!(oc_inb(0x64) & 2)) break;
    }
    oc_outb(0x64, 0xFE);
    /* ACPI RESET_REG next (QEMU/edk2: port 0xCF9). */
    ocire_acpi_reset();
    /* Raw 0xCF9 "hard reset" sequence as the final fallback. */
    oc_outb(0xCF9, 0x02);
    oc_outb(0xCF9, 0x06);
    for (volatile int i = 0; i < 2000000; i++) { }
    ocire_puts("Reset failed on this machine; halting.\n");
    oc_halt();
}

static void read_line(char *buf, int cap) {
    int len = 0;
    for (;;) {
        char c;
        while (!ocire_getchar(&c)) { }
        if (c == '\n') {
            ocire_putc('\n');
            break;
        }
        if (c == '\b' || c == 0x7F) {
            if (len > 0) {
                len--;
                ocire_putc('\b');
                ocire_putc(' ');
                ocire_putc('\b');
            }
            continue;
        }
        if (c == '\r') continue;      /* ignore stray CR */
        if (len < cap - 1) {
            buf[len++] = c;
            ocire_putc(c);             /* echo */
        }
    }
    buf[len] = 0;
}

void ocire_shell_run(void) {
    static char line[LINE_MAX];
    for (;;) {
        ocire_puts(OCIRE_PROMPT);
        read_line(line, LINE_MAX);
        /* Trim leading spaces. */
        char *s = line;
        while (*s == ' ') s++;
        if (!s[0]) continue;
        if (oc_strcmp(s, "help") == 0) {
            cmd_help();
        } else if (oc_strcmp(s, "logread") == 0) {
            ocire_cmd_logread();
        } else if (oc_strcmp(s, "syscheck") == 0) {
            ocire_cmd_syscheck();
        } else if (oc_strcmp(s, "repair quick") == 0) {
            ocire_cmd_repair_quick();
        } else if (oc_strcmp(s, "repair full") == 0) {
            ocire_cmd_repair_full();
        } else if (oc_strcmp(s, "return") == 0) {
            ocire_cmd_return();            /* does not return */
        } else if (oc_strcmp(s, "repair") == 0) {
            ocire_puts(sh_tr(SH_USAGE_REPAIR));
        } else if (oc_strcmp(s, "lang") == 0) {
            cmd_lang("");
        } else if (oc_strncmp(s, "lang ", 5) == 0) {
            cmd_lang(s + 5);
        } else if (oc_strcmp(s, "shutdown") == 0) {
            cmd_shutdown();            /* does not return on success */
        } else if (oc_strcmp(s, "reboot") == 0) {
            cmd_reboot();              /* does not return on success */
        } else {
            ocire_puts(sh_tr(SH_UNKNOWN));
            ocire_puts(s);
            ocire_puts("  (try 'help')\n");
        }
    }
}
