/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-10-tui-unified
 * File: tui/tui_dt_apps.c
 * Purpose: desktop builtin pages: full-screen page helper, help (F1),
 *          language switch, update.  All pages render real data - no
 *          stubs.  (The command bridge and the dynamic disk subtree
 *          live in tui_dt_cmdbr.c.)
 */
#include "tui_desktop.h"
#include "../core/lib/lib_string.h"
#include "../core/lib/lib_config.h"
#include "../recovery/shell.h"
#include "../services/power/core_power.h"
#include "../services/ota/ota_update.h"
#include "../services/ota/ota_ab.h"
#include "../services/net/net_core.h"

/* ------------------------------------------------------------------ */
/* help page (F1)                                                      */
/* ------------------------------------------------------------------ */

typedef struct { const char *key; const char *en; const char *zh; } dt_kv_t;

int tui_dt_help_page_run(tui_dt_node_t *self) {
    (void)self;
    if (tui_dt_page_open() != 0) return -1;
    tui_print_at(0, 1, tui_tr("Open Cube OS Desktop - keys",
                              "Open Cube OS 桌面 - 按键"));
    static const dt_kv_t kvs[] = {
        { "Up/Down (Ctrl-P/N)", "move the selection",        "移动选择" },
        { "Enter",              "open the selected item",    "打开选中项" },
        { "Esc / Backspace",    "back one level; at root: exit", "返回上一层；根层退出" },
        { "F1",                 "this help",                 "帮助" },
        { "F2",                 "switch en/zh (persists)",   "切换 en/zh（持久化）" },
        { "F5",                 "repaint / refresh",         "刷新重绘" },
        { "F10",                "exit the desktop",          "退出桌面" },
        { NULL, NULL, NULL },
    };
    int row = 2;
    for (int i = 0; kvs[i].key; i++) {
        char line[128];
        int n = 0;
        for (const char *p = kvs[i].key; *p && n < 30; p++) line[n++] = *p;
        while (n < 22) line[n++] = ' ';
        line[n++] = '-'; line[n++] = ' ';
        const char *d = tui_tr(kvs[i].en, kvs[i].zh);
        for (const char *p = d; *p && n < 120; p++) line[n++] = *p;
        line[n] = 0;
        tui_print_at(row++, 1, line);
    }
    row++;
    tui_print_at(row++, 1, tui_tr("Registered top-level applications:",
                                  "已注册的顶层应用："));
    tui_dt_node_t *root = tui_dt_find("root");
    for (int i = 0; root && i < root->child_count &&
                    row < tui_dt_page_rows() - 2; i++) {
        tui_dt_node_t *c = root->children[i];
        if (!c) continue;
        char line[128];
        int n = 0;
        for (const char *p = c->en; *p && n < 24; p++) line[n++] = *p;
        while (n < 18) line[n++] = ' ';
        line[n++] = '-'; line[n++] = ' ';
        const char *d = tui_tr(c->desc_en, c->desc_zh);
        for (const char *p = d; *p && n < 120; p++) line[n++] = *p;
        line[n] = 0;
        tui_print_at(row++, 1, line);
    }
    tui_dt_page_wait_close(NULL);
    return 0;
}

/* ------------------------------------------------------------------ */
/* language switch page                                                */
/* ------------------------------------------------------------------ */

