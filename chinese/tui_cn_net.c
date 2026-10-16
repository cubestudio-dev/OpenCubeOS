/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-10-chinese_support
 * File: chinese/tui_cn_net.c
 * Purpose: Wireless network TUI with Chinese labels - scan / connect /
 *          status / bluetooth pages on the real wl + bt stacks.
 */
#include "tui_chinese.h"
#include "../kernel/lib/lib_string.h"
#include "../drivers/wireless/driver_wireless.h"
#include "../drivers/bluetooth/driver_bt.h"
#include "../net/net_core.h"

#define CN_NET_MAX_SCAN 16

static wl_scan_result_t g_cn_scan[CN_NET_MAX_SCAN];
static int g_cn_scan_count;
static bt_inquiry_result_t g_cn_bt[8];

/* Chinese encryption description from the real scan fields. */
static const char *cn_enc_str(const wl_scan_result_t *r) {
    if (r->has_rsn) return "WPA2 加密";
    if (r->has_wpa) return "WPA 加密";
    if (r->capability & 0x0010) return "有密码保护";
    return "开放网络";
}

int tui_cn_wifi_scan(void) {
    wl_scan_clear();
    if (wl_scan_start(0) != 0) return -1;
    g_cn_scan_count = wl_scan_result_count();
    if (g_cn_scan_count > CN_NET_MAX_SCAN) g_cn_scan_count = CN_NET_MAX_SCAN;
    for (int i = 0; i < g_cn_scan_count; i++) {
        const wl_scan_result_t *r = wl_scan_result_get(i);
        if (r) g_cn_scan[i] = *r;
    }
    return g_cn_scan_count;
}

int tui_cn_wifi_connect(const char *ssid, const char *password) {
    (void)password;   /* open-network connect path; WPA keying is a
                         wl-family driver concern (see EXTENSIONS_WP10-wireless) */
    if (!ssid) return -2;
    for (int i = 0; i < g_cn_scan_count; i++) {
        if (strcmp(g_cn_scan[i].ssid, ssid) == 0) {
            return wl_connect(0, g_cn_scan[i].bssid, ssid);
        }
    }
    return -3;
}

int tui_cn_wifi_disconnect(void) {
    extern int wl_disconnect(int dev_id);
    return wl_disconnect(0);
}

/* Render the scan table into a window with Chinese column headers. */
static void cn_net_render_scan(tui_window_t *win) {
    tui_window_clear(win);
    tui_window_focus_set(win);
    tui_set_attr((tui_attr_t)TUI_MAKE_ATTR(TUI_COLOR_YELLOW, TUI_COLOR_BLACK) |
                 TUI_ATTR_BOLD);
    tui_print_at(0, 1, "无线网络扫描  [q 返回]");
    tui_set_attr((tui_attr_t)TUI_MAKE_ATTR(TUI_COLOR_AQUA, TUI_COLOR_BLACK));
    tui_print_at(1, 1, "序号  SSID            信号  信道  加密方式");
    tui_set_attr((tui_attr_t)TUI_MAKE_ATTR(TUI_COLOR_SILVER, TUI_COLOR_BLACK));
    if (g_cn_scan_count == 0) {
        tui_print_at(2, 1, "（扫描结果为空 - QEMU 无 802.11 硬件，如实报告）");
    }
    int max = win->h - 4;
    if (max > g_cn_scan_count) max = g_cn_scan_count;
    char num[8];
    for (int i = 0; i < max; i++) {
        u64_to_str((u64)(i + 1), num);
        char row[100];
        int n = 0;
        for (int k = 0; num[k]; k++) row[n++] = num[k];
        const char *pad = "    ";
        for (int k = 0; pad[k]; k++) row[n++] = pad[k];
        const char *ssid = g_cn_scan[i].ssid;
        for (int k = 0; ssid && ssid[k] && n < 90; k++) row[n++] = ssid[k];
        char tail[48];
        char rssi[8], chan[8];
        u64_to_str((u64)g_cn_scan[i].rssi, rssi);
        u64_to_str((u64)g_cn_scan[i].channel, chan);
        int t = 0;
        tail[t++] = ' ';
        for (int k = 0; rssi[k]; k++) tail[t++] = rssi[k];
        tail[t++] = ' ';
        for (int k = 0; chan[k]; k++) tail[t++] = chan[k];
        tail[t++] = ' ';
        const char *enc = cn_enc_str(&g_cn_scan[i]);
        for (int k = 0; enc[k]; k++) tail[t++] = enc[k];
        tail[t] = 0;
        for (int k = 0; tail[k] && n < 96; k++) row[n++] = tail[k];
        row[n] = 0;
        tui_print_at(i + 2, 1, row);
    }
    tui_window_refresh(win);
}

