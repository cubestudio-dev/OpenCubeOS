/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 cubestudio-dev <cubestudio@qq.com>
 * Open Cube OS - WP-10-wireless_bluetooth
 *
 * File: shell/shell_cmds_wireless.c
 *
 * `wifi` command:
 *   wifi            - family/device registry status (honest states)
 *   wifi scan       - start a scan and print the beacon-derived list
 *   wifi test       - 802.11 core selftest + every family selftest
 *   wifi sdio       - SDHCI/SDIO bus status + selftest
 */
#include "shell_cmds_wireless.h"
#include "shell.h"
#include "driver_wireless.h"
#include "driver_sdhci.h"
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

static void put_hex(u64 v, int digits)
{
    char b[20];
    u64_to_hex(v, b, digits);
    put(b);
}

static const char *wl_state_str(wl_state_t s)
{
    switch (s) {
    case WL_STATE_DOWN:          return "down";
    case WL_STATE_READY:         return "ready";
    case WL_STATE_SCANNING:      return "scanning";
    case WL_STATE_AUTHENTICATING:return "authenticating";
    case WL_STATE_ASSOCIATING:   return "associating";
    case WL_STATE_ASSOCIATED:    return "associated";
    default:                     return "?";
    }
}

int shell_cmd_wifi(const char *args)
{
    const char *p = args;
    while (*p == ' ' || *p == '\t') p++;

    if (word_is(&p, "test")) {
        char rep[2048];
        int fails = 0;
        fails += driver_wireless_selftest(rep, (int)sizeof(rep));
        put(rep);
        /* WP-10-tui-qa: iterate the DEVICE registry for per-device
         * family selftests.  The old loop indexed wl_device_get() with
         * the FAMILY count - with no hardware (QEMU) zero selftests
         * ran and the 'wireless test' line silently covered nothing;
         * with hardware it mismatched family i against device i.
         * Families without a device are now reported by name so the
         * registry state is visible either way. */
        for (int f = 0; f < wl_family_count(); f++) {
            int has_dev = 0;
            for (int d = 0; d < wl_device_count(); d++) {
                wl_device_t *dv = wl_device_get(d);
                if (dv && dv->ops && dv->ops->name &&
                    wl_family_name(f) && strcmp(dv->ops->name,
                                                wl_family_name(f)) == 0) {
                    has_dev = 1;
                    break;
                }
            }
            if (!has_dev) {
                put("  ");
                put(wl_family_name(f) ? wl_family_name(f) : "?");
                put(": no device (selftest needs hardware)\n");
            }
        }
        for (int d = 0; d < wl_device_count(); d++) {
            wl_device_t *dv = wl_device_get(d);
            if (dv && dv->ops && dv->ops->selftest) {
                char frep[1024];
                int ff = dv->ops->selftest(dv, frep, (int)sizeof(frep));
                fails += ff;
                put("  ");
                put(dv->ops->name ? dv->ops->name : "?");
                put(":\n");
                put(frep);
            }
        }
        put("wifi test: ");
        put_dec((u64)fails);
        put(fails == 0 ? " failures - PASS\n" : " failures - FAIL\n");
        return fails == 0 ? 0 : 1;
    }

    if (word_is(&p, "scan")) {
        wl_scan_clear();
        if (wl_device_count() == 0) {
            put("wifi scan: no wireless devices registered\n");
            return 1;
        }
        for (int i = 0; i < wl_device_count(); i++) {
            wl_device_t *d = wl_device_get(i);
            if (d && d->state >= WL_STATE_READY) {
                (void)wl_scan_start(i);
            }
        }
        /* passive scan: beacons arrive through the family RX path;    */
        /* without hardware the list stays empty (honest output).      */
        put("wifi scan: ");
        put_dec((u64)wl_scan_result_count());
        put(" networks seen");
        put(wl_scan_result_count() == 0 ?
            " (no beacons - no radio present)\n" : "\n");
        for (int i = 0; i < wl_scan_result_count(); i++) {
            const wl_scan_result_t *r = wl_scan_result_get(i);
            char mac[18];
            wl_mac_to_str(r->bssid, mac);
            put(mac);
            put(" ch ");
            put_dec(r->channel);
            put(" ");
            put(r->ssid);
            put("\n");
        }
        return 0;
    }

    if (word_is(&p, "sdio")) {
        char rep[1024];
        put("sdio bus: state=");
        put(driver_sdhci_state());
        put(" hosts=");
        put_dec((u64)driver_sdhci_host_count());
        put("\n");
        (void)driver_sdhci_selftest(rep, (int)sizeof(rep));
        put(rep);
        return 0;
    }

    /* default: registry status                                         */
    put("wireless families: ");
    put_dec((u64)wl_family_count());
    put("  devices: ");
    put_dec((u64)wl_device_count());
    put("\n");
    for (int i = 0; i < wl_family_count(); i++) {
        put("  ");
        put(wl_family_name(i) ? wl_family_name(i) : "?");
        put("\n");
    }
    for (int i = 0; i < wl_device_count(); i++) {
        wl_device_t *d = wl_device_get(i);
        if (!d) continue;
        put("  dev");
        put_dec((u64)i);
        put(" ");
        put(d->ops && d->ops->name ? d->ops->name : "?");
        put(" bus=");
        put(d->bus_name ? d->bus_name : "?");
        put(" vid=");
        put_hex((u64)d->vendor_id, 4);
        put(" did=");
        put_hex((u64)d->device_id, 4);
        put(" state=");
        put(wl_state_str(d->state));
        put("\n");
    }
    put("(QEMU has no 802.11 models; down = device not present)\n");
    return 0;
}


/* ---- v2.0.0 file-local command declarations (collected at boot; see core_registry.h) ---- */
OC_SHELL_CMD("wifi", shell_cmd_wifi, "wireless: status/scan/test/sdio (802.11 families + SDIO bus)", "wireless");
