/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP-l1apps-merge - L1 app "sound-tool" (the audio
 * mega-app).
 *
 * Merge surface (the WP-l1apps-merge contract):
 *   sound-tool                -> the menu wizard below
 *   sound-tool direct <op> .. -> explicit verbatim passthrough
 *   sound-tool <op> [args]    -> classic verbatim dispatch
 *
 * The table carries the whole snd service surface (sound ..
 * sample_rate_test) - 17 ops, zero functionality loss; the
 * single-command apps stay registered.
 *
 * The wizard groups by intent: device overview, volume (the number
 * is validated before it ever reaches the service), the 440 Hz test
 * tone, per-driver status and the selftest family. */
#include "l1_lib.h"
#include "l1svc_ops.h"
#include "wizard_lib.h"

struct wiz_op {
    const char *name;
    long cat;
    long op;
};

static const struct wiz_op g_ops[] = {
    {"sound",           L1SVC_CAT_SND, L1SVC_SND_SOUND},
    {"hda",             L1SVC_CAT_SND, L1SVC_SND_HDA},
    {"ac97",            L1SVC_CAT_SND, L1SVC_SND_AC97},
    {"sb16",            L1SVC_CAT_SND, L1SVC_SND_SB16},
    {"es1370",          L1SVC_CAT_SND, L1SVC_SND_ES1370},
    {"virtiosnd",       L1SVC_CAT_SND, L1SVC_SND_VIRTIOSND},
    {"usbaudio",        L1SVC_CAT_SND, L1SVC_SND_USBAUDIO},
    {"play",            L1SVC_CAT_SND, L1SVC_SND_PLAY},
    {"volume",          L1SVC_CAT_SND, L1SVC_SND_VOLUME},
    {"hda_test",        L1SVC_CAT_SND, L1SVC_SND_HDA_TEST},
    {"ac97_test",       L1SVC_CAT_SND, L1SVC_SND_AC97_TEST},
    {"sb16_test",       L1SVC_CAT_SND, L1SVC_SND_SB16_TEST},
    {"es1370_test",     L1SVC_CAT_SND, L1SVC_SND_ES1370_TEST},
    {"virtio_snd_test", L1SVC_CAT_SND, L1SVC_SND_VIRTIO_TEST},
    {"usb_audio_test",  L1SVC_CAT_SND, L1SVC_SND_USB_TEST},
    {"audio_rw_test",   L1SVC_CAT_SND, L1SVC_SND_RW_TEST},
    {"sample_rate_test",L1SVC_CAT_SND, L1SVC_SND_RATE_TEST},
};
#define G_OPS_N ((int)(sizeof(g_ops) / sizeof(g_ops[0])))

static char g_out[WIZ_OUT_CAP];

static const struct wiz_op *find_op(const char *name) {
    for (int i = 0; i < G_OPS_N; i++)
        if (streq_(g_ops[i].name, name)) return &g_ops[i];
    return (const struct wiz_op *)0;
}

static int run_op(const struct wiz_op *o, int argc, char **argv, int start) {
    char arg[256];
    int len = 0;
    arg[0] = 0;
    for (int j = start; j < argc && len < (int)sizeof(arg) - 1; j++) {
        if (j > start && len < (int)sizeof(arg) - 2) arg[len++] = ' ';
        for (const char *p = argv[j]; *p && len < (int)sizeof(arg) - 1; p++)
            arg[len++] = *p;
        arg[len] = 0;
    }
    long rc = sys_l1svc(o->cat, o->op, arg[0] ? (long)arg : 0,
                        (long)g_out, (long)sizeof(g_out));
    if (rc != 0) {
        puts_(o->name);
        puts_(tr_("svc_call_failed"));
        return 1;
    }
    puts_(g_out);
    if (g_out[0] && g_out[strlen_(g_out) - 1] != '\n') putc_('\n');
    return 0;
}

static void usage(void) {
    puts_("usage: sound-tool            (interactive menu)\n");
    puts_("       sound-tool direct <op> [args]   (explicit passthrough)\n");
    puts_("       sound-tool <op> [args]          (classic op dispatch)\n");
    puts_("ops:");
    for (int i = 0; i < G_OPS_N; i++) {
        puts_("  ");
        puts_(g_ops[i].name);
        putc_('\n');
    }
}

