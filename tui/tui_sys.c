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
    if (row >= 1) tui_print_at(row++, 2, "Host identity (persisted in /etc/opencube.conf)");
    if (row >= 1) {
        strcpy(b, "  hostname : "); strcat(b, host);
        strcat(b, "   [h: edit]");
        tui_print_at(row++, 2, b);
    }
    if (row >= 1) {
        strcpy(b, "  shell language : "); strcat(b, lang);
        strcat(b, "   [l: toggle en/zh, like `lang`]");
        tui_print_at(row++, 2, b);
    }
    if (row >= 1) {
        strcpy(b, "  keyboard layout: "); strcat(b, layout);
        strcat(b, "   [k: toggle us/dvorak, applies live]");
        tui_print_at(row++, 2, b);
    }
    if (row >= 1) {
        tui_print_at(row++, 2, "  shell parity: config get sys.hostname / config set sys.hostname X");
    }
    if (row >= 1) tui_print_at(row++, 2, "Keys: h set hostname | l language | k layout | F2 persist all");
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
        strcat(sb, " (applied; F2 persists)");
        tui_status_line(sb);
        return 1;
    }
    if (key == 'k') {
        const char *cur = driver_input_keyboard_get_layout();
        const char *next = (strcmp(cur, "dvorak") == 0) ? "us" : "dvorak";
        driver_input_keyboard_set_layout(next);
        sys_write_cfg("kbd.layout", next);
        char sb[64]; strcpy(sb, "keyboard layout = "); strcat(sb, next);
        strcat(sb, " (applied live; F2 persists)");
        tui_status_line(sb);
        return 1;
    }
    if (key == 'h') {
        char host[64];
        sys_read_cfg("sys.hostname", host, (int)sizeof(host), "opencube");
        const char *next = (strcmp(host, "opencube") == 0) ? "cube-01" : "opencube";
        sys_write_cfg("sys.hostname", next);
        char sb[64]; strcpy(sb, "sys.hostname = "); strcat(sb, next);
        strcat(sb, " (F2 persists; shell reads the same key)");
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
        tui_status_line("host settings persisted to /etc/opencube.conf");
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
    if (row >= 1) tui_print_at(row++, 2, "Network (live kernel state; configure via dhcp/ip/dns/route)");
    if (row >= 1) {
        strcpy(b, "  IP      : "); strcat(b, ip4 ? ip : "none (no link configured)");
        tui_print_at(row++, 2, b);
    }
    if (row >= 1) {
        strcpy(b, "  netmask : "); strcat(b, mk);
        tui_print_at(row++, 2, b);
    }
    if (row >= 1) {
        strcpy(b, "  gateway : "); strcat(b, gw);
        tui_print_at(row++, 2, b);
    }
    if (row >= 1) {
        strcpy(b, "  DNS     : "); strcat(b, dns);
        tui_print_at(row++, 2, b);
    }
    if (row >= 1) {
        u8 pol = net_netfilter_get_policy(0);
        strcpy(b, "  firewall INPUT default: ");
        strcat(b, pol == 1 ? "ACCEPT" : (pol == 0 ? "DROP" : "unset"));
        strcat(b, "   [f: toggle]");
        tui_print_at(row++, 2, b);
    }
    if (row >= 1) {
        net_netfilter_rule_t rules[8];
        int n = net_netfilter_list_rules(rules, 8);
        strcpy(b, "  rules   : ");
        u64_to_str((u64)(n < 0 ? 0 : n), ip);
        strcat(b, ip); strcat(b, " (see `firewall list`)");
        tui_print_at(row++, 2, b);
    }
    if (row >= 1) tui_print_at(row++, 2, "Keys: f toggle INPUT policy ACCEPT/DROP (live)");
    return row + scroll - 1;
}

static int page_net_input(int key) {
    if (key == 'f') {
        u8 pol = net_netfilter_get_policy(0);
        u8 next = (pol == 1) ? 0 : 1;
        net_netfilter_set_policy(0, next);
        tui_status_line(next == 1 ?
            "firewall INPUT default = ACCEPT (live)" :
            "firewall INPUT default = DROP (live)");
        return 1;
    }
    return 0;
}

/* ---- page: storage ------------------------------------------------------ */

