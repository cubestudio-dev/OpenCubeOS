/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-10-chinese_support-fix4
 * File: tui/tui_sys.c
 * Purpose: System settings TUI (fix4 4.2.3): the configuration surface
 * for everything that is NOT a raw hardware domain - host identity,
 * network, storage view, user/security posture, update settings, log
 * level and TUI look-and-feel. Every mutable setting is stored in
 * /etc/opencube.conf through lib_config, the SAME keys the shell
 * exposes via `config get/set` - one source of truth, two frontends
 * (TUI <=> shell parity, fix4 4.2.4).
 *
 * Pages (Tab cycles, fix4 L1 extension registry):
 *   host     hostname / shell language / keyboard layout
 *   network  IP, mask, gateway, DNS, firewall policy + rules
 *   storage  block devices, partitions, mounts
 *   user     honest single-user posture (no multi-user shadow)
 *   security TOFU / known_hosts posture + firewall quick toggles
 *   update   update URL, online_update, A/B slot state
 *   log      log level (0 default / 1 quiet)
 *   tui      theme (tui_ext 7.5 registry)
 */
#include "tui.h"
#include "tui_desktop.h"   /* WP-10-tui-native: tui_tr */
#include "../kernel/lib/lib_string.h"
#include "../kernel/lib/lib_config.h"
#include "../kernel/lib/lib_log.h"
#include "../net/net_core.h"
#include "../kernel/ota/ota_ab.h"
#include "../fs/fs_vfs.h"
#include "../drivers/block/driver_block_blk.h"
#include "../drivers/input/driver_input_keyboard.h"

int tui_sys_main(void);   /* menu action target, defined below */

/* ---- shared helpers --------------------------------------------------- */

static void sys_ip_str(u32 ip, char *out) {
    char n[8];
    u64_to_str((u64)((ip >> 24) & 0xFF), n); strcpy(out, n); strcat(out, ".");
    u64_to_str((u64)((ip >> 16) & 0xFF), n); strcat(out, n); strcat(out, ".");
    u64_to_str((u64)((ip >> 8) & 0xFF), n);  strcat(out, n); strcat(out, ".");
    u64_to_str((u64)(ip & 0xFF), n);         strcat(out, n);
}

static void sys_read_cfg(const char *key, char *out, int cap, const char *def) {
    lib_config_read_default(key, out, cap, def);
}

static void sys_write_cfg(const char *key, const char *val) {
    lib_config_write(key, val);
}

/* ---- page: host -------------------------------------------------------- */


static int page_host_render(tui_window_t *win, int scroll) {
    (void)win;
    char b[100], host[64], lang[8], layout[16];
    int row = 1 - scroll;
    sys_read_cfg("sys.hostname", host, (int)sizeof(host), "opencube");
    sys_read_cfg("shell.lang", lang, (int)sizeof(lang), "en");
    sys_read_cfg("kbd.layout", layout, (int)sizeof(layout), "us");
    if (row >= 1) tui_print_at(row++, 2, tui_tr("Host identity (persisted in /etc/opencube.conf)",
                                 "主机标识（持久化于 /etc/opencube.conf）"));
    if (row >= 1) {
        strcpy(b, tui_tr("  hostname : ", "  主机名　：")); strcat(b, host);
        strcat(b, tui_tr("   [h: edit]", "   [h: 编辑]"));
        tui_print_at(row++, 2, b);
    }
    if (row >= 1) {
        strcpy(b, tui_tr("  shell language : ", "  shell 语言　　：")); strcat(b, lang);
        strcat(b, tui_tr("   [l: toggle en/zh, like `lang`]", "   [l: 切换 en/zh，同 `lang`]"));
        tui_print_at(row++, 2, b);
    }
    if (row >= 1) {
        strcpy(b, tui_tr("  keyboard layout: ", "  键盘布局　　：")); strcat(b, layout);
        strcat(b, tui_tr("   [k: toggle us/dvorak, applies live]", "   [k: 切换 us/dvorak，实时生效]"));
        tui_print_at(row++, 2, b);
    }
    if (row >= 1) {
        tui_print_at(row++, 2, tui_tr("  shell parity: config get sys.hostname / config set sys.hostname X",
                                 "  shell 对等：config get sys.hostname / config set sys.hostname X"));
    }
    if (row >= 1) tui_print_at(row++, 2, tui_tr("Keys: h set hostname | l language | k layout | F2 persist all",
                                 "按键：h 主机名 | l 语言 | k 布局 | F2 全部持久化"));
    return row + scroll - 1;
}

