/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 cubestudio-dev <cubestudio@qq.com>
 * Open Cube OS - WP-10-wireless_bluetooth
 *
 * File: shell/shell_cmds_bt.c
 *
 * `bt` command:
 *   bt            - transport + controller status
 *   bt usb        - HCI-USB transport status
 *   bt uart       - H4 UART transport status
 *   bt test       - the real flow against the attached transport:
 *                   Reset -> Read_Local_Version -> Read_BD_ADDR ->
 *                   Set_Event_Mask -> Inquiry -> Create_Connection ->
 *                   L2CAP echo -> SDP SPP channel -> RFCOMM DLC ->
 *                   SPP echo -> disconnect.  Every step's verdict comes
 *                   from real controller/peer responses; a missing
 *                   transport prints an honest no-transport line.
 */
#include "shell_cmds_bt.h"
#include "shell.h"
#include "driver_bt.h"
#include "screen_console.h"
#include "lib_string.h"
#include "core_registry.h"

static int word_is(const char **p, const char *w) {
    const char *s = *p;
    while (*s && *w) {
        char a = *s, b = *w;
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return 0;
        s++; w++;
    }
    if (*w) return 0;
    *p = s;
    return 1;
}

static void put(const char *s) { screen_console_puts(s); }

static void put_dec(u64 v)
{
    char b[24];
    u64_to_str(v, b);
    put(b);
}

static void put_addr(const u8 a[6])
{
    char s[18];
    bt_addr_to_str(a, s);
    put(s);
}

static int test_step(const char *name, int cond)
{
    put("  ");
    put(name);
    put(": ");
    put(cond ? "PASS" : "FAIL");
    put("\n");
    return cond ? 0 : 1;
}

