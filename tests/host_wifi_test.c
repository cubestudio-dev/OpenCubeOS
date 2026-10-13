/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 cubestudio-dev <cubestudio@qq.com>
 * Open Cube OS - WP-10-wireless_bluetooth
 *
 * File: tests/host_wifi_test.c
 *
 * Host-side byte-level test of the 802.11 core (drivers/wireless/
 * driver_wireless.c).  The SAME kernel .c is compiled for the host so the
 * exact production code paths run here: frame control parsing, IE walks,
 * beacon -> scan-list assembly, auth/assoc builders + parsers, the
 * Ethernet <-> 802.11 data bridge and the family/device registry state
 * machine.  Every frame is a byte-exact IEEE 802.11-2016 fixture.
 *
 * Build (same pattern as the other host tests, e.g. host_fix4_tar_test):
 *   gcc -O2 -g -Ikernel -Ikernel/lib -Ikernel/mem -Idrivers/wireless \
 *       tests/host_wifi_test.c -o /tmp/host_wifi_test
 * (do NOT include <string.h>: the kernel's own lib_string.c below provides
 *  memset/memcpy/strcmp/... for the freestanding code)
 *
 * Exit code = number of failing checks.
 */
#include <stdio.h>
#include <stdlib.h>

/* --- kernel service shims (freestanding code compiled for host) ------ */
void *kmalloc(unsigned long long size) { return malloc(size); }
void  kfree(void *p)                   { free(p); }

#include "../kernel/lib/lib_string.c"
#include "../drivers/wireless/driver_wireless.c"

static int check(const char *name, int cond)
{
    printf("%-58s %s\n", name, cond ? "PASS" : "FAIL");
    return cond ? 0 : 1;
}

/* --- a real test family: tx_frame counts, hw ops succeed ------------- */
static int test_tx_count;
static int test_attach_ok, test_fw_ok, test_fw_fail;

static int tf_tx_frame(wl_device_t *dev, const u8 *frame, u16 len)
{
    (void)dev; (void)frame;
    test_tx_count++;
    return 0;
}
static int tf_attach(wl_device_t *dev)          { (void)dev; return test_attach_ok ? 0 : -1; }
static int tf_fw_load(wl_device_t *dev)         { (void)dev; return test_fw_fail ? -1 : (test_fw_ok ? 0 : 0); }
static int tf_scan_start(wl_device_t *dev)      { (void)dev; return 0; }

static const wl_family_ops_t test_family = {
    "host-test", 0, tf_attach, tf_fw_load, tf_scan_start, tf_tx_frame, 0,
};