static int page_host_input(int key) {
    if (key == 'l') {
        char lang[8];
        sys_read_cfg("shell.lang", lang, (int)sizeof(lang), "en");
        const char *next = (strcmp(lang, "zh") == 0) ? "en" : "zh";
        sys_write_cfg("shell.lang", next);
        {
            extern void shell_lang_set_str(const char *s);
            shell_lang_set_str(next);
        }
        char sb[64]; strcpy(sb, "shell.lang = "); strcat(sb, next);
        strcat(sb, tui_tr(" (applied; F2 persists)", "（已生效；F2 持久化）"));
        tui_status_line(sb);
        return 1;
    }
    if (key == 'k') {
        const char *cur = driver_input_keyboard_get_layout();
        const char *next = (strcmp(cur, "dvorak") == 0) ? "us" : "dvorak";
        driver_input_keyboard_set_layout(next);
        sys_write_cfg("kbd.layout", next);
        char sb[64]; strcpy(sb, "keyboard layout = "); strcat(sb, next);
        strcat(sb, tui_tr(" (applied live; F2 persists)", "（实时生效；F2 持久化）"));
        tui_status_line(sb);
        return 1;
    }
    if (key == 'h') {
        char host[64];
        sys_read_cfg("sys.hostname", host, (int)sizeof(host), "opencube");
        const char *next = (strcmp(host, "opencube") == 0) ? "cube-01" : "opencube";
        sys_write_cfg("sys.hostname", next);
        char sb[64]; strcpy(sb, "sys.hostname = "); strcat(sb, next);
        strcat(sb, tui_tr(" (F2 persists; shell reads the same key)", "（F2 持久化；shell 读同一键）"));
        tui_status_line(sb);
        return 1;
    }
    if (key == TUI_KEY_F(2)) {
        /* persist everything this page touched (config writes are
         * already durable through lib_config; re-write the ones the
         * page may have changed so one keypress captures them all) */
        char v[64];
        sys_read_cfg("sys.hostname", v, (int)sizeof(v), "opencube");
        sys_write_cfg("sys.hostname", v);
        sys_read_cfg("shell.lang", v, (int)sizeof(v), "en");
        sys_write_cfg("shell.lang", v);
        sys_read_cfg("kbd.layout", v, (int)sizeof(v), "us");
        sys_write_cfg("kbd.layout", v);
        tui_status_line(tui_tr("host settings persisted to /etc/opencube.conf",
                             "主机设置已持久化到 /etc/opencube.conf"));
        return 1;
    }
    return 0;
}

/* ---- page: network ------------------------------------------------------ */

static int page_net_render(tui_window_t *win, int scroll) {
    (void)win;
    char b[100], ip[16], gw[16], dns[16], mk[16];
    int row = 1 - scroll;
    u32 ip4 = net_get_ip();
    sys_ip_str(ip4, ip);
    sys_ip_str(net_get_gateway(), gw);
    sys_ip_str(net_get_dns(), dns);
    sys_ip_str(net_get_mask(), mk);
    if (row >= 1) tui_print_at(row++, 2, tui_tr("Network (live kernel state; configure via dhcp/ip/dns/route)",
                                 "网络（内核实时状态；经 dhcp/ip/dns/route 配置）"));
    if (row >= 1) {
        strcpy(b, tui_tr("  IP      : ", "  IP      ：")); strcat(b, ip4 ? ip : tui_tr("none (no link configured)", "无（链路未配置）"));
        tui_print_at(row++, 2, b);
    }
    if (row >= 1) {
        strcpy(b, tui_tr("  netmask : ", "  掩码   ：")); strcat(b, mk);
        tui_print_at(row++, 2, b);
    }
    if (row >= 1) {
        strcpy(b, tui_tr("  gateway : ", "  网关   ：")); strcat(b, gw);
        tui_print_at(row++, 2, b);
    }
    if (row >= 1) {
        strcpy(b, tui_tr("  DNS     : ", "  DNS    ：")); strcat(b, dns);
        tui_print_at(row++, 2, b);
    }
    if (row >= 1) {
        u8 pol = net_netfilter_get_policy(0);
        strcpy(b, tui_tr("  firewall INPUT default: ", "  防火墙 INPUT 默认　　："));
        strcat(b, pol == 1 ? "ACCEPT" : (pol == 0 ? "DROP" : tui_tr("unset", "未设置")));
        strcat(b, tui_tr("   [f: toggle]", "   [f: 切换]"));
        tui_print_at(row++, 2, b);
    }
    if (row >= 1) {
        net_netfilter_rule_t rules[8];
        int n = net_netfilter_list_rules(rules, 8);
        strcpy(b, tui_tr("  rules   : ", "  规则   ："));
        u64_to_str((u64)(n < 0 ? 0 : n), ip);
        strcat(b, ip); strcat(b, tui_tr(" (see `firewall list`)", "（见 `firewall list`）"));
        tui_print_at(row++, 2, b);
    }
    if (row >= 1) tui_print_at(row++, 2, tui_tr("Keys: f toggle INPUT policy ACCEPT/DROP (live)",
                                 "按键：f 实时切换 INPUT 策略 ACCEPT/DROP"));
    return row + scroll - 1;
}

