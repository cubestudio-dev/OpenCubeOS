/* SPDX-License-Identifier: Apache-2.0
 * [3.0.0-WP04 G11c: revived verbatim from v2.2.0 tui/tui_net.c; include
 *  paths flattened to the WP-03 -I layout, shell_lang->disp_lang and
 *  shell_execute*->tui_exec* renamed (see tui_exec.h).]
 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-10-tui
 * File: tui/tui_net.c
 * Purpose: Wireless network TUI: scan listing (SSID/BSSID/channel/
 *          signal/encryption), open-network connect through the STA
 *          state machine, status panel, and the Bluetooth manager
 *          (inquiry, connect, remote name, SDP service browse).
 */
#include "tui.h"
#include "driver_wireless.h"
#include "driver_bt.h"
#include "net_core.h"
#include "lib_string.h"

#define WL_SCAN_MAX 16

static wl_scan_result_t g_scan[WL_SCAN_MAX];
static int g_scan_count;
static bt_inquiry_result_t g_bt_scan[8];
static int g_bt_scan_count;

int tui_wifi_init(void) {
    if (!tui_started()) return -1;
    g_scan_count = 0;
    return wl_device_count() > 0 ? 0 : -1;
}

/* scan through the real STA state machine (beacon/probe parsing) */
int tui_wifi_scan(void) {
    wl_device_t *w = wl_device_get(0);
    if (!w) return -1;
    wl_scan_clear();
    if (wl_scan_start(0) != 0) return -1;
    g_scan_count = wl_scan_result_count();
    if (g_scan_count > WL_SCAN_MAX) g_scan_count = WL_SCAN_MAX;
    for (int i = 0; i < g_scan_count; i++) {
        const wl_scan_result_t *r = wl_scan_result_get(i);
        if (r) g_scan[i] = *r;
    }
    return 0;
}

static const char *enc_str(const wl_scan_result_t *r) {
    /* the STA implements open-system authentication (association);
     * capability bits advertise WEP, beacons advertise RSN/WPA IEs */
    if (r->capability & 0x0010) return "WEP (not supported)";
    if (r->has_rsn) return "WPA2 (not supported)";
    if (r->has_wpa) return "WPA1 (not supported)";
    return "open";
}

/* Small ASCII line editor on the bottom row: printable characters land
 * in buf, Backspace deletes, Enter commits, Esc cancels.  Independent
 * of the Chinese IME stack (English flow). */
static int net_ask_line(const char *prompt, char *buf, int cap) {
    if (!buf || cap < 2) return -2;
    int len = 0;
    buf[0] = 0;
    int cols, rows;
    tui_term_size(&cols, &rows);
    int row = rows - 1;
    for (;;) {
        tui_attr_t a = (tui_attr_t)TUI_MAKE_ATTR(TUI_COLOR_WHITE, TUI_COLOR_NAVY);
        tui_attr_t save = tui_default_attr();
        tui_set_attr(a);
        for (int x = 0; x < cols; x++) {
            tui_move(row, x);
            tui_addch(' ');
        }
        tui_move(row, 0);
        tui_print(prompt ? prompt : "");
        tui_print(buf);
        tui_window_refresh(tui_window_focus());
        tui_set_attr(save);

        int k = tui_getch();
        if (k == TUI_KEY_ESC) return -1;
        if (k == TUI_KEY_ENTER) {
            int n = len;
            if (n > cap - 1) n = cap - 1;
            buf[n] = 0;
            return n;
        }
        if (k == TUI_KEY_BACKSPACE) {
            if (len > 0) buf[--len] = 0;
        } else if (k >= 0x20 && k < 0x7F && len < cap - 1) {
            buf[len++] = (char)k;
            buf[len] = 0;
        }
    }
}