int shell_cmd_bt(const char *args)
{
    const char *p = args;
    int fails = 0;
    char stat[256];

    while (*p == ' ' || *p == '\t') p++;

    if (word_is(&p, "usb")) {
        (void)bt_usb_ops()->poll_status(stat, (int)sizeof(stat));
        put(stat);
        return 0;
    }
    if (word_is(&p, "uart")) {
        (void)bt_h4_ops()->poll_status(stat, (int)sizeof(stat));
        put(stat);
        return 0;
    }

    if (!word_is(&p, "test")) {
        /* plain status: query the controller for real when no BD addr is
     * known yet (bt_hci_present is false until the first read)         */
        if (!bt_hci_present() && strcmp(bt_hci_transport_name(), "none") != 0)
        {
            u8 a[6];
            (void)bt_hci_read_bd_addr(a);
        }
        put("bluetooth: transport=");
        put(bt_hci_transport_name());
        put(" controller=");
        {
            char a[18];
            u8 addr[6];
            if (bt_hci_present() && bt_hci_read_bd_addr(addr) == 0) {
                bt_addr_to_str(addr, a);
                put(a);
            } else {
                put("not-present");
            }
        }
        put(" conns=");
        put_dec((u64)bt_hci_conn_count());
        put(" l2cap=");
        put_dec((u64)bt_l2cap_channel_count());
        put("\n");
        {
            char d[512];
            bt_l2cap_dump(d, (int)sizeof(d));
            put(d);
        }
        return 0;
    }

    /* ---------------- bt test: the real flow ------------------------ */
    if (!bt_hci_transport_name() || strcmp(bt_hci_transport_name(),
                                           "none") == 0) {
        put("bt test: no transport (no USB dongle, no COM2 controller)\n");
        return 1;
    }
    {
        u8 addr[6];
        u8 hv, lv;
        u16 hr, mf, ls;
        bt_inquiry_result_t iq[4];
        int iq_n;
        u16 handle = 0;
        u8 echo_req[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
        u8 echo_resp[16];
        u16 echo_len = (u16)sizeof(echo_resp);
        u16 spp_dlci = 0;
        char echo_msg[16] = "oc-spp-echo";
        char echo_out[64];
        u16 cid;

        put("bt test: transport=");
        put(bt_hci_transport_name());
        put("\n");

        fails += test_step("hci reset", bt_hci_reset() == 0);
        fails += test_step("read local version",
                           bt_hci_read_local_version(&hv, &hr, &lv,
                                                     &mf, &ls) == 0);
        {
            char line[96];
            int o = 0;
            const char *pre = "  controller: hci ";
            const char *mfr = ", mfr 0x";
            while (pre[o]) { line[o] = pre[o]; o++; }
            {
                char n[8];
                u64_to_str((u64)hv, n);
                for (int i = 0; n[i]; i++) line[o++] = n[i];
            }
            for (int i = 0; mfr[i]; i++) line[o++] = mfr[i];
            {
                char n[8];
                u64_to_hex((u64)mf, n, 4);
                for (int i = 0; n[i]; i++) line[o++] = n[i];
            }
            line[o++] = '\n';
            line[o] = '\0';
            put(line);
        }
        fails += test_step("read bd addr", bt_hci_read_bd_addr(addr) == 0);
        {
            put("  bd_addr: ");
            put_addr(addr);
            put("\n");
        }
        /* WP-10-tui-qa: try the FULL mask first.  The old
         * mask(0) || mask(all) short-circuit accepted the all-events-
         * OFF mask whenever the controller allowed it (every real one
         * does), so no event ever reached the stack afterwards and the
         * inquiry step below failed against a perfectly healthy peer.
         * Full mask first; fall back to the zero mask only when the
         * controller refuses the full one. */
        {
            int mask_rc = bt_hci_set_event_mask(0xffffffffffffffffull);
            if (mask_rc != 0)
                mask_rc = bt_hci_set_event_mask(0x0);
            fails += test_step("set event mask", mask_rc == 0);
        }

        /* inquiry: results come only from real controller events       */
        {
            int rs = bt_hci_inquiry_start();
            iq_n = bt_hci_inquiry_results(iq, 4);
            put("  inquiry: ");
            put_dec((u64)iq_n);
            put(iq_n == 1 ? " device(s) (expected 1 from the test peer)"
                          : " device(s)");
            put("\n");
            fails += test_step("inquiry sees test peer",
                               rs == 0 && iq_n >= 1);
        }

        if (iq_n >= 1) {
            int cs = bt_hci_create_connection(iq[0].addr, &handle);
            fails += test_step("acl connection", cs == 0 && handle != 0);
            if (cs == 0) {
                /* L2CAP echo over the ACL link                        */
                int es = bt_l2cap_echo(handle, echo_req, 8,
                                       echo_resp, &echo_len);
                fails += test_step("l2cap echo", es == 0 &&
                                   echo_len == 8 &&
                                   echo_resp[0] == 1 &&
                                   echo_resp[7] == 8);
                /* SDP: SPP server channel                             */
                {
                    u16 ch = 0;
                    int ss = bt_sdp_get_spp_channel(iq[0].addr, &ch);
                    fails += test_step("sdp spp channel",
                                       ss == 0 && ch >= 1 && ch <= 30);
                    put("  spp server channel: ");
                    put_dec((u64)ch);
                    put(" (sdp err ");
                    put_dec((u64)(ss < 0 ? -ss : ss));
                    put(")\n");
                    /* RFCOMM mux + DLC + SPP echo                     */
                    if (ss == 0) {
                        bt_rfcomm_set_peer(handle);
                        int ro = bt_rfcomm_open(ch, &spp_dlci);
                        fails += test_step("rfcomm dlc",
                                           ro == 0 && spp_dlci != 0);
                        if (ro == 0) {
                            int ee = bt_spp_echo(spp_dlci, echo_msg,
                                                 echo_out,
                                                 (int)sizeof(echo_out));
                            fails += test_step("spp echo",
                                               ee == 0 &&
                                               strncmp(echo_out,
                                                       echo_msg, 11) == 0);
                            put("  spp echo: rc=");
                            put_dec((u64)(ee < 0 ? -ee : ee));
                            put(" out=[");
                            put(echo_out);
                            put("]\n");
                            spp_capture_clear();
                            (void)bt_rfcomm_close(spp_dlci);
                        }
                    }
                }
                /* HID: control + interrupt channels                   */
                if (bt_hid_connect(iq[0].addr) == 0) {
                    u8 rep[16];
                    u16 rl = (u16)sizeof(rep);
                    int gr = bt_hid_get_report(BT_HID_REPORT_INPUT, rep, &rl);
                    fails += test_step("hid get report", gr == 0 && rl > 0);
                    put("  hid report: ");
                    put_dec((u64)rl);
                    put(" bytes\n");
                    (void)bt_hid_disconnect();
                    (void)cid;
                } else {
                    fails += test_step("hid channels", 0);
                }
                (void)bt_hci_disconnect(handle);
            }
        }
        put("bt test: ");
        put_dec((u64)fails);
        put(fails == 0 ? " failures - PASS\n" : " failures - FAIL\n");
    }
    return fails == 0 ? 0 : 1;
}


/* ---- v2.0.0 file-local command declarations (collected at boot; see core_registry.h) ---- */
OC_SHELL_CMD("bt", shell_cmd_bt, "bluetooth: status/usb/uart/test (HCI/L2CAP/RFCOMM/SDP/SPP/HID)", "wireless");