static int page_net_input(int key) {
    if (key == 'f') {
        u8 pol = net_netfilter_get_policy(0);
        u8 next = (pol == 1) ? 0 : 1;
        net_netfilter_set_policy(0, next);
        tui_status_line(next == 1 ?
            tui_tr("firewall INPUT default = ACCEPT (live)", "防火墙 INPUT 默认 = ACCEPT（实时）") :
            tui_tr("firewall INPUT default = DROP (live)", "防火墙 INPUT 默认 = DROP（实时）"));
        return 1;
    }
    return 0;
}

/* ---- page: storage ------------------------------------------------------ */

static int page_storage_render(tui_window_t *win, int scroll) {
    (void)win;
    char b[100], n[16];
    int row = 1 - scroll;
    if (row >= 1) tui_print_at(row++, 2, tui_tr("Storage (block devices, partitions, mounts)",
                                 "存储（块设备、分区、挂载）"));
    if (row >= 1) {
        int ndev = driver_block_num_devices();
        strcpy(b, tui_tr("  block devices: ", "  块设备　　："));
        u64_to_str((u64)ndev, n);
        strcat(b, n);
        strcat(b, tui_tr("  (see `lsblk` for the full table)", " （完整表见 `lsblk`）"));
        tui_print_at(row++, 2, b);
    }
    if (row >= 1) tui_print_at(row++, 2, tui_tr("  mounts    : see `df` / `mount` (same VFS tables)",
                                 "  挂载　　：见 `df` / `mount`（同一 VFS 表）"));
    if (row >= 1) tui_print_at(row++, 2, tui_tr("  A/B slots : see the update page (Tab)",
                                 "  A/B 槽　：见更新页（Tab）"));
    if (row >= 1) tui_print_at(row++, 2, tui_tr("Partition/fstab editing stays in the shell (fdisk/mount) -",
                                 "分区/fstab 编辑保留在 shell（fdisk/mount）——"));
    if (row >= 1) tui_print_at(row++, 2, tui_tr("this page mirrors the live state so both views agree.",
                                 "本页镜像实时状态，两个视图保持一致。"));
    return row + scroll - 1;
}

/* ---- page: user ---------------------------------------------------------- */

static int page_user_render(tui_window_t *win, int scroll) {
    (void)win;
    int row = 1 - scroll;
    if (row >= 1) tui_print_at(row++, 2, tui_tr("Users (honest single-user kernel posture)",
                                 "用户（诚实的单用户内核姿态）"));
    if (row >= 1) tui_print_at(row++, 2, tui_tr("  L0 runs a single privileged shell (ring 0) and ring-3 tasks",
                                 "  L0 运行单一特权 shell（Ring 0）与 Ring 3 任务"));
    if (row >= 1) tui_print_at(row++, 2, tui_tr("  via SYS_fork/execute; there is no multi-user shadow table.",
                                 "  经 SYS_fork/execute；无多用户影子表。"));
    if (row >= 1) tui_print_at(row++, 2, tui_tr("  create/delete users: NOT BUILT (no /etc/passwd in L0)",
                                 "  创建/删除用户：未构建（L0 无 /etc/passwd）"));
    if (row >= 1) tui_print_at(row++, 2, tui_tr("  passwords: SSH server key-based only (see `ssh-keygen`)",
                                 "  密码：SSH 服务器仅密钥认证（见 `ssh-keygen`）"));
    if (row >= 1) tui_print_at(row++, 2, tui_tr("  user API for L1 extensions: l1_ext_process_* (fork/exec)",
                                 "  L1 扩展用户 API：l1_ext_process_*（fork/exec）"));
    return row + scroll - 1;
}