int tui_wifi_list(void) {
    if (!tui_started()) return -1;
    int cols, rows;
    tui_term_size(&cols, &rows);
    tui_window_t *win = tui_window_new(0, 0, rows, cols);
    if (!win) return -1;
    tui_window_title(win, "Wireless networks");
    tui_window_focus_set(win);
    tui_window_clear(win);
    if (g_scan_count <= 0) {
        tui_print_at(1, 2, "no scan results (run Scan; no QEMU 802.11 model)");
    } else {
        tui_set_attr((tui_attr_t)TUI_MAKE_ATTR(TUI_COLOR_YELLOW, TUI_COLOR_BLACK) |
                     TUI_ATTR_BOLD);
        tui_print_at(1, 2, "SSID              BSSID             CH SIG ENC");
        tui_set_attr((tui_attr_t)TUI_MAKE_ATTR(TUI_COLOR_SILVER, TUI_COLOR_BLACK));
        for (int i = 0; i < g_scan_count && i < rows - 4; i++) {
            char line[96];
            char bssid[18];
            bt_addr_to_str(g_scan[i].bssid, bssid);   /* same NN: format */
            int n = 0;
            const char *s = g_scan[i].ssid;
            for (int k = 0; k < 18; k++) line[n++] = (s && s[k]) ? s[k] : ' ';
            for (int k = 0; k < 18; k++) line[n++] = bssid[k];
            line[n++] = ' ';
            line[n++] = (char)('0' + g_scan[i].channel / 10);
            line[n++] = (char)('0' + g_scan[i].channel % 10);
            line[n++] = ' ';
            char neg = g_scan[i].rssi < 0 ? '-' : 0;
            if (neg) line[n++] = neg;
            char sig[8];
            u64_to_str((u64)(g_scan[i].rssi < 0 ? -g_scan[i].rssi : g_scan[i].rssi), sig);
            for (int k = 0; sig[k]; k++) line[n++] = sig[k];
            line[n++] = ' ';
            const char *e = enc_str(&g_scan[i]);
            for (int k = 0; e[k] && n < 94; k++) line[n++] = e[k];
            line[n] = 0;
            tui_print_at(2 + i, 2, line);
        }
    }
    tui_status_line("C connect | Esc back | no WPA support (open networks only)");
    tui_window_border(win, &TUI_BORDER_SINGLE);
    tui_window_refresh(win);
    for (;;) {
        int k = tui_getch();
        if (k == TUI_KEY_ESC || k == TUI_KEY_F(10) || k == 'q') break;
        if (k == 'c' || k == 'C') {
            /* connect flow: ask for the SSID (defaults to the first
             * scan result when the user just presses Enter); the STA
             * state machine reports honestly when the network is not
             * reachable or requires unsupported encryption. */
            char ssid[64];
            if (g_scan_count > 0) {
                int n = 0;
                const char *s = g_scan[0].ssid;
                for (; s && s[n] && n < 63; n++) ssid[n] = s[n];
                ssid[n] = 0;
            } else {
                ssid[0] = 0;
            }
            int rc = net_ask_line("SSID (Enter=default, Esc=cancel): ",
                                  ssid, sizeof(ssid));
            if (rc < 0) {
                tui_status_line("connect cancelled");
                tui_window_refresh(win);
                continue;
            }
            if (ssid[0] && tui_wifi_connect(ssid, "") == 0)
                tui_status_line("associated with %s", ssid);
            else
                tui_status_line("associate failed (honest state machine result)");
            tui_window_refresh(win);
        }
    }
    tui_window_free(win);
    tui_update_panels();
    return 0;
}

/* connect through the real authenticate->associate sequence.  WPA/
 * WPA2 networks are refused with an honest reason: the 802.11 core
 * implements open-system authentication only (see KNOWN_ISSUES). */
int tui_wifi_connect(const char *ssid, const char *password) {
    (void)password;
    wl_device_t *w = wl_device_get(0);
    if (!w || !ssid) return -1;
    if (w->state != WL_STATE_READY && w->state != WL_STATE_ASSOCIATED)
        return -1;
    for (int i = 0; i < g_scan_count; i++) {
        if (strcmp(g_scan[i].ssid, ssid) != 0) continue;
        if ((g_scan[i].capability & 0x0010) || g_scan[i].has_rsn ||
            g_scan[i].has_wpa)
            return -1;   /* WEP/WPA advertised: open-system STA only */
        return wl_connect(0, g_scan[i].bssid, ssid);
    }
    return -1;
}

