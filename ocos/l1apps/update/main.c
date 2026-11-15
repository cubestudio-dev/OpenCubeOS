/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP-l1apps-merge - L1 app "update" (the OTA
 * mega-app).
 *
 * Merge surface (the WP-l1apps-merge contract):
 *   update                -> the 5-step intent wizard below
 *   update direct <args>  -> the historical OTA update op, verbatim,
 *                            with the literal word "direct" skipped
 *   update <args>         -> the historical OTA update op, verbatim
 *                            (byte-identical to the pre-merge thin
 *                            app - l1svc_main semantics, so scripts
 *                            keep working)
 *
 * Zero functionality loss: the historical bare `update` (online
 * update flow) is exactly `update direct`; every other argument shape
 * is the same call it always was.  The A/B, checkupdate, config and
 * rollback ops stay reachable through their own apps AND through the
 * wizard's steps.
 *
 * The wizard (intent: "bring this machine to the newest version"):
 *   1/5 network   - `ip` status; a 0.0.0.0 address offers a DHCP fix
 *                   (cross-domain: the net service), then rechecks
 *   2/5 A/B disk  - `update status`; when the A/B disk is absent the
 *                   wizard walks the user through `lsblk` + a
 *                   double-confirmed `abdisk <dev>` (cross-domain:
 *                   the block service) and rechecks
 *   3/5 DNS       - resolves the update host parsed out of the live
 *                   `config get update_url` value
 *   4/5 check     - `checkupdate`; up-to-date ends the wizard OK
 *   5/5 install   - confirmed online update, then a confirmed reboot
 *
 * All probes read the SERVICE's own English output lines (the stable
 * script-facing contract): "IP:", "DHCP success", "A/B disk:
 * present", "New version available.", "Current version is up to
 * date.", "DNS resolution failed", "reboot required". */
#include "l1_lib.h"
#include "l1svc_ops.h"
#include "wizard_lib.h"