/* ---- page: security ------------------------------------------------------- */

static int page_sec_render(tui_window_t *win, int scroll) {
    (void)win;
    char b[100], m[8];
    int row = 1 - scroll;
    u8 pol = net_netfilter_get_policy(0);
    net_netfilter_rule_t rules[8];
    int nr = net_netfilter_list_rules(rules, 8);
    if (row >= 1) tui_print_at(row++, 2, tui_tr("Security posture", "安全态势"));
    if (row >= 1) {
        strcpy(b, tui_tr("  firewall INPUT default : ", "  防火墙 INPUT 默认　　："));
        strcat(b, pol == 1 ? "ACCEPT" : (pol == 0 ? "DROP" : tui_tr("unset", "未设置")));
        tui_print_at(row++, 2, b);
    }
    if (row >= 1) {
        strcpy(b, tui_tr("  netfilter rules        : ", "  netfilter 规则　　　　："));
        u64_to_str((u64)(nr < 0 ? 0 : nr), m);
        strcat(b, m);
        tui_print_at(row++, 2, b);
    }
    if (row >= 1) {
        strcpy(b, tui_tr("  conntrack entries      : ", "  连接跟踪条目　　　　　："));
        u64_to_str((u64)net_netfilter_ct_count(), m);
        strcat(b, m);
        tui_print_at(row++, 2, b);
    }
    if (row >= 1) tui_print_at(row++, 2, tui_tr("  SSH host auth          : TOFU + known_hosts (`ssh` client)",
                                 "  SSH 主机认证　　　　　：TOFU + known_hosts（`ssh` 客户端）"));
    if (row >= 1) tui_print_at(row++, 2, tui_tr("  TLS certificates       : X.509 chain verify (`cryptotest`)",
                                 "  TLS 证书　　　　　　　：X.509 链验证（`cryptotest`）"));
    if (row >= 1) tui_print_at(row++, 2, tui_tr("Keys: f toggle INPUT policy | c flush conntrack",
                                 "按键：f 切换 INPUT 策略 | c 清空连接跟踪"));
    return row + scroll - 1;
}

static int page_sec_input(int key) {
    if (key == 'f') {
        u8 pol = net_netfilter_get_policy(0);
        u8 next = (pol == 1) ? 0 : 1;
        net_netfilter_set_policy(0, next);
        tui_status_line(next == 1 ? tui_tr("INPUT default = ACCEPT", "INPUT 默认 = ACCEPT")
                            : tui_tr("INPUT default = DROP", "INPUT 默认 = DROP"));
        return 1;
    }
    if (key == 'c') {
        net_netfilter_ct_flush();
        tui_status_line(tui_tr("conntrack flushed", "连接跟踪已清空"));
        return 1;
    }
    return 0;
}

/* ---- page: update ----------------------------------------------------------- */

static int page_update_render(tui_window_t *win, int scroll) {
    (void)win;
    char b[100], url[96], v[16];
    int row = 1 - scroll;
    ota_update_status_t st;
    sys_read_cfg("update_url", url, (int)sizeof(url), "(unset)");
    int rc = ota_update_get_status(&st);
    if (row >= 1) tui_print_at(row++, 2, tui_tr("Update (config keys shared with `update`/`checkupdate`)",
                                 "更新（配置键与 `update`/`checkupdate` 共享）"));
    if (row >= 1) {
        strcpy(b, tui_tr("  update_url    : ", "  update_url　　：")); strcat(b, url);
        tui_print_at(row++, 2, b);
    }
    if (row >= 1) {
        strcpy(b, tui_tr("  online_update : ", "  online_update："));
        sys_read_cfg("online_update", v, (int)sizeof(v), "1");
        strcat(b, (v[0] == '0') ? tui_tr("0 (manual checks only)", "0（仅手动检查）")
                                : tui_tr("1 (checkupdate allowed)", "1（允许 checkupdate）"));
        strcat(b, tui_tr("   [o: toggle]", "   [o: 切换]"));
        tui_print_at(row++, 2, b);
    }
    if (row >= 1) {
        strcpy(b, tui_tr("  current ver   : ", "  当前版本　　　："));
        strcat(b, rc == 0 ? st.current_version : tui_tr("(status unavailable)", "（状态不可用）"));
        tui_print_at(row++, 2, b);
    }
    if (row >= 1) {
        strcpy(b, tui_tr("  boot slot     : ", "  启动槽　　　　："));
        strcat(b, rc == 0 ? st.current_boot : "?");
        if (rc == 0 && st.next_boot[0]) { strcat(b, tui_tr(" -> next: ", " -> 下次：")); strcat(b, st.next_boot); }
        strcat(b, tui_tr("   A/B disk: ", "   A/B 盘："));
        strcat(b, (rc == 0 && st.ota_ab_present) ? tui_tr("yes", "有") : tui_tr("no", "无"));
        tui_print_at(row++, 2, b);
    }
    if (row >= 1) {
        strcpy(b, tui_tr("  update state  : ", "  更新状态　　　："));
        if (rc == 0) {
            if (st.ota_update_available == 1) strcat(b, tui_tr("new version available", "有新版本"));
            else if (st.ota_update_available == 0) strcat(b, tui_tr("up to date", "已是最新"));
            else strcat(b, tui_tr("unknown (not checked)", "未知（未检查）"));
        } else strcat(b, tui_tr("unavailable", "不可用"));
        tui_print_at(row++, 2, b);
    }
    if (row >= 1) tui_print_at(row++, 2, tui_tr("Keys: o toggle online_update | F2 persist",
                                 "按键：o 切换 online_update | F2 持久化"));
    return row + scroll - 1;
}