/* digits-only check (the volume op clamps to 0..100 itself) */
static int all_digits(const char *s) {
    if (!s || !s[0]) return 0;
    for (const char *p = s; *p; p++)
        if (*p < '0' || *p > '9') return 0;
    return 1;
}

/* ---- submenus -------------------------------------------------------- */
static void submenu_drivers(void) {
    static const char *const items[] = {
        "st_drv_hda", "st_drv_ac97", "st_drv_sb16",
        "st_drv_es1370", "st_drv_virtiosnd", "st_drv_usbaudio"};
    static const long ops[] = {
        L1SVC_SND_HDA, L1SVC_SND_AC97, L1SVC_SND_SB16,
        L1SVC_SND_ES1370, L1SVC_SND_VIRTIOSND, L1SVC_SND_USBAUDIO};
    for (;;) {
        int c = wiz_menu(tr_("st_drv_title"), items, 6);
        if (c == 0) return;
        (void)wiz_svc(L1SVC_CAT_SND, ops[c - 1], 0, g_out);
        wiz_pause();
    }
}

static void submenu_selftest(void) {
    static const char *const items[] = {
        "st_t_rw", "st_t_rate", "st_t_all"};
    for (;;) {
        int c = wiz_menu(tr_("st_t_title"), items, 3);
        if (c == 0) return;
        if (c == 1) {
            (void)wiz_svc(L1SVC_CAT_SND, L1SVC_SND_RW_TEST, 0, g_out);
        } else if (c == 2) {
            (void)wiz_svc(L1SVC_CAT_SND, L1SVC_SND_RATE_TEST, 0, g_out);
        } else {
            /* every registered driver's own selftest, one by one */
            static const long tops[] = {
                L1SVC_SND_HDA_TEST, L1SVC_SND_AC97_TEST,
                L1SVC_SND_SB16_TEST, L1SVC_SND_ES1370_TEST,
                L1SVC_SND_VIRTIO_TEST, L1SVC_SND_USB_TEST};
            for (int i = 0; i < 6; i++)
                (void)wiz_svc(L1SVC_CAT_SND, tops[i], 0, g_out);
        }
        wiz_pause();
    }
}

/* ---- the main menu --------------------------------------------------- */
static int sound_wizard(void) {
    static const char *const items[] = {
        "st_m_sound", "st_m_volume", "st_m_play",
        "st_m_drivers", "st_m_selftest"};
    static char dev[48], num[24], arg[80];
    for (;;) {
        int c = wiz_menu(tr_("wiz_title"), items, 5);
        if (c == 0) return 0;
        if (c == 1) {
            (void)wiz_svc(L1SVC_CAT_SND, L1SVC_SND_SOUND, 0, g_out);
        } else if (c == 2) {
            wiz_ask(tr_("st_ask_dev_opt"), dev, (int)sizeof(dev));
            wiz_ask(tr_("st_ask_volume"), num, (int)sizeof(num));
            if (!all_digits(num)) {
                wiz_fail(tr_("st_bad_number"));
                continue;
            }
            strcpy_(arg, dev);
            if (arg[0]) strcat_(arg, " ");
            strcat_(arg, num);
            (void)wiz_svc(L1SVC_CAT_SND, L1SVC_SND_VOLUME, arg, g_out);
        } else if (c == 3) {
            wiz_ask(tr_("st_ask_dev_opt"), dev, (int)sizeof(dev));
            wiz_ask(tr_("st_ask_rate"), num, (int)sizeof(num));
            if (num[0] && !all_digits(num)) {
                wiz_fail(tr_("st_bad_number"));
                continue;
            }
            strcpy_(arg, dev);
            if (arg[0]) strcat_(arg, " ");
            strcat_(arg, num);
            (void)wiz_svc(L1SVC_CAT_SND, L1SVC_SND_PLAY, arg, g_out);
        } else if (c == 4) {
            submenu_drivers();
        } else {
            submenu_selftest();
        }
        if (c != 4 && c != 5) wiz_pause();
    }
}

int app_main(int argc, char **argv) {
    if (argc < 2) return sound_wizard();

    int start = 1;
    if (streq_(argv[1], "direct")) {
        if (argc < 3) { usage(); return 1; }
        start = 2;
    }
    const struct wiz_op *o = find_op(argv[start]);
    if (!o) {
        puts_("sound-tool: unknown op: ");
        puts_(argv[start]);
        putc_('\n');
        usage();
        return 1;
    }
    return run_op(o, argc, argv, start + 1);
}