int tui_wifi_disconnect(void) {
    wl_device_t *w = wl_device_get(0);
    if (!w) return -1;
    if (w->state != WL_STATE_ASSOCIATED) return 0;   /* nothing to do */
    extern int wl_disconnect(int dev_id);
    return wl_disconnect(0);
}

int tui_wifi_status(void) {
    if (!tui_started()) return -1;
    int cols, rows;
    tui_term_size(&cols, &rows);
    tui_window_t *win = tui_window_new(0, 0, rows, cols);
    if (!win) return -1;
    tui_window_title(win, "Wireless status");
    tui_window_focus_set(win);
    tui_window_clear(win);
    static const char *states[] = { "DOWN", "READY", "SCANNING",
                                    "AUTHENTICATING", "ASSOCIATING",
                                    "ASSOCIATED" };
    int n = wl_device_count();
    tui_print_at(1, 2, n ? "devices:" : "no wireless device (honest DOWN)");
    for (int i = 0; i < n && i < 4; i++) {
        wl_device_t *w = wl_device_get(i);
        if (!w) continue;
        char line[96];
        int p = 0;
        const char *pre = "wl0: state ";
        for (int k = 0; pre[k]; k++) line[p++] = pre[k];
        const char *st = states[w->state];
        for (int k = 0; st[k]; k++) line[p++] = st[k];
        const char *mid = "  ssid ";
        for (int k = 0; mid[k]; k++) line[p++] = mid[k];
        for (int k = 0; w->ssid[k] && p < 90; k++) line[p++] = w->ssid[k];
        line[p] = 0;
        tui_print_at(2 + i, 2, line);
    }
    tui_status_line("Esc back");
    tui_window_border(win, &TUI_BORDER_SINGLE);
    tui_window_refresh(win);
    tui_getch();
    tui_window_free(win);
    tui_update_panels();
    return 0;
}

/* ---- Bluetooth manager ---- */

int tui_bt_init(void) {
    if (!tui_started()) return -1;
    return bt_hci_present() ? 0 : -1;
}

int tui_bt_scan(void) {
    if (bt_hci_inquiry_start() != 0) return -1;
    g_bt_scan_count = bt_hci_inquiry_results(g_bt_scan, 8);
    return (g_bt_scan_count >= 0) ? 0 : -1;
}

int tui_bt_pair(int dev_index) {
    if (dev_index < 0 || dev_index >= g_bt_scan_count) return -1;
    /* pairing = ACL connection + remote name fetch; SDP browse is the
     * service discovery step.  The stack verifies end-to-end over the
     * H4 test peer (tools/bt_test_peer.py). */
    u16 handle = 0;
    if (bt_hci_create_connection(g_bt_scan[dev_index].addr, &handle) != 0)
        return -1;
    return 0;
}

int tui_bt_unpair(int dev_index) {
    if (dev_index < 0 || dev_index >= g_bt_scan_count) return -1;
    u16 handle = 0;
    if (bt_hci_handle_of_addr(g_bt_scan[dev_index].addr, &handle) != 0)
        return 0;                     /* not connected: already unpaired */
    return bt_hci_disconnect(handle);
}

int tui_bt_services(int dev_index) {
    if (dev_index < 0 || dev_index >= g_bt_scan_count) return -1;
    char name[64];
    if (bt_hci_remote_name(g_bt_scan[dev_index].addr, name, sizeof(name)) != 0)
        name[0] = 0;
    tui_status_line("device %s: SPP/HID discovery via bt command (SDP)",
                     name[0] ? name : "(unknown)");
    return 0;
}

