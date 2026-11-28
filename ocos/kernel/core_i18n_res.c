/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP-runtime-hardening-and-experience-rebase
 * File: ocos/kernel/core_i18n_res.c
 * Purpose: the lang_en / lang_zh system message tables and the
 *          get_text(MSG_ID) accessor (the 4.4 resource layer; see
 *          core_i18n_res.h and ocos/interface/l1_i18n_res.h for the
 *          contract and the append-only extension protocol).
 *
 *          zh entries are UTF-8; the CJK console pipeline renders
 *          them (16x16) and the serial tee passes the exact bytes
 *          through (utf8) or transcoded (gbk) - the WP-tui TTY work.
 */
#include "core_i18n_res.h"
#include "core_lib_i18n.h"   /* disp_lang_get + DISP_LANG_ZH */

/* The build gate: both tables carry exactly MSG_ID_COUNT entries. */
_Static_assert(MSG_ID_COUNT > 0, "empty message id set");

const char *const lang_en[MSG_ID_COUNT] = {
    /* kernel boot */
    "[oc] FATAL: bad multiboot2 magic",
    "[oc] FATAL: multiboot2 parse failed",
    "[oc] FATAL: fb_init failed",
    "l1 launcher: failed to start the default L1 app; entering recovery shell",
    "l1 launcher: default app recovered",
    /* kernel core */
    "PANIC: idle task stack allocation failed - halting",
    "System halted.",
    "\nKERNEL PANIC (no crash handler installed): ",
    "\r\n*** NESTED #PF INSIDE EXCEPTION HANDLER - HALTING ***\r\n",
    "[user] process killed by exception (vector=",
    "  (faulting address)",
    /* usermode ELF / process errors */
    "[user] no address space",
    "[user] not an ELF file",
    "user: process table full",
    "[user] bad phentsize",
    "[user] PT_LOAD vaddr+memsz overflow",
    "[user] PT_LOAD vaddr in kernel space",
    "dynlink: main image failed ELF header checks",
    "dynlink: failed to map main PT_LOAD",
    /* update / OTA human lines */
    "update: this is a live/ISO environment - direct updates are not supported here. Install to an A/B disk first.",
    "update: no A/B disk - install to an A/B disk first (install / abdisk)",
    "update wizard: no A/B disk in this live environment",
    "update wizard: check for updates only (update check) - or attach an A/B disk",
    /* block / disk */
    "block: no disk found",
    "block: read failed",
    "block: write failed",
    /* filesystem */
    "fs: file not found",
    "fs: path is a directory",
    "fs: no space left",
    "fs: read-only filesystem",
    /* network */
    "net: interface is down",
    "net: no route to host",
    "net: DNS resolution failed - check the server address",
    /* shell / common UX */
    "command not found",
    "bad arguments",
    "permission denied",
    "done",
    "cancelled",
    /* config / serial */
    "config: unknown key",
    "config saved",
    "serial encoding set",
    "serial encoding",
    "serial-probe: pick the line that reads correctly",
    "serial-probe: sample 1 - UTF-8:",
    "serial-probe: sample 2 - GBK:",
    "serial-probe: enter 1 (UTF-8) or 2 (GBK)",
    "serial-probe: configuration saved",
    /* TUI hidden-entry interception */
    "entry hidden by build configuration (CONFIG_TUI)",
    /* misc / diagnostics */
    "not supported here",
    "device not found",
    /* update / OTA labels (appended P2A-L0-I18N, mirrors the enum tail) */
    "Changes: ",
};

_Static_assert(sizeof(lang_en) / sizeof(lang_en[0]) == MSG_ID_COUNT,
               "lang_en entry count drift");

const char *const lang_zh[MSG_ID_COUNT] = {
    /* kernel boot */
    "[内核] 致命错误：multiboot2 魔数错误",
    "[内核] 致命错误：multiboot2 解析失败",
    "[内核] 致命错误：帧缓冲初始化失败",
    "L1 启动器：默认 L1 程序启动失败，进入恢复控制台",
    "L1 启动器：默认程序已恢复",
    /* kernel core */
    "致命错误：空闲任务栈分配失败——系统停止",
    "系统已停止。",
    "\n内核致命错误（未安装崩溃处理器）：",
    "\r\n*** 异常处理器内发生嵌套缺页——系统停止 ***\r\n",
    "[用户] 进程因异常被终止（向量=",
    "  （出错地址）",
    /* usermode ELF / process errors */
    "[用户] 无可用地址空间",
    "[用户] 不是 ELF 文件",
    "用户：进程表已满",
    "[用户] phentsize 非法",
    "[用户] PT_LOAD 地址与长度溢出",
    "[用户] PT_LOAD 地址落入内核空间",
    "动态链接：主镜像 ELF 头校验失败",
    "动态链接：主镜像 PT_LOAD 映射失败",
    /* update / OTA human lines */
    "更新：当前为 Live/ISO 临时环境，不支持直接更新。请先安装到 A/B 磁盘。",
    "更新：未发现 A/B 磁盘——请先安装（install / abdisk）",
    "更新向导：当前 Live 环境中没有 A/B 磁盘",
    "更新向导：仅可检查更新（update check）——或接入 A/B 磁盘",
    /* block / disk */
    "块设备：未发现磁盘",
    "块设备：读取失败",
    "块设备：写入失败",
    /* filesystem */
    "文件系统：文件不存在",
    "文件系统：路径是目录",
    "文件系统：空间不足",
    "文件系统：只读文件系统",
    /* network */
    "网络：接口未启用",
    "网络：没有到主机的路由",
    "网络：DNS 解析失败——请检查服务器地址",
    /* shell / common UX */
    "未找到命令",
    "参数错误",
    "权限不足",
    "完成",
    "已取消",
    /* config / serial */
    "配置：未知的键",
    "配置已保存",
    "串口编码已设置",
    "串口编码",
    "serial-probe：请选择显示正常的一行",
    "serial-probe：样例 1——UTF-8 编码：",
    "serial-probe：样例 2——GBK 编码：",
    "serial-probe：输入 1（UTF-8）或 2（GBK）",
    "serial-probe：配置已保存",
    /* TUI hidden-entry interception */
    "入口已被构建配置隐藏（CONFIG_TUI）",
    /* misc / diagnostics */
    "此处不支持该操作",
    "未找到设备",
    /* update / OTA labels (appended P2A-L0-I18N, mirrors the enum tail) */
    "更新内容：",
};

_Static_assert(sizeof(lang_zh) / sizeof(lang_zh[0]) == MSG_ID_COUNT,
               "lang_zh entry count drift");

const char *get_text(msg_id_t id)
{
    if ((int)id < 0 || (int)id >= MSG_ID_COUNT)
        return "?";
    if (disp_lang_get() == DISP_LANG_ZH && lang_zh[id] && lang_zh[id][0])
        return lang_zh[id];
    return lang_en[id] ? lang_en[id] : "?";
}