int main(void)
{
    int fails = 0;
    char report[2048];

    /* ---- 1. core selftest (same vectors as the in-kernel wifi test) -- */
    {
        int f = driver_wireless_selftest(report, (int)sizeof(report));
        printf("core selftest report:\n%s", report);
        fails += check("core selftest 0 fails", f == 0);
    }

    /* ---- 2. registry lifecycle against a real test family ------------ */
    fails += check("init clean", driver_wireless_init() == 0);
    fails += check("register family", wl_register_family(&test_family) == 0);
    fails += check("duplicate family rejected",
                   wl_register_family(&test_family) == -3);
    fails += check("family count 1", wl_family_count() == 1);
    fails += check("family name", wl_family_name(0) != 0 &&
                   wl_family_name(0)[0] == 'h');
    fails += check("register device", wl_register_device(0, "PCI", 0x8086, 0x08b1) == 0);
    fails += check("bad family id rejected",
                   wl_register_device(9, "PCI", 0, 0) == -1);
    fails += check("device count 1", wl_device_count() == 1);

    {
        wl_device_t *d = wl_device_get(0);
        fails += check("device state DOWN", d != 0 && d->state == WL_STATE_DOWN);
        fails += check("tx while DOWN refused", wl_tx_frame(d, (const u8 *)"\x40\x00", 2) == -2);

        /* bring-up: fw present -> READY                                */
        test_attach_ok = 1; test_fw_ok = 1; test_fw_fail = 0;
        fails += check("bring up ok", wl_device_bring_up(0) == 0);
        fails += check("device READY", d->state == WL_STATE_READY);
        {
            u8 f2[2] = { 0x40, 0x00 };
            fails += check("tx while READY counts", wl_tx_frame(d, f2, 2) == 0 &&
                           test_tx_count == 1 && d->tx_frames == 1);
        }

        /* scan starts (family scan_start = 0)                          */
        fails += check("scan start", wl_scan_start(0) == 0 &&
                       d->state == WL_STATE_SCANNING);
        fails += check("scan list empty", wl_scan_result_count() == 0);

        /* feed a real beacon through the RX path -> scan list fills    */
        {
            u8 bcn[68] = {
                0x80, 0x00, 0x00, 0x00,
                0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                0x02, 0x11, 0x22, 0x33, 0x44, 0x55,
                0x02, 0x11, 0x22, 0x33, 0x44, 0x55,
                0x34, 0x12,
                0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
                0x64, 0x00, 0x01, 0x00,
                0x00, 0x05, 'O','C','-','A','P',
                0x03, 0x01, 0x0b,
                0x30, 0x14, 0x01, 0x00,             /* RSN IE -> WPA2     */
                0x00, 0x0f, 0xac, 0x04, 0x01, 0x00, 0x00, 0x00,
                0xac, 0x04, 0x01, 0x00, 0x00, 0x00, 0xac, 0x02,
                0x00, 0x00,
            };
            fails += check("rx beacon into scan list",
                           wl_rx_frame(d, bcn, (u16)sizeof(bcn)) == 0 &&
                           d->rx_beacons == 1);
            fails += check("scan list size 1", wl_scan_result_count() == 1);
            {
                const wl_scan_result_t *r = wl_scan_result_get(0);
                fails += check("scan entry fields",
                               r != 0 && r->ssid_len == 5 &&
                               r->ssid[0] == 'O' && r->ssid[4] == 'P' &&
                               r->channel == 0x0b && r->has_rsn == 1 &&
                               r->bssid[5] == 0x55);
            }
            /* same beacon again -> refreshed in place, not duplicated */
            fails += check("scan dedup",
                           wl_rx_frame(d, bcn, (u16)sizeof(bcn)) == 0 &&
                           wl_scan_result_count() == 1);
            wl_scan_clear();
            fails += check("scan cleared", wl_scan_result_count() == 0);
        }

        /* bad frame -> rx_bad_fcs                                      */
        {
            u8 bad[3] = { 0x01, 0x02, 0x03 };  /* proto ver != 0        */
            fails += check("bad frame counted",
                           wl_rx_frame(d, bad, 3) == -2 &&
                           d->rx_bad_fcs == 1);
        }
    }

    /* ---- 3. honest failures: firmware absent keeps the device DOWN --- */
    {
        fails += check("device 2 registered", wl_register_device(0, "PCI", 0x168c, 0x002b) == 1);
        test_attach_ok = 1; test_fw_ok = 0; test_fw_fail = 1;
        fails += check("bring up fails w/o firmware", wl_device_bring_up(1) == -3);
        fails += check("device 2 stays DOWN", wl_device_get(1)->state == WL_STATE_DOWN);
        test_fw_fail = 0; test_fw_ok = 1; test_attach_ok = 0;
        fails += check("attach failure honest", wl_device_bring_up(1) == -2);
        wl_device_down(1);
        fails += check("device 2 DOWN after down()", wl_device_get(1)->state == WL_STATE_DOWN);
    }

    /* ---- 4. auth/assoc walk-through with the state machine ----------- */
    {
        wl_device_t *d = wl_device_get(0);
        u8 resp[8];
        u8 ap[6] = { 0x02, 0x11, 0x22, 0x33, 0x44, 0x55 };
        u8 rates[4] = { 0x82, 0x84, 0x0c, 0x96 };
        u8 frame[WL_FRAME_MAX];
        wl_frame_info_t fi;
        int len;

        d->n_rates = 4;
        for (int i = 0; i < 4; i++) d->rates[i] = rates[i];

        fails += check("connect sends auth (tx 2)",
                       wl_connect(0, ap, "OC-TEST") == 0 &&
                       test_tx_count == 2 && d->state == WL_STATE_AUTHENTICATING);

        /* the AP answers: auth success (alg 0, txn 2, status 0)         */
        {
            u8 auth_ok[34] = {
                0xb0, 0x00, 0x00, 0x00,
                0x02, 0x00, 0x00, 0x00, 0x00, 0x01,        /* dst = STA   */
                0x02, 0x11, 0x22, 0x33, 0x44, 0x55,        /* src = AP    */
                0x02, 0x11, 0x22, 0x33, 0x44, 0x55,        /* BSSID       */
                0x20, 0x00,
                0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
            };
            fails += check("auth resp advances state",
                           wl_rx_frame(d, auth_ok, 30) == 0 &&
                           d->state == WL_STATE_ASSOCIATING &&
                           test_tx_count == 3);   /* assoc req went out  */
        }
        /* assoc resp success: cap 0x0401, status 0, AID 5               */
        {
            u8 assoc_ok[36] = {
                0x10, 0x00, 0x00, 0x00,
                0x02, 0x00, 0x00, 0x00, 0x00, 0x01,
                0x02, 0x11, 0x22, 0x33, 0x44, 0x55,
                0x02, 0x11, 0x22, 0x33, 0x44, 0x55,
                0x30, 0x00,
                0x01, 0x04, 0x00, 0x00, 0x05, 0xc0,
            };
            fails += check("assoc resp -> ASSOCIATED",
                           wl_rx_frame(d, assoc_ok, 30) == 0 &&
                           d->state == WL_STATE_ASSOCIATED &&
                           d->aid == 5 && d->capability == 0x0401);
        }

        /* data frame arrives from the AP -> decapsulated into the ring */
        {
            u8 data[38] = {
                0x08, 0x02, 0x00, 0x00,                 /* data, from_ds  */
                0x50, 0x50, 0x50, 0x50, 0x50, 0x50,     /* DA             */
                0x02, 0x11, 0x22, 0x33, 0x44, 0x55,     /* BSSID          */
                0x60, 0x60, 0x60, 0x60, 0x60, 0x60,     /* SA             */
                0x40, 0x00,
                0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00, 0x08, 0x00,
                0xde, 0xad, 0xbe, 0xef, 0x12, 0x34,
            };
            u8 eth[1522];
            u16 elen = 0;
            fails += check("data rx queued", wl_rx_frame(d, data, (u16)sizeof(data)) == 0);
            fails += check("netif recv 1", wl_netif_recv(eth, sizeof(eth), &elen) == 1);
            fails += check("netif frame bytes",
                           elen == 14 + 6 && eth[12] == 0x08 && eth[13] == 0x00 &&
                           eth[0] == 0x50 && eth[6] == 0x60 &&
                           eth[14] == 0xde && eth[17] == 0xef);
            fails += check("netif empty after drain",
                           wl_netif_recv(eth, sizeof(eth), &elen) == 0);
        }

        /* disconnect sends deauth and resets state                      */
        fails += check("disconnect", wl_disconnect(0) == 0 &&
                       d->state == WL_STATE_READY && d->aid == 0);
        (void)resp; (void)frame; (void)len; (void)fi;
    }

    /* ---- 5. malformed-input honesty (truncated / non-LLC frames) ----- */
    {
        u8 junk[10] = { 0x08, 0x01, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06 };
        u8 eth[1522];
        u16 elen;
        fails += check("short data frame rejected",
                       wl_80211_data_to_eth(eth, &elen, junk, 10) < 0);
        {
            /* data frame with non-SNAP body -> rejected                  */
            u8 d2[40] = {
                0x08, 0x01, 0x00, 0x00,
                0x50, 0x50, 0x50, 0x50, 0x50, 0x50,
                0x02, 0x11, 0x22, 0x33, 0x44, 0x55,
                0x60, 0x60, 0x60, 0x60, 0x60, 0x60,
                0x40, 0x00,
                0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                0x09, 0x0a, 0x0b, 0x0c,
            };
            fails += check("non-LLC body rejected",
                           wl_80211_data_to_eth(eth, &elen, d2, 40) == -4);
        }
        {
            /* IE walk over a truncated element -> -2                     */
            u8 ies[3] = { 0x00, 0x40, 0x61 };  /* len 64 but only 1 byte */
            int pos = 0; u8 id, elen2;
            const u8 *data;
            fails += check("truncated IE rejected",
                           wl_walk_ie(ies, 3, &pos, &id, &data, &elen2) == -2);
        }
    }

    printf("\n%s: %d checks failed\n", "host_wifi_test", fails);
    return fails;
}