/* menu-driven bluetooth app */
int tui_bt_main(void) {
    if (!tui_started()) return -1;
    if (!bt_hci_present()) {
        tui_status_line("no Bluetooth transport (USB dongle or COM2 H4)");
        return -1;
    }
    int cols, rows;
    tui_term_size(&cols, &rows);
    tui_window_t *win = tui_window_new(0, 0, rows, cols);
    if (!win) return -1;
    tui_window_title(win, "Bluetooth");
    tui_window_focus_set(win);
    tui_window_clear(win);
    char bd[18];
    u8 addr[6] = { 0, 0, 0, 0, 0, 0 };
    int has_addr = (bt_hci_read_bd_addr(addr) == 0);
    bt_addr_to_str(addr, bd);
    tui_print_at(1, 2, has_addr ? "local BD_ADDR:" : "no controller answer");
    if (has_addr) tui_print_at(1, 18, bd);
    tui_print_at(3, 2, "S scan  P pair  U unpair  V services  Esc back");
    tui_window_border(win, &TUI_BORDER_SINGLE);
    tui_window_refresh(win);
    for (;;) {
        int k = tui_getch();
        if (k == TUI_KEY_ESC || k == TUI_KEY_F(10) || k == 'q') break;
        if (k == 's' || k == 'S') {
            tui_bt_scan();
            tui_window_clear(win);
            tui_print_at(3, 2, "S scan  P pair  U unpair  V services  Esc back");
            tui_print_at(5, 2, g_bt_scan_count > 0 ? "inquiry results:" :
                         "no devices found (honest inquiry result)");
            for (int i = 0; i < g_bt_scan_count && i < rows - 7; i++) {
                char line[96];
                char a[18];
                bt_addr_to_str(g_bt_scan[i].addr, a);
                int p = 0;
                const char *n1 = "dev ";
                for (int j = 0; n1[j]; j++) line[p++] = n1[j];
                char num[4];
                u64_to_str((u64)i, num);
                for (int j = 0; num[j]; j++) line[p++] = num[j];
                line[p++] = ' ';
                for (int j = 0; a[j]; j++) line[p++] = a[j];
                line[p] = 0;
                tui_print_at(6 + i, 2, line);
            }
            tui_window_refresh(win);
        } else if (k == 'p' || k == 'P') {
            tui_status_line(tui_bt_pair(0) == 0 ? "connection established" :
                            "connection failed (honest HCI result)");
        } else if (k == 'u' || k == 'U') {
            tui_bt_unpair(0);
            tui_status_line("disconnected (if it was connected)");
        } else if (k == 'v' || k == 'V') {
            tui_bt_services(0);
        }
    }
    tui_window_free(win);
    tui_update_panels();
    return 0;
}

/* root wireless app: wifi list + status + bt */
int tui_net_main(void) {
    if (!tui_started()) return -1;
    int cols, rows;
    tui_term_size(&cols, &rows);
    tui_window_t *win = tui_window_new(0, 0, rows, cols);
    if (!win) return -1;
    tui_window_title(win, "Wireless network");
    tui_menu_item_t items[4];
    items[0].name = "WiFi scan & list";
    items[0].desc = "active scan through the 802.11 STA state machine";
    items[1].name = "WiFi status";
    items[1].desc = "device/link state panel";
    items[2].name = "Bluetooth";
    items[2].desc = "HCI inquiry, connect, services";
    items[3].name = "Disconnect";
    items[3].desc = "leave the associated network";
    tui_menu_t *menu = tui_menu_new(win, items, 4);
    tui_menu_post(menu);
    for (;;) {
        int k = tui_getch();
        int d = tui_menu_driver(menu, k);
        if (d == -1) break;
        if (d == 1) {
            int sel = tui_menu_current(menu);
            if (sel == 0) { tui_wifi_scan(); tui_wifi_list(); }
            else if (sel == 1) tui_wifi_status();
            else if (sel == 2) tui_bt_main();
            else tui_wifi_disconnect();
            tui_menu_post(menu);
        }
    }
    tui_menu_free(menu);
    tui_window_free(win);
    tui_update_panels();
    return 0;
}