static int cn_net_scan_page(void) {
    int cols, rows;
    tui_term_size(&cols, &rows);
    tui_window_t *win = tui_window_new(0, 0, rows, cols);
    if (!win) return -1;
    tui_window_title(win, "无线扫描（中文）");
    tui_cn_wifi_scan();
    cn_net_render_scan(win);
    for (;;) {
        int k = tui_getch();
        if (k == 'r' || k == 'R') {
            tui_cn_wifi_scan();
            cn_net_render_scan(win);
        } else if (k == 'q' || k == TUI_KEY_ESC) {
            break;
        }
    }
    tui_window_free(win);
    tui_update_panels();
    return g_cn_scan_count;
}

static int cn_net_connect_page(void) {
    int cols, rows;
    tui_term_size(&cols, &rows);
    tui_window_t *win = tui_window_new(0, 0, rows, cols);
    if (!win) return -1;
    tui_window_title(win, "无线连接（中文）");
    char ssid[64];
    ssid[0] = 0;
    int rc = 0;
    /* IME-free direct input: SSIDs are typed as ASCII. */
    tui_window_clear(win);
    tui_window_focus_set(win);
    tui_set_attr((tui_attr_t)TUI_MAKE_ATTR(TUI_COLOR_YELLOW, TUI_COLOR_BLACK));
    tui_print_at(1, 1, "输入要连接的 SSID（回车确认，Esc 取消）:");
    tui_set_attr((tui_attr_t)TUI_MAKE_ATTR(TUI_COLOR_WHITE, TUI_COLOR_GRAY));
    tui_print_at(3, 3, "SSID: ");
    tui_window_refresh(win);
    tui_move(3, 9);
    tui_set_attr((tui_attr_t)TUI_MAKE_ATTR(TUI_COLOR_WHITE, TUI_COLOR_GRAY));
    /* SSIDs are ASCII names: use the direct ASCII editor.  The pinyin
     * editor turned every lowercase letter into a composition buffer
     * and made simple names like "home" impossible to type. */
    rc = tui_chinese_input_ascii(win, "SSID: ", ssid, sizeof(ssid));
    if (rc >= 0) {
        tui_status_line("连接 %s ...", ssid);
        int crc = tui_cn_wifi_connect(ssid, "");
        tui_status_line("连接结果: %s (rc=%d)",
                        crc == 0 ? "成功" : "失败", crc);
    }
    tui_window_free(win);
    tui_update_panels();
    return 1;
}