static int page_update_input(int key) {
    if (key == 'o') {
        char v[4];
        sys_read_cfg("online_update", v, (int)sizeof(v), "1");
        const char *next = (v[0] == '0') ? "1" : "0";
        sys_write_cfg("online_update", next);
        char sb[64]; strcpy(sb, "online_update = "); strcat(sb, next);
        tui_status_line(sb);
        return 1;
    }
    if (key == TUI_KEY_F(2)) {
        char v[4];
        sys_read_cfg("online_update", v, (int)sizeof(v), "1");
        sys_write_cfg("online_update", v);
        tui_status_line(tui_tr("update settings persisted", "更新设置已持久化"));
        return 1;
    }
    return 0;
}

/* ---- page: log ------------------------------------------------------------------ */

static int page_log_render(tui_window_t *win, int scroll) {
    (void)win;
    char b[100];
    int row = 1 - scroll;
    int lvl = lib_log_get_level();
    if (row >= 1) tui_print_at(row++, 2, tui_tr("Logging (log.level, shared with `config set log.level N`)",
                                 "日志（log.level，与 `config set log.level N` 共享）"));
    if (row >= 1) {
        strcpy(b, tui_tr("  level : ", "  级别 ："));
        strcat(b, lvl ? tui_tr("1 (quiet: only failures)", "1（安静：仅失败）")
                    : tui_tr("0 (default: everything)", "0（默认：全部）"));
        strcat(b, tui_tr("   [0/1 set]", "   [0/1 设置]"));
        tui_print_at(row++, 2, b);
    }
    if (row >= 1) tui_print_at(row++, 2, tui_tr("  boot log ring : 64 lines (see `tui-sysinfo logs`)",
                                 "  启动日志环　　：64 行（见 `tui-sysinfo logs`）"));
    if (row >= 1) tui_print_at(row++, 2, tui_tr("  rotation      : NOT BUILT (ring is fixed size, oldest dropped)",
                                 "  轮转　　　　　：未构建（环固定大小，最旧丢弃）"));
    if (row >= 1) tui_print_at(row++, 2, tui_tr("Keys: 0 default | 1 quiet | F2 persist",
                                 "按键：0 默认 | 1 安静 | F2 持久化"));
    return row + scroll - 1;
}

static int page_log_input(int key) {
    if (key == '0') { lib_log_set_level(0); tui_status_line(tui_tr("log.level = 0 (default)", "log.level = 0（默认）")); return 1; }
    if (key == '1') { lib_log_set_level(1); tui_status_line(tui_tr("log.level = 1 (quiet, failures only)",
                                                    "log.level = 1（安静，仅失败）")); return 1; }
    if (key == TUI_KEY_F(2)) {
        sys_write_cfg("log.level", lib_log_get_level() ? "1" : "0");
        tui_status_line(tui_tr("log.level persisted", "log.level 已持久化"));
        return 1;
    }
    return 0;
}

/* ---- page: tui look --------------------------------------------------------------- */