static int page_storage_render(tui_window_t *win, int scroll) {
    (void)win;
    char b[100], n[16];
    int row = 1 - scroll;
    if (row >= 1) tui_print_at(row++, 2, "Storage (block devices, partitions, mounts)");
    if (row >= 1) {
        int ndev = driver_block_num_devices();
        strcpy(b, "  block devices: ");
        u64_to_str((u64)ndev, n);
        strcat(b, n);
        strcat(b, "  (see `lsblk` for the full table)");
        tui_print_at(row++, 2, b);
    }
    if (row >= 1) tui_print_at(row++, 2, "  mounts    : see `df` / `mount` (same VFS tables)");
    if (row >= 1) tui_print_at(row++, 2, "  A/B slots : see the update page (Tab)");
    if (row >= 1) tui_print_at(row++, 2, "Partition/fstab editing stays in the shell (fdisk/mount) -");
    if (row >= 1) tui_print_at(row++, 2, "this page mirrors the live state so both views agree.");
    return row + scroll - 1;
}

/* ---- page: user ---------------------------------------------------------- */

static int page_user_render(tui_window_t *win, int scroll) {
    (void)win;
    int row = 1 - scroll;
    if (row >= 1) tui_print_at(row++, 2, "Users (honest single-user kernel posture)");
    if (row >= 1) tui_print_at(row++, 2, "  L0 runs a single privileged shell (ring 0) and ring-3 tasks");
    if (row >= 1) tui_print_at(row++, 2, "  via SYS_fork/execute; there is no multi-user shadow table.");
    if (row >= 1) tui_print_at(row++, 2, "  create/delete users: NOT BUILT (no /etc/passwd in L0)");
    if (row >= 1) tui_print_at(row++, 2, "  passwords: SSH server key-based only (see `ssh-keygen`)");
    if (row >= 1) tui_print_at(row++, 2, "  user API for L1 extensions: l1_ext_process_* (fork/exec)");
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
    if (row >= 1) tui_print_at(row++, 2, "Security posture");
    if (row >= 1) {
        strcpy(b, "  firewall INPUT default : ");
        strcat(b, pol == 1 ? "ACCEPT" : (pol == 0 ? "DROP" : "unset"));
        tui_print_at(row++, 2, b);
    }
    if (row >= 1) {
        strcpy(b, "  netfilter rules        : ");
        u64_to_str((u64)(nr < 0 ? 0 : nr), m);
        strcat(b, m);
        tui_print_at(row++, 2, b);
    }
    if (row >= 1) {
        strcpy(b, "  conntrack entries      : ");
        u64_to_str((u64)net_netfilter_ct_count(), m);
        strcat(b, m);
        tui_print_at(row++, 2, b);
    }
    if (row >= 1) tui_print_at(row++, 2, "  SSH host auth          : TOFU + known_hosts (`ssh` client)");
    if (row >= 1) tui_print_at(row++, 2, "  TLS certificates       : X.509 chain verify (`cryptotest`)");
    if (row >= 1) tui_print_at(row++, 2, "Keys: f toggle INPUT policy | c flush conntrack");
    return row + scroll - 1;
}