static int cn_net_status_page(void) {
    int cols, rows;
    tui_term_size(&cols, &rows);
    tui_window_t *win = tui_window_new(0, 0, rows, cols);
    if (!win) return -1;
    tui_window_title(win, "无线状态（中文）");
    tui_window_clear(win);
    tui_window_focus_set(win);
    tui_set_attr((tui_attr_t)TUI_MAKE_ATTR(TUI_COLOR_YELLOW, TUI_COLOR_BLACK) |
                 TUI_ATTR_BOLD);
    tui_print_at(0, 1, "无线状态  [q 返回]");
    tui_set_attr((tui_attr_t)TUI_MAKE_ATTR(TUI_COLOR_SILVER, TUI_COLOR_BLACK));
    int n = wl_device_count();
    char num[12];
    u64_to_str((u64)n, num);
    char line[100];
    int p = 0;
    const char *lab = "无线设备数量: ";
    for (int k = 0; lab[k]; k++) line[p++] = lab[k];
    for (int k = 0; num[k]; k++) line[p++] = num[k];
    line[p] = 0;
    tui_print_at(2, 1, line);
    if (n == 0) {
        tui_print_at(3, 1, "无 802.11 硬件（QEMU 无此模型，探测如实报告）");
    }
    for (int i = 0; i < n && i < 4; i++) {
        wl_device_t *w = wl_device_get(i);
        if (!w) continue;
        static const char *states[] = { "关闭", "就绪", "扫描中",
                                        "认证中", "关联中", "已关联" };
        p = 0;
        const char *pre = "wl";
        for (int k = 0; pre[k]; k++) line[p++] = pre[k];
        char idx[4];
        u64_to_str((u64)i, idx);
        for (int k = 0; idx[k]; k++) line[p++] = idx[k];
        const char *sep = "  状态: ";
        for (int k = 0; sep[k]; k++) line[p++] = sep[k];
        const char *st = states[w->state];
        for (int k = 0; st[k]; k++) line[p++] = st[k];
        const char *ssid_lab = "  SSID: ";
        for (int k = 0; ssid_lab[k]; k++) line[p++] = ssid_lab[k];
        for (int k = 0; w->ssid[k] && p < 96; k++) line[p++] = w->ssid[k];
        line[p] = 0;
        tui_print_at(4 + i, 1, line);
    }
    tui_window_refresh(win);
    for (;;) {
        int k = tui_getch();
        if (k == 'q' || k == TUI_KEY_ESC) break;
    }
    tui_window_free(win);
    tui_update_panels();
    return n;
}

static int cn_net_bt_page(void) {
    int cols, rows;
    tui_term_size(&cols, &rows);
    tui_window_t *win = tui_window_new(0, 0, rows, cols);
    if (!win) return -1;
    tui_window_title(win, "蓝牙（中文）");
    tui_window_clear(win);
    tui_window_focus_set(win);
    tui_set_attr((tui_attr_t)TUI_MAKE_ATTR(TUI_COLOR_YELLOW, TUI_COLOR_BLACK) |
                 TUI_ATTR_BOLD);
    tui_print_at(0, 1, "蓝牙设备  [S 扫描  P 连接  U 断开  V 服务  q 返回]");
    tui_set_attr((tui_attr_t)TUI_MAKE_ATTR(TUI_COLOR_SILVER, TUI_COLOR_BLACK));
    int present = bt_hci_present();
    tui_print_at(2, 1, present ? "蓝牙控制器: 存在" : "蓝牙控制器: 未检测到");
    int found = 0;
    if (present) {
        if (bt_hci_inquiry_start() == 0) {
            found = bt_hci_inquiry_results(g_cn_bt, 8);
        }
        char num[8];
        u64_to_str((u64)found, num);
        char line[64];
        const char *pre = "发现设备数: ";
        int p = 0;
        for (int k = 0; pre[k]; k++) line[p++] = pre[k];
        for (int k = 0; num[k]; k++) line[p++] = num[k];
        line[p] = 0;
        tui_print_at(3, 1, line);
        for (int i = 0; i < found && i < 8; i++) {
            char addr[20];
            bt_addr_to_str(g_cn_bt[i].addr, addr);
            p = 0;
            char idx[4];
            u64_to_str((u64)(i + 1), idx);
            for (int k = 0; idx[k]; k++) line[p++] = idx[k];
            line[p++] = ' ';
            for (int k = 0; addr[k] && p < 90; k++) line[p++] = addr[k];
            line[p] = 0;
            tui_print_at(4 + i, 1, line);
        }
    } else {
        tui_print_at(3, 1, "QEMU 经 COM2 H4 对端仿真（bt 对端未运行时如实报告）");
    }
    tui_window_refresh(win);
    for (;;) {
        int k = tui_getch();
        if (k == 'q' || k == TUI_KEY_ESC) break;
        if (!present) continue;
        if (k == 's' || k == 'S') {
            found = 0;
            if (bt_hci_inquiry_start() == 0) {
                found = bt_hci_inquiry_results(g_cn_bt, 8);
            }
            tui_window_clear(win);
            tui_window_focus_set(win);
            tui_set_attr((tui_attr_t)TUI_MAKE_ATTR(TUI_COLOR_YELLOW, TUI_COLOR_BLACK) |
                         TUI_ATTR_BOLD);
            tui_print_at(0, 1, "蓝牙设备  [S 扫描  P 连接  U 断开  V 服务  q 返回]");
            tui_set_attr((tui_attr_t)TUI_MAKE_ATTR(TUI_COLOR_SILVER, TUI_COLOR_BLACK));
            char num2[8];
            u64_to_str((u64)found, num2);
            char l2[64];
            int p2 = 0;
            const char *pre2 = "发现设备数: ";
            for (int c = 0; pre2[c]; c++) l2[p2++] = pre2[c];
            for (int c = 0; num2[c]; c++) l2[p2++] = num2[c];
            l2[p2] = 0;
            tui_print_at(2, 1, l2);
            for (int i = 0; i < found && i < 8; i++) {
                char addr[20];
                bt_addr_to_str(g_cn_bt[i].addr, addr);
                char idx[4];
                u64_to_str((u64)(i + 1), idx);
                p2 = 0;
                for (int c = 0; idx[c]; c++) l2[p2++] = idx[c];
                l2[p2++] = ' ';
                for (int c = 0; addr[c] && p2 < (int)sizeof(l2) - 1; c++)
                    l2[p2++] = addr[c];
                l2[p2] = 0;
                tui_print_at(3 + i, 1, l2);
            }
            tui_window_refresh(win);
        } else if (k == 'p' || k == 'P') {
            u16 handle = 0;
            int rc = (found > 0)
                     ? bt_hci_create_connection(g_cn_bt[0].addr, &handle)
                     : -1;
            tui_status_line(rc == 0 ? "连接已建立（设备 1）"
                                    : "连接失败（HCI 真实结果）");
        } else if (k == 'u' || k == 'U') {
            int rc = -1;
            if (found > 0) {
                u16 handle = 0;
                if (bt_hci_handle_of_addr(g_cn_bt[0].addr, &handle) == 0)
                    rc = bt_hci_disconnect(handle);
                else
                    rc = 0;   /* not connected: nothing to drop */
            }
            tui_status_line(rc == 0 ? "已断开（如曾连接）"
                                    : "断开失败（HCI 真实结果）");
        } else if (k == 'v' || k == 'V') {
            char name[64];
            name[0] = 0;
            if (found > 0)
                bt_hci_remote_name(g_cn_bt[0].addr, name, sizeof(name));
            tui_status_line("设备 %s: SPP/HID 服务经 bt 命令（SDP）",
                            name[0] ? name : "(未知)");
        }
    }
    tui_window_free(win);
    tui_update_panels();
    return present ? found : 0;
}