static int page_tui_render(tui_window_t *win, int scroll) {
    (void)win;
    char b[100];
    int row = 1 - scroll;
    tui_themeext_t th[8];
    int n = tui_theme_list(th, 8);
    const char *cur = tui_theme_current();
    if (row >= 1) tui_print_at(row++, 2, tui_tr("TUI look & feel (theme registry, tui_ext 7.5)",
                                 "TUI 外观（主题注册表，tui_ext 7.5）"));
    if (row >= 1) {
        strcpy(b, tui_tr("  current theme : ", "  当前主题　　　："));
        strcat(b, cur[0] ? cur : tui_tr("default", "默认"));
        strcat(b, tui_tr("   [t: next theme]", "   [t: 下一个主题]"));
        tui_print_at(row++, 2, b);
    }
    if (row >= 1) tui_print_at(row++, 2, tui_tr("  registered themes:", "  已注册主题："));
    for (int i = 0; i < n && row >= 1; i++) {
        strcpy(b, "    - ");
        strcat(b, th[i].name);
        strcat(b, " (");
        {
            char m[8];
            u64_to_str((u64)th[i].fg, m); strcat(b, m);
            strcat(b, tui_tr(" on ", " 配 "));
            u64_to_str((u64)th[i].bg, m); strcat(b, m);
        }
        strcat(b, ") ");
        strcat(b, th[i].desc);
        tui_print_at(row++, 2, b);
    }
    if (row >= 1) tui_print_at(row++, 2, tui_tr(
        "  backend/font   : fb 8x16 cells (fixed); serial backend via tui.backend",
        "  后端/字体　　　：fb 8×16 单元（固定）；串口后端经 tui.backend"));
    if (row >= 1) tui_print_at(row++, 2, tui_tr("Keys: t cycle themes (applies live) | F2 persist theme",
                                 "按键：t 循环主题（实时生效）| F2 持久化主题"));
    return row + scroll - 1;
}

static int page_tui_input(int key) {
    if (key == 't') {
        tui_themeext_t th[8];
        int n = tui_theme_list(th, 8);
        if (n <= 0) { tui_status_line(tui_tr("no themes registered", "无已注册主题")); return 1; }
        const char *cur = tui_theme_current();
        int pick = 0;
        for (int i = 0; i < n; i++)
            if (strcmp(th[i].name, cur) == 0) pick = (i + 1) % n;
        tui_theme_set(th[pick].name);
        char sb[80]; strcpy(sb, "theme = "); strcat(sb, th[pick].name);
        strcat(sb, tui_tr(" (F2 persists)", "（F2 持久化）"));
        tui_status_line(sb);
        return 1;
    }
    if (key == TUI_KEY_F(2)) {
        const char *cur = tui_theme_current();
        if (cur[0]) sys_write_cfg("tui.theme", cur);
        tui_status_line(tui_tr("tui.theme persisted", "tui.theme 已持久化"));
        return 1;
    }
    return 0;
}

/* ---- registration + entry --------------------------------------------------------- */

static int host_persist_action(void) {
    lib_config_write("log.level", lib_log_get_level() ? "1" : "0");
    tui_status_line(tui_tr("settings persisted to /etc/opencube.conf",
                             "设置已持久化到 /etc/opencube.conf"));
    return 0;
}

void tui_sys_register_pages(void) {
    tui_page_register("builtin-sys", "sys.host", "System: host identity",
                      "system", 20, page_host_render, page_host_input, 0);
    tui_page_register("builtin-sys", "sys.net", "System: network",
                      "system", 21, page_net_render, page_net_input, 0);
    tui_page_register("builtin-sys", "sys.storage", "System: storage",
                      "system", 22, page_storage_render, 0, 0);
    tui_page_register("builtin-sys", "sys.user", "System: users",
                      "system", 23, page_user_render, 0, 0);
    tui_page_register("builtin-sys", "sys.security", "System: security",
                      "system", 24, page_sec_render, page_sec_input, 0);
    tui_page_register("builtin-sys", "sys.update", "System: update",
                      "system", 25, page_update_render, page_update_input, 0);
    tui_page_register("builtin-sys", "sys.log", "System: logging",
                      "system", 26, page_log_render, page_log_input, 0);
    tui_page_register("builtin-sys", "sys.tui", "System: TUI look",
                      "system", 27, page_tui_render, page_tui_input, 0);
    /* menu extension: the main TUI menu picks this up automatically */
    tui_menu_register("builtin-sys", "System settings",
                      "host/network/storage/user/security/update/log/tui",
                      "[S]", "system", 0, 15, tui_sys_main, 0);
    (void)host_persist_action;
}

int tui_sys_main(void) {
    if (!tui_started()) { if (tui_init() != 0) return -1; }
    return tui_ext_page_main("sys.host");
}