int tui_dt_lang_run(tui_dt_node_t *self) {
    (void)self;
    if (tui_dt_page_open() != 0) return -1;
    tui_print_at(0, 1, "Language / 语言");
    int row = 2;
    tui_print_at(row++, 1, tui_tr("1. English (en)", "1. 英语 (en)"));
    tui_print_at(row++, 1, tui_tr("2. Chinese (zh)", "2. 中文 (zh)"));
    tui_print_at(row++, 1, tui_tr(
        "Press 1 or 2; Esc keeps the current language.",
        "按 1 或 2；Esc 保持当前语言。"));
    tui_dt_page_flush();
    for (;;) {
        int k = tui_getch();
        if (k == '1') { tui_lang_set(SHELL_LANG_EN); break; }
        if (k == '2') { tui_lang_set(SHELL_LANG_ZH); break; }
        if (k == TUI_KEY_ESC || k == TUI_KEY_F10 || k == 'q') break;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* update page (real commands, live captured output)                   */
/* ------------------------------------------------------------------ */

/* print the first lines of a captured buffer, wrapping at the screen */
static void dt_print_captured(const char *out, int start_row, int max_rows) {
    int printed = 0;
    const char *p = out;
    while (*p && printed < max_rows) {
        const char *nl = strchr(p, '\n');
        int len = nl ? (int)(nl - p) : (int)strlen(p);
        if (len > tui_dt_page_cols() - 3) len = tui_dt_page_cols() - 3;
        char line[128];
        int m = len < 120 ? len : 120;
        memcpy(line, p, m);
        line[m] = 0;
        tui_print_at(start_row + printed, 1, line);
        printed++;
        if (!nl) break;
        p = nl + 1;
    }
}

int tui_dt_update_run(tui_dt_node_t *self) {
    (void)self;
    if (tui_dt_page_open() != 0) return -1;
    tui_print_at(0, 1, tui_tr("Update", "更新"));
    tui_print_at(2, 1, tui_tr("1. checkupdate (network check vs update.json)",
                              "1. checkupdate（联网检查 update.json）"));
    tui_print_at(3, 1, tui_tr("2. abupdate   (A/B slot status)",
                              "2. abupdate   （A/B 槽状态）"));
    tui_print_at(4, 1, tui_tr("3. install    (download+verify+install, then reboot prompt)",
                              "3. install    （下载+校验+安装，完成后提示重启）"));
    tui_print_at(5, 1, tui_tr("Esc cancels.", "Esc 取消。"));
    tui_dt_page_flush();
    for (;;) {
        int k = tui_getch();
        if (k == '1' || k == '2') {
            char out[8192];
            const char *cmd = (k == '1') ? "checkupdate" : "abupdate";
            tui_dt_loading_begin(cmd);
            int rc = shell_execute_captured(cmd, out, (int)sizeof(out));
            tui_dt_loading_end(rc == 0, tui_tr("update check done", "更新检查完成"),
                               tui_tr("update check failed", "更新检查失败"));
            tui_dt_page_open();          /* fresh window over the toast */
            tui_print_at(0, 1, cmd);
            dt_print_captured(out, 2, tui_dt_page_rows() - 5);
            tui_dt_toast(rc == 0, tui_tr("%s rc=%d", "%s rc=%d"), cmd, rc);
            tui_dt_page_wait_close(NULL);
            return rc;
        }
        if (k == '3') {
            /* ---- full native install flow (no shell bridge) ----
             * Phase-based progress with the desktop loading spinner:
             * the L0 OTA API is synchronous, so the progress shows the
             * real phase transitions (check -> download -> install)
             * rather than a fake percentage. */
            ota_update_pkg_info_t pkg;
            tui_dt_loading_begin(tui_tr("update: checking manifest",
                                        "更新：正在检查清单"));
            tui_dt_loading_tick();
            int rc = ota_update_check_pkg(&pkg);
            if (rc == OC_UPDATE_OK) {
                tui_dt_loading_end(1, tui_tr("already up to date", "已是最新版本"), NULL);
                tui_dt_toast(1, tui_tr("up to date: %s", "已是最新：%s"),
                             pkg.info.version);
                continue;
            }
            if (rc != OC_UPDATE_NEW) {
                tui_dt_loading_end(0, NULL,
                    tui_tr("check failed", "检查失败"));
                tui_dt_toast(0, tui_tr("check failed: %s", "检查失败：%s"),
                             ota_update_strerror(rc));
                continue;
            }
            if (!pkg.have_package || !pkg.package_url[0]) {
                tui_dt_loading_end(0, NULL, tui_tr("no package_url in manifest",
                                                   "清单无 package_url"));
                tui_dt_toast(0, tui_tr("no package_url in manifest",
                                       "清单无 package_url"));
                continue;
            }
            /* download */
            tui_dt_loading_begin(tui_tr("update: downloading package",
                                        "更新：正在下载安装包"));
            tui_dt_loading_tick();
            const char *pkg_path = "/tmp/ota_update.tar.gz";
            int drc = ota_update_download(pkg.package_url, pkg_path);
            if (drc != 0) {
                tui_dt_loading_end(0, NULL, tui_tr("download failed", "下载失败"));
                tui_dt_toast(0, tui_tr("download failed: %s", "下载失败：%s"),
                             ota_update_strerror(drc));
                continue;
            }
            /* verify */
            tui_dt_loading_begin(tui_tr("update: verifying SHA256",
                                        "更新：正在校验 SHA256"));
            tui_dt_loading_tick();
            int vrc = ota_update_verify(pkg_path, pkg.package_sha256);
            if (vrc != 0) {
                tui_dt_loading_end(0, NULL, tui_tr("verify failed", "校验失败"));
                tui_dt_toast(0, tui_tr("verify failed: %s", "校验失败：%s"),
                             ota_update_strerror(vrc));
                continue;
            }
            /* install to the inactive slot */
            tui_dt_loading_begin(tui_tr("update: installing to slot",
                                        "更新：正在安装到槽"));
            tui_dt_loading_tick();
            ota_update_status_t stt;
            ota_update_get_status(&stt);
            const char *tgt = (stt.current_boot[0] == 'A') ? "B" : "A";
            if (stt.current_boot[0] != 'A' && stt.current_boot[0] != 'B')
                tgt = "B";
            int irc = ota_update_install(pkg_path, tgt);
            if (irc != 0) {
                tui_dt_loading_end(0, NULL, tui_tr("install failed", "安装失败"));
                tui_dt_toast(0, tui_tr("install failed: %s", "安装失败：%s"),
                             ota_update_strerror(irc));
                continue;
            }
            if (ota_update_set_boot(tgt) != 0) {
                tui_dt_loading_end(0, NULL,
                    tui_tr("set-boot failed", "设置启动槽失败"));
                tui_dt_toast(0, tui_tr("set-boot failed", "设置启动槽失败"));
                continue;
            }
            tui_dt_loading_end(1, tui_tr("update installed", "更新已安装"), NULL);
            /* reboot prompt (the A/B switch only takes effect on reboot) */
            if (tui_dt_confirm(
                    "update installed to the inactive slot - reboot now? (y/n)",
                    "更新已安装到非活动槽 - 现在重启？（y/n）") == 1) {
                tui_shutdown();
                core_power_reboot();   /* never returns */
            }
            tui_dt_toast(1, tui_tr("update ready, reboots into slot %s",
                                   "更新就绪，重启后进入槽 %s"), tgt);
            return 0;
        }
        if (k == TUI_KEY_ESC || k == TUI_KEY_F10 || k == 'q') return 0;
    }
}

/* ------------------------------------------------------------------ */
/* WP-10-tui-native: native network configuration page                 */
/* ------------------------------------------------------------------ */

/* dotted-quad from the host-order u32 the net stack uses (net_core.c
 * format_ip parity: MSB first via shifts, endian-independent) */
static void nc_ip_str(u32 ip, char *out, int cap) {
    out[0] = 0;
    for (int i = 3; i >= 0; i--) {
        char num[8];
        u64_to_str((ip >> (i * 8)) & 0xFF, num);
        if (i < 3) strcat(out, ".");
        strcat(out, num);
    }
    out[cap - 1] = 0;
}

static int nc_prompt_ip(const char *title_en, const char *title_zh,
                        char *out, int cap) {
    int rows = tui_dt_page_rows(), cols = tui_dt_page_cols();
    tui_window_t *fwin = tui_window_new(rows / 2 - 2, (cols - 52) / 2, 4, 52);
    if (!fwin) return -1;
    tui_window_focus_set(fwin);
    tui_field_t f[1];
    memset(f, 0, sizeof(f));
    f[0].y = 2; f[0].x = 6; f[0].width = 40;
    f[0].editable = 1; f[0].label = "";
    tui_form_t *form = tui_form_new(fwin, f, 1);
    tui_set_attr(TUI_MAKE_ATTR(TUI_COLOR_WHITE, TUI_COLOR_TEAL) | TUI_ATTR_BOLD);
    tui_print_at(0, 1, tui_tr(title_en, title_zh));
    tui_set_attr(tui_default_attr());
    tui_form_post(form);
    tui_window_refresh(fwin);
    int rc = -1;
    for (;;) {
        int k = tui_getch();
        if (k == TUI_KEY_ESC) break;
        int d = tui_form_driver(form, k);
        if (d == 1) {
            const char *v = tui_form_field_buffer(form, 0);
            strncpy(out, v ? v : "", cap - 1);
            out[cap - 1] = 0;
            rc = 0;
            break;
        }
        if (d == -1) break;
        tui_window_refresh(fwin);
    }
    tui_form_unpost(form);
    tui_window_free(fwin);
    tui_window_t *pg = tui_dt_page_window();
    if (pg) tui_window_focus_set(pg);
    return rc;
}

int tui_dt_netconf_run(tui_dt_node_t *self) {
    (void)self;
    if (tui_dt_page_open() != 0) return -1;
    for (;;) {
        int rows = tui_dt_page_rows(), cols = tui_dt_page_cols();
        /* live view of the real net stack state */
        tui_attr_t hdr = TUI_MAKE_ATTR(TUI_COLOR_WHITE, TUI_COLOR_TEAL) | TUI_ATTR_BOLD;
        tui_set_attr(hdr);
        for (int x = 0; x < cols; x++) tui_print_at(0, x, " ");
        tui_print_at(0, 1, tui_tr("Network configuration", "网络配置"));
        tui_set_attr(tui_default_attr());
        for (int r = 1; r < rows - 2; r++) {
            tui_set_attr(tui_default_attr());
            for (int x = 0; x < cols; x++) tui_print_at(r, x, " ");
        }
        char line[96], vs[20];
        int row = 2;
        nc_ip_str(net_get_ip(), vs, (int)sizeof(vs));
        strncpy(line, tui_tr("  IP     : ", "  IP     ："), sizeof(line) - 1);
        tui_dt_str_append(line, (int)sizeof(line), vs[0] ? vs : tui_tr("(unset)", "（未设置）"));
        tui_print_at(row++, 1, line);
        nc_ip_str(net_get_mask(), vs, (int)sizeof(vs));
        strncpy(line, tui_tr("  Mask   : ", "  掩码   ："), sizeof(line) - 1);
        tui_dt_str_append(line, (int)sizeof(line), vs[0] ? vs : tui_tr("(unset)", "（未设置）"));
        tui_print_at(row++, 1, line);
        nc_ip_str(net_get_gateway(), vs, (int)sizeof(vs));
        strncpy(line, tui_tr("  Gateway: ", "  网关   ："), sizeof(line) - 1);
        tui_dt_str_append(line, (int)sizeof(line), vs[0] ? vs : tui_tr("(unset)", "（未设置）"));
        tui_print_at(row++, 1, line);
        nc_ip_str(net_get_dns(), vs, (int)sizeof(vs));
        strncpy(line, tui_tr("  DNS    : ", "  DNS    ："), sizeof(line) - 1);
        tui_dt_str_append(line, (int)sizeof(line), vs[0] ? vs : tui_tr("(unset)", "（未设置）"));
        tui_print_at(row++, 1, line);
        u8 mac[6];
        net_get_mac(mac);
        {
            char mb[24];
            for (int i = 0; i < 6; i++) {
                char h[4];
                h[0] = "0123456789ABCDEF"[(mac[i] >> 4) & 0xF];
                h[1] = "0123456789ABCDEF"[mac[i] & 0xF];
                h[2] = i < 5 ? ':' : 0;
                h[3] = 0;
                if (i == 0) strncpy(mb, h, sizeof(mb) - 1);
                else strcat(mb, h);
            }
            strncpy(line, tui_tr("  MAC    : ", "  MAC    ："), sizeof(line) - 1);
            tui_dt_str_append(line, (int)sizeof(line), mb);
            tui_print_at(row++, 1, line);
        }
        int link = net_get_link_status();
        strncpy(line, tui_tr("  Link   : ", "  链路   ："), sizeof(line) - 1);
        tui_dt_str_append(line, (int)sizeof(line),
                          link ? tui_tr("up", "已连接") : tui_tr("down", "未连接"));
        tui_print_at(row++, 1, line);
        row++;
        tui_set_attr(TUI_MAKE_ATTR(TUI_COLOR_AQUA, TUI_COLOR_BLACK));
        tui_print_at(row++, 1, tui_tr("  d: DHCP lease    i: static IP    n: DNS    r: refresh",
                                       "  d: DHCP 获取     i: 静态 IP      n: DNS    r: 刷新"));
        /* toast row */
        tui_set_attr(tui_default_attr());
        for (int x = 0; x < cols; x++) tui_print_at(rows - 2, x, " ");
        int tok = 1;
        const char *txt = tui_dt_toast_text(&tok);
        if (txt[0]) {
            tui_set_attr(tok
                ? (tui_attr_t)(TUI_MAKE_ATTR(TUI_COLOR_LIME, TUI_COLOR_BLACK) | TUI_ATTR_BOLD)
                : (tui_attr_t)(TUI_MAKE_ATTR(TUI_COLOR_WHITE, TUI_COLOR_MAROON) | TUI_ATTR_REVERSE));
            tui_print_at(rows - 2, 1, txt);
        }
        tui_attr_t bar = TUI_MAKE_ATTR(TUI_COLOR_BLACK, TUI_COLOR_AQUA);
        tui_set_attr(bar);
        for (int x = 0; x < cols; x++) tui_print_at(rows - 1, x, " ");
        tui_print_at(rows - 1, 0, tui_tr(
            " d:DHCP i:StaticIP n:DNS r:Refresh Esc:Back ",
            " d:DHCP i:静态IP n:DNS r:刷新 Esc:返回 "));
        tui_set_attr(tui_default_attr());
        tui_dt_page_flush();

        int k = tui_getch();
        if (k == TUI_KEY_ESC || k == TUI_KEY_F10 || k == 'q') break;
        else if (k == 'r') {
            tui_dt_toast(1, tui_tr("refreshed", "已刷新"));
        }
        else if (k == 'd') {
            /* real DHCP discover through the kernel DHCP client */
            tui_dt_loading_begin(tui_tr("dhcp: discovering", "DHCP：正在发现"));
            tui_dt_loading_tick();
            int rc = net_dhcp_discover();
            tui_dt_loading_end(rc == 0,
                tui_tr("dhcp lease acquired", "已获取 DHCP 租约"),
                tui_tr("dhcp failed", "DHCP 失败"));
            tui_dt_toast(rc == 0, tui_tr("dhcp rc=%d", "DHCP rc=%d"), rc);
        }
        else if (k == 'i') {
            char ips[20], masks[20], gws[20];
            if (nc_prompt_ip("IP address", "IP 地址", ips, (int)sizeof(ips)) != 0)
                continue;
            if (nc_prompt_ip("netmask", "子网掩码", masks, (int)sizeof(masks)) != 0)
                continue;
            if (nc_prompt_ip("gateway", "网关", gws, (int)sizeof(gws)) != 0)
                continue;
            u32 ip = net_parse_ip(ips);
            u32 mask = net_parse_ip(masks);
            u32 gw = net_parse_ip(gws);
            if (ip == 0) {
                tui_dt_toast(0, tui_tr("bad IP: %s", "IP 无效：%s"), ips);
                continue;
            }
            net_set_ip(ip, mask, gw);
            tui_dt_toast(1, tui_tr("static IP set", "静态 IP 已设置"));
        }
        else if (k == 'n') {
            char dnss[20];
            if (nc_prompt_ip("DNS server", "DNS 服务器", dnss, (int)sizeof(dnss)) != 0)
                continue;
            u32 dns = net_parse_ip(dnss);
            if (dns == 0) {
                tui_dt_toast(0, tui_tr("bad DNS: %s", "DNS 无效：%s"), dnss);
                continue;
            }
            net_set_dns(dns);
            tui_dt_toast(1, tui_tr("DNS set", "DNS 已设置"));
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* WP-11-crash_recovery: crash-test page (TUI guard verification)     */
/* ------------------------------------------------------------------ */

/* Opens as a normal page, then deliberately raises a ring-0 page
 * fault INSIDE TUI code (this function is linked into the
 * __tui_text window - see linker.ld). The exception dispatcher sees
 * rip within the TUI window + an active tui_dt_guard() and longjmps
 * back to the desktop instead of a full kernel panic: the menu
 * survives, the crash is recorded in the crashlog, and the guard
 * toast reports the page as contained. This is the acceptance test
 * for "a TUI sub-page crash must return to the menu, not hang". */
int tui_dt_crashtest_run(tui_dt_node_t *self) {
    (void)self;
    if (tui_dt_page_open() != 0) return -1;
    tui_print_at(1, 1, tui_tr("WP-11 crash isolation test",
                              "WP-11 崩溃隔离测试"));
    tui_print_at(2, 1, tui_tr("Press C to raise a ring-0 fault in this",
                              "按 C 在本页内触发 ring-0 异常"));
    tui_print_at(3, 1, tui_tr("page (guard must catch it). Esc quits.",
                              "（护栏应拦截）。Esc 退出。"));
    tui_dt_page_flush();
    for (;;) {
        int k = tui_getch();
        if (k == 'c' || k == 'C') {
            tui_print_at(5, 1, tui_tr("faulting now...",
                                      "正在触发异常..."));
            tui_dt_page_flush();
            /* ring-0 NULL write: #PF with rip inside the TUI window */
            *(volatile u64 *)0 = 0x0BADF00D0BADF00DULL;
            /* NOT reached when the guard works */
            tui_print_at(6, 1, tui_tr("!!! fault NOT contained (BUG)",
                                      "!!! 异常未被隔离（BUG）"));
            tui_dt_page_flush();
        }
        if (k == TUI_KEY_ESC || k == 'q') break;
    }
    tui_dt_page_close();
    return 0;
}