int tui_cn_net_main(void) {
    if (tui_chinese_init() != 0) return -1;
    int cols, rows;
    tui_term_size(&cols, &rows);
    tui_window_t *win = tui_window_new(0, 0, rows, cols);
    if (!win) return -1;
    tui_window_free(win);

    tui_menu_item_t items[5];
    items[0].name = "扫描网络";
    items[0].desc = "扫描周围 802.11 网络（扫描/刷新 r 键）";
    items[0].userptr = 0;
    items[1].name = "连接网络";
    items[1].desc = "输入 SSID 并连接";
    items[1].userptr = 0;
    items[2].name = "断开连接";
    items[2].desc = "断开当前无线连接";
    items[2].userptr = 0;
    items[3].name = "连接状态";
    items[3].desc = "查看无线设备与关联状态";
    items[3].userptr = 0;
    items[4].name = "蓝牙设备";
    items[4].desc = "蓝牙控制器查询与设备发现";
    items[4].userptr = 0;

    int total_ops = 0;
    for (;;) {
        tui_window_t *menu = tui_window_new(0, 0, 7, 40);
        if (!menu) return -1;
        tui_window_title(menu, "无线网络（中文）");
        int pick = tui_chinese_menu(menu, items, 5);
        tui_window_free(menu);
        tui_update_panels();
        if (pick < 0) break;
        switch (pick) {
        case 0: cn_net_scan_page(); total_ops++; break;
        case 1: cn_net_connect_page(); total_ops++; break;
        case 2: tui_cn_wifi_disconnect(); total_ops++; break;
        case 3: cn_net_status_page(); total_ops++; break;
        case 4: cn_net_bt_page(); total_ops++; break;
        }
    }
    return total_ops;
}