/* ---- the intent wizard ---------------------------------------------- */
static int update_wizard(void) {
    static char out[WIZ_OUT_CAP];
    static char ipbuf[32];
    static char url[256];
    static char host[128];
    static char dev[64];
    static char dev2[64];

    wiz_banner(tr_("wiz_title"));
    wiz_pause();

    /* 1/5 network ---------------------------------------------------- */
    wiz_step(1, 5, tr_("step_net"));
    if (wiz_svc_quiet(L1SVC_CAT_NET, L1SVC_NET_IP, 0, out) != 0) {
        wiz_fail(tr_("net_probe_failed"));
        return 1;
    }
    const char *ip = wiz_token_after(out, "IP:", ipbuf, (int)sizeof(ipbuf));
    if (ip && ip[0] && !streq_(ip, "0.0.0.0")) {
        wiz_ok(tr_("net_got_ip"));
    } else {
        wiz_fail(tr_("net_no_ip"));
        if (!wiz_confirm(tr_("net_offer_dhcp"))) return 1;
        if (wiz_svc(L1SVC_CAT_NET, L1SVC_NET_DHCP, 0, out) != 0) return 1;
        if (!wiz_find(out, "DHCP success")) {
            wiz_fail(tr_("net_still_no_ip"));
            return 1;
        }
        wiz_ok(tr_("net_got_ip"));
    }

    /* 2/5 A/B disk --------------------------------------------------- */
    wiz_step(2, 5, tr_("step_ab"));
    for (;;) {
        if (wiz_svc_quiet(L1SVC_CAT_OTA, L1SVC_OTA_UPDATE, "status",
                          out) != 0) {
            wiz_fail(tr_("ab_probe_failed"));
            return 1;
        }
        if (wiz_find(out, "A/B disk: present")) {
            wiz_ok(tr_("ab_present"));
            break;
        }
        wiz_fail(tr_("ab_missing"));
        puts_(out);
        puts_(tr_("ab_list"));
        putc_('\n');
        if (wiz_svc(L1SVC_CAT_BLOCK, L1SVC_BLK_LSBLK, 0, out) != 0)
            return 1;
        wiz_ask(tr_("ab_ask_dev"), dev, (int)sizeof(dev));
        if (!dev[0]) return 1;
        wiz_ask(tr_("ab_retype"), dev2, (int)sizeof(dev2));
        if (!streq_(dev, dev2)) {
            wiz_fail(tr_("ab_mismatch"));
            return 1;
        }
        puts_(tr_("ab_warn"));
        putc_('\n');
        if (!wiz_confirm(tr_("ab_confirm"))) return 1;
        if (wiz_svc(L1SVC_CAT_BLOCK, L1SVC_BLK_ABDISK, dev, out) != 0)
            return 1;
        /* loop: the re-check at the top proves the fix landed */
    }

    /* 3/5 DNS -------------------------------------------------------- */
    wiz_step(3, 5, tr_("step_dns"));
    if (wiz_svc_quiet(L1SVC_CAT_OTA, L1SVC_OTA_CONFIG,
                      "get update_url", out) != 0 || !out[0]) {
        wiz_fail(tr_("dns_nourl"));
        return 1;
    }
    /* out = "https://host/path..."; take the authority component */
    strcpy_(url, out);
    const char *p = url;
    while (*p && *p != ':') p++;
    if (p[0] == ':' && p[1] == '/' && p[2] == '/') {
        p += 3;
        int n = 0;
        while (p[n] && p[n] != '/' && n < (int)sizeof(host) - 1) {
            host[n] = p[n];
            n++;
        }
        host[n] = 0;
    } else {
        host[0] = 0;
    }
    if (!host[0]) {
        wiz_fail(tr_("dns_nourl"));
        return 1;
    }
    if (wiz_svc(L1SVC_CAT_NET, L1SVC_NET_DNS, host, out) != 0) return 1;
    if (wiz_find(out, "DNS resolution failed")) {
        wiz_fail(tr_("dns_fail"));
        return 1;
    }
    wiz_ok(tr_("dns_ok"));

    /* 4/5 check ------------------------------------------------------ */
    wiz_step(4, 5, tr_("step_check"));
    if (wiz_svc(L1SVC_CAT_OTA, L1SVC_OTA_CHECKUPDATE, 0, out) != 0)
        return 1;
    if (wiz_find(out, "Current version is up to date.")) {
        wiz_ok(tr_("uptodate"));
        return 0;
    }
    if (!wiz_find(out, "New version available.")) {
        wiz_fail(tr_("check_unknown"));
        return 1;
    }

    /* 5/5 install ---------------------------------------------------- */
    wiz_step(5, 5, tr_("step_install"));
    if (!wiz_confirm(tr_("install_ask"))) {
        puts_(tr_("install_skipped"));
        return 0;
    }
    if (wiz_svc(L1SVC_CAT_OTA, L1SVC_OTA_UPDATE, "", out) != 0) return 1;
    if (!wiz_find(out, "reboot required")) {
        wiz_fail(tr_("install_failed"));
        return 1;
    }
    wiz_ok(tr_("step_reboot"));
    if (!wiz_confirm(tr_("reboot_ask"))) {
        puts_(tr_("reboot_skipped"));
        return 0;
    }
    wiz_svc(L1SVC_CAT_POWER, L1SVC_PWR_REBOOT, 0, out);
    return 0;
}

int app_main(int argc, char **argv) {
    /* no arguments: the intent wizard */
    if (argc < 2) return update_wizard();

    /* "direct": explicit verbatim passthrough - the literal word is
     * skipped and the remaining argv reaches the historical op with
     * the original app name as argv[0] (so the error line and every
     * byte of output stay identical to the pre-merge app). */
    if (streq_(argv[1], "direct")) {
        char *av[64];
        int n = 0;
        av[n++] = argv[0];
        for (int i = 2; i < argc && n < (int)(sizeof(av) / sizeof(av[0])) - 1;
             i++)
            av[n++] = argv[i];
        return l1svc_main(n, av, L1SVC_CAT_OTA, L1SVC_OTA_UPDATE);
    }

    /* classic: byte-identical wrap of the historical update op */
    return l1svc_main(argc, argv, L1SVC_CAT_OTA, L1SVC_OTA_UPDATE);
}