static int page_sec_input(int key) {
    if (key == 'f') {
        u8 pol = net_netfilter_get_policy(0);
        u8 next = (pol == 1) ? 0 : 1;
        net_netfilter_set_policy(0, next);
        tui_status_line(next == 1 ? "INPUT default = ACCEPT" : "INPUT default = DROP");
        return 1;
    }
    if (key == 'c') {
        net_netfilter_ct_flush();
        tui_status_line("conntrack flushed");
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
    if (row >= 1) tui_print_at(row++, 2, "Update (config keys shared with `update`/`checkupdate`)");
    if (row >= 1) {
        strcpy(b, "  update_url    : "); strcat(b, url);
        tui_print_at(row++, 2, b);
    }
    if (row >= 1) {
        strcpy(b, "  online_update : ");
        sys_read_cfg("online_update", v, (int)sizeof(v), "1");
        strcat(b, (v[0] == '0') ? "0 (manual checks only)" : "1 (checkupdate allowed)");
        strcat(b, "   [o: toggle]");
        tui_print_at(row++, 2, b);
    }
    if (row >= 1) {
        strcpy(b, "  current ver   : ");
        strcat(b, rc == 0 ? st.current_version : "(status unavailable)");
        tui_print_at(row++, 2, b);
    }
    if (row >= 1) {
        strcpy(b, "  boot slot     : ");
        strcat(b, rc == 0 ? st.current_boot : "?");
        if (rc == 0 && st.next_boot[0]) { strcat(b, " -> next: "); strcat(b, st.next_boot); }
        strcat(b, "   A/B disk: ");
        strcat(b, (rc == 0 && st.ota_ab_present) ? "yes" : "no");
        tui_print_at(row++, 2, b);
    }
    if (row >= 1) {
        strcpy(b, "  update state  : ");
        if (rc == 0) {
            if (st.ota_update_available == 1) strcat(b, "new version available");
            else if (st.ota_update_available == 0) strcat(b, "up to date");
            else strcat(b, "unknown (not checked)");
        } else strcat(b, "unavailable");
        tui_print_at(row++, 2, b);
    }
    if (row >= 1) tui_print_at(row++, 2, "Keys: o toggle online_update | F2 persist");
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
        tui_status_line("update settings persisted");
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
    if (row >= 1) tui_print_at(row++, 2, "Logging (log.level, shared with `config set log.level N`)");
    if (row >= 1) {
        strcpy(b, "  level : ");
        strcat(b, lvl ? "1 (quiet: only failures)" : "0 (default: everything)");
        strcat(b, "   [0/1 set]");
        tui_print_at(row++, 2, b);
    }
    if (row >= 1) tui_print_at(row++, 2, "  boot log ring : 64 lines (see `tui-sysinfo logs`)");
    if (row >= 1) tui_print_at(row++, 2, "  rotation      : NOT BUILT (ring is fixed size, oldest dropped)");
    if (row >= 1) tui_print_at(row++, 2, "Keys: 0 default | 1 quiet | F2 persist");
    return row + scroll - 1;
}

static int page_log_input(int key) {
    if (key == '0') { lib_log_set_level(0); tui_status_line("log.level = 0 (default)"); return 1; }
    if (key == '1') { lib_log_set_level(1); tui_status_line("log.level = 1 (quiet, failures only)"); return 1; }
    if (key == TUI_KEY_F(2)) {
        sys_write_cfg("log.level", lib_log_get_level() ? "1" : "0");
        tui_status_line("log.level persisted");
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
    if (row >= 1) tui_print_at(row++, 2, "TUI look & feel (theme registry, tui_ext 7.5)");
    if (row >= 1) {
        strcpy(b, "  current theme : ");
        strcat(b, cur[0] ? cur : "default");
        strcat(b, "   [t: next theme]");
        tui_print_at(row++, 2, b);
    }
    if (row >= 1) tui_print_at(row++, 2, "  registered themes:");
    for (int i = 0; i < n && row >= 1; i++) {
        strcpy(b, "    - ");
        strcat(b, th[i].name);
        strcat(b, " (");
        {
            char m[8];
            u64_to_str((u64)th[i].fg, m); strcat(b, m);
            strcat(b, " on ");
            u64_to_str((u64)th[i].bg, m); strcat(b, m);
        }
        strcat(b, ") ");
        strcat(b, th[i].desc);
        tui_print_at(row++, 2, b);
    }
    if (row >= 1) tui_print_at(row++, 2, "  backend/font   : fb 8x16 cells (fixed); serial backend via tui.backend");
    if (row >= 1) tui_print_at(row++, 2, "Keys: t cycle themes (applies live) | F2 persist theme");
    return row + scroll - 1;
}

static int page_tui_input(int key) {
    if (key == 't') {
        tui_themeext_t th[8];
        int n = tui_theme_list(th, 8);
        if (n <= 0) { tui_status_line("no themes registered"); return 1; }
        const char *cur = tui_theme_current();
        int pick = 0;
        for (int i = 0; i < n; i++)
            if (strcmp(th[i].name, cur) == 0) pick = (i + 1) % n;
        tui_theme_set(th[pick].name);
        char sb[80]; strcpy(sb, "theme = "); strcat(sb, th[pick].name);
        strcat(sb, " (F2 persists)");
        tui_status_line(sb);
        return 1;
    }
    if (key == TUI_KEY_F(2)) {
        const char *cur = tui_theme_current();
        if (cur[0]) sys_write_cfg("tui.theme", cur);
        tui_status_line("tui.theme persisted");
        return 1;
    }
    return 0;
}

/* ---- registration + entry --------------------------------------------------------- */

static int host_persist_action(void) {
    lib_config_write("log.level", lib_log_get_level() ? "1" : "0");
    tui_status_line("settings persisted to /etc/opencube.conf");
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
