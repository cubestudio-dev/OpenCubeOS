/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP06c-installer_gui
 * File: ocse/ocse_wizard.c
 * Purpose: the 9-step install wizard - see ocse_wizard.h. All mutable
 * state sits in the .ocsework NOLOAD zone; strings are built with the
 * lib_string helpers (freestanding: no printf).
 */
#include "ocse_wizard.h"
#include "ocse_wzctl.h"
#include "ocse_cl.h"
#include "ocse_i18n.h"
#include "ocse_sha256.h"
#include "ocse_disk.h"
#include "screen_fb.h"
#include "driver_input_keyboard.h"
#include "core_sched.h"
#include "lib_string.h"
#include <stdint.h>

extern int g_ocse_hotkey_req;      /* ocse_main.c hotkey plumbing */
extern void ocse_serial_puts_raw(const char* s);

/* ---- work zone state ---- */
#define WZ_WORK __attribute__((section(".ocsework")))

#define WZ_CAP_SZ   4096
#define WZ_OUT_SZ   1024
#define WZ_CMD_SZ   160

static WZ_WORK char wz_out[WZ_OUT_SZ];     /* last command output    */
static WZ_WORK char wz_errcmd[WZ_CMD_SZ];  /* failed command copy    */
static WZ_WORK char wz_errout[WZ_OUT_SZ];  /* failed output copy     */
static WZ_WORK char wz_head[80];           /* per-page headline      */
static WZ_WORK char wz_line1[96];          /* dynamic label texts    */
static WZ_WORK char wz_line2[96];
static WZ_WORK char wz_line3[96];
static WZ_WORK char wz_line4[96];
static WZ_WORK char wz_line5[96];
static WZ_WORK char wz_line6[96];
static WZ_WORK char wz_rows[8][76];        /* error-page output rows */
static WZ_WORK char wz_disk[16];           /* "hda"                  */
static WZ_WORK int  wz_partno;             /* 1-based partition no   */
static WZ_WORK char wz_dev[24];            /* "hdap1"                */
static WZ_WORK char wz_user[32];
static WZ_WORK char wz_full[48];
static WZ_WORK char wz_pass_sha[65];
static WZ_WORK char wz_host[32];
static WZ_WORK char wz_upd[96];
static WZ_WORK char wz_auto[8];
static WZ_WORK char wz_gap[40];            /* "8191 MiB"             */
static WZ_WORK int  wz_gap_lo, wz_gap_hi;  /* largest gap, MiB       */
static WZ_WORK int  wz_have_disk;
static WZ_WORK int  wz_have_part;          /* wizard-created part    */
static WZ_WORK int  wz_fmt_ok;
static WZ_WORK int  wz_written;            /* 0 clean, 1 mkfs ran,
                                              2 system extracted     */
static WZ_WORK int  wz_failed_stage;       /* install stage to retry */
static WZ_WORK int  wz_err_page;           /* Back target for errors */

/* ---- pages ---- */
#define WZ_PG_LANG    0
#define WZ_PG_DISK    1
#define WZ_PG_PART    2
#define WZ_PG_FMT     3
#define WZ_PG_USER    4
#define WZ_PG_SYS     5
#define WZ_PG_CONFIRM 6
#define WZ_PG_INSTALL 7
#define WZ_PG_DONE    8
#define WZ_PG_ERROR   9
#define WZ_PG_CANCEL  10
#define WZ_PG_EXIT    11

/* install stages (the error page retries one stage) */
#define WZ_ST_MOUNT   0
#define WZ_ST_INSTALL 1
#define WZ_ST_CONFIG  2
#define WZ_ST_USER    3
#define WZ_ST_SYNC    4
#define WZ_ST_UMOUNT  5
#define WZ_ST_NONE    99

/* ==================================================================
 * command channel
 * ================================================================== */

/* Serial mirror of the wizard's command flow (grep-able; the command
 * OUTPUT already mirrors to serial through the capture path). */
static void wz_log(const char* cmd) {
    ocse_serial_puts_raw("[wizard] $ ");
    ocse_serial_puts_raw(cmd);
    ocse_serial_puts_raw("\r\n");
}

/* Run one command quietly (output captured, failures NOT recorded -
 * probes like the no-overwrite gate use this). */
static int wz_runq(const char* cmd) {
    wz_log(cmd);
    wz_out[0] = 0;
    return ocse_cl_exec(cmd, wz_out, sizeof(wz_out));
}

/* Run one RECORDING command: on failure the command + captured output
 * land on the error page and 'stage' marks the retry point. */
static int wz_run(const char* cmd, int stage, int err_page) {
    int rc = wz_runq(cmd);
    if (rc != 0) {
        strncpy(wz_errcmd, cmd, sizeof(wz_errcmd) - 1);
        wz_errcmd[sizeof(wz_errcmd) - 1] = 0;
        strncpy(wz_errout, wz_out, sizeof(wz_errout) - 1);
        wz_errout[sizeof(wz_errout) - 1] = 0;
        wz_failed_stage = stage;
        wz_err_page = err_page;
        return rc;
    }
    return 0;
}

/* Build "a b c d" without printf. */
static void wz_cat(char* dst, int cap, const char* a, const char* b,
                   const char* c, const char* d) {
    dst[0] = 0;
    const char* parts[4] = { a, b, c, d };
    for (int i = 0; i < 4; i++) {
        if (!parts[i]) continue;
        if (strlen(dst) + strlen(parts[i]) + 1 >= (usize)cap) return;
        strcat(dst, parts[i]);
    }
}

/* ==================================================================
 * capture-text parsing (the SAME lines the user sees)
 * ================================================================== */

/* lib_string has no strstr (freestanding); local helper as in
 * lib_klog.c. */
static const char* wz_strstr(const char* hay, const char* needle) {
    if (!*needle) return hay;
    for (; *hay; hay++) {
        const char* a = hay;
        const char* b = needle;
        while (*a && *b && *a == *b) { a++; b++; }
        if (!*b) return hay;
    }
    return 0;
}

static int tok(const char** p, char* out, int cap) {
    const char* s = *p;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == 0 || *s == '\n') { *p = s; return 0; }
    int n = 0;
    while (*s && *s != ' ' && *s != '\t' && *s != '\n' && n < cap - 1)
        out[n++] = *s++;
    out[n] = 0;
    while (*s && *s != '\n' && (*s == ' ' || *s == '\t')) s++;
    *p = s;
    return 1;
}

/* lsdisk capture -> list rows ("  hda [no table] 8192 MiB, ..."). */
static int parse_lsdisk(const char* cap, char rows[][WZ_ROW_LEN],
                        int max) {
    int n = 0;
    const char* p = cap;
    char line[WZ_ROW_LEN];
    while (*p && n < max) {
        int l = 0;
        while (*p && *p != '\n' && l < WZ_ROW_LEN - 1)
            line[l++] = *p++;
        line[l] = 0;
        if (*p == '\n') p++;
        if (strncmp(line, "  ", 2) != 0) continue;
        if (!wz_strstr(line, "MiB")) continue;
        strncpy(rows[n++], line, WZ_ROW_LEN - 1);
    }
    return n;
}

/* lsdisk capture -> disk size in MiB (first line with MiB). The
 * lsdisk line is "  hda [no table] 128 MiB, 0 part(s)" - the digits
 * are separated from "MiB" by a SPACE (the lspart "128MiB" format
 * has no gap; parse both). */
static int parse_lsdisk_mb(const char* cap) {
    const char* m = wz_strstr(cap, "MiB");
    if (!m) return 0;
    const char* q = m;
    while (q > cap && (q[-1] == ' ' || q[-1] == '\t')) q--;
    const char* e = q;
    while (q > cap && q[-1] >= '0' && q[-1] <= '9') q--;
    int v = 0;
    while (q < e && *q >= '0' && *q <= '9') v = v * 10 + (*q++ - '0');
    return v;
}

/* Count "#n" partition lines in an lspart capture. The lines look
 * like "  #1 FAT32 start=1MiB end=128MiB ..." (2-space prefix; the
 * WP06b lspart printer emits exactly two leading spaces). */
static int count_parts(const char* cap) {
    int n = 0;
    const char* p = cap;
    char line[WZ_ROW_LEN];
    while (*p) {
        int l = 0;
        while (*p && *p != '\n' && l < WZ_ROW_LEN - 1) line[l++] = *p++;
        line[l] = 0;
        if (*p == '\n') p++;
        if (strncmp(line, "  #", 3) == 0) n++;
    }
    return n;
}

/* lspart capture -> the largest free gap (MiB) >= 64. Lines (the
 * WP06b lspart format): "  #1 FAT32 start=1MiB end=128MiB size=127MiB
 * type=0x0C" - start/end arrive in MiB (no LBA conversion); the gap
 * walk covers [1 MiB .. disk end) minus the partitions. */
#define WZ_GAP_MIN 64
static int parse_lspart_gap(const char* cap, int disk_mb,
                            int* glo, int* ghi) {
    int start_mib[8];
    int end_mib[8];
    int np = 0;
    const char* p = cap;
    char line[WZ_ROW_LEN];
    while (*p && np < 8) {
        int l = 0;
        while (*p && *p != '\n' && l < WZ_ROW_LEN - 1) line[l++] = *p++;
        line[l] = 0;
        if (*p == '\n') p++;
        if (strncmp(line, "  #", 3) != 0) continue;
        const char* q = line;
        char t[32];
        (void)tok(&q, t, sizeof(t));            /* "#1"        */
        if (!tok(&q, t, sizeof(t))) continue;   /* fs name     */
        if (!tok(&q, t, sizeof(t))) continue;   /* start=..MiB */
        int sm = 0;
        { const char* r = t;
          while (*r && *r != '=') r++;
          if (*r == '=') r++;
          while (*r >= '0' && *r <= '9') sm = sm * 10 + (*r++ - '0'); }
        if (!tok(&q, t, sizeof(t))) continue;   /* end=..MiB */
        int em = 0;
        { const char* r = t;
          while (*r && *r != '=') r++;
          if (*r == '=') r++;
          while (*r >= '0' && *r <= '9') em = em * 10 + (*r++ - '0'); }
        if (em <= sm) continue;
        start_mib[np] = sm;
        end_mib[np] = em;
        np++;
    }
    for (int i = 1; i < np; i++)
        for (int j = i; j > 0 && start_mib[j] < start_mib[j - 1]; j--) {
            int a = start_mib[j]; start_mib[j] = start_mib[j - 1];
            start_mib[j - 1] = a;
            int b = end_mib[j]; end_mib[j] = end_mib[j - 1];
            end_mib[j - 1] = b;
        }
    int best = 0, blo = 0, bhi = 0;
    int cur = 1;
    for (int i = 0; i < np; i++) {
        int s = start_mib[i];
        int e = end_mib[i];
        if (s > cur) {
            int g = s - cur;
            if (g > best) { best = g; blo = cur; bhi = s; }
        }
        if (e > cur) cur = e;
    }
    if (disk_mb > cur) {
        int g = disk_mb - cur;
        if (g > best) { best = g; blo = cur; bhi = disk_mb; }
    }
    if (best >= WZ_GAP_MIN) {
        *glo = blo;
        *ghi = bhi;
        return best;
    }
    return 0;
}

/* ==================================================================
 * page plumbing
 * ================================================================== */
static void page_frame(const char* stepname, int stepno) {
    strcpy(wz_head, "Step ");
    char n[8];
    u64_to_str((u64)stepno, n);
    strcat(wz_head, n);
    strcat(wz_head, "/9  ");
    strcat(wz_head, stepname);
    wz_add_label(24, 56, 0, wz_head);
}

/* One modal page: pump keyboard + pointer until a button fires. */
static int page_pump(void) {
    for (;;) {
        if (g_ocse_hotkey_req)
            return WZA_CANCEL;    /* wizard_run exits before overlay */
        while (driver_input_keyboard_has_key()) {
            int k = driver_input_keyboard_getch();
            int act = wz_handle_key(k);
            if (act != WZ_ACT_NONE) return act;
        }
        int act = wz_poll_click();
        if (act != WZ_ACT_NONE) return act;
        wz_repaint();
        core_sched_yield();
    }
}

/* ==================================================================
 * step 1: welcome + language
 * ================================================================== */
static int wz_page_lang(void) {
    wz_reset();
    page_frame(ocse_tr(OCSE_W_STEP_LANG), 1);
    wz_add_label(24, 96, OCSE_W_WELCOME1, NULL);
    wz_add_label(24, 130, OCSE_W_WELCOME2, NULL);
    wz_add_label(24, 186, OCSE_W_LANG_HEAD, NULL);
    wz_add_button(24, 220, 200, 46, OCSE_T_BTN_LANG, NULL, WZA_LANG);
    int b_next = wz_add_button(470, 500, 150, 46, OCSE_W_BTN_NEXT, NULL,
                               WZA_NEXT);
    wz_add_button(630, 500, 140, 46, OCSE_W_BTN_CANCEL, NULL, WZA_CANCEL);
    wz_focus_set(b_next);
    wz_set_default(b_next);
    for (;;) {
        int act = page_pump();
        if (act == WZA_LANG) {
            ocse_i18n_set(ocse_i18n_lang() == OCSE_LANG_EN
                          ? OCSE_LANG_ZH : OCSE_LANG_EN);
            wz_repaint();
            continue;
        }
        if (act == WZA_NEXT) return WZ_PG_DISK;
        if (act == WZA_CANCEL) return WZ_PG_CANCEL;
    }
}

/* ==================================================================
 * step 2: target disk (lsdisk -> list; existing-system refusal)
 * ================================================================== */
static int wz_page_disk(void) {
    wz_reset();
    page_frame(ocse_tr(OCSE_W_STEP_DISK), 2);
    wz_add_label(24, 96, OCSE_W_DISK_HEAD, NULL);
    int lst = wz_add_list(24, 126, 700, 220);
    wz_add_label(24, 366, OCSE_W_DISK_NOOVER, NULL);
    int b_next = wz_add_button(470, 500, 150, 46, OCSE_W_BTN_NEXT, NULL,
                               WZA_NEXT);
    wz_add_button(630, 500, 140, 46, OCSE_W_BTN_CANCEL, NULL, WZA_CANCEL);
    wz_focus_set(lst);
    wz_set_default(b_next);   /* keyboard users: Enter advances once a
                                 disk row exists (WZA_NEXT re-checks) */

    for (;;) {
        wz_list_clear(lst);
        wz_have_disk = 0;
        if (wz_runq("lsdisk") == 0) {
            char rows2[8][WZ_ROW_LEN];
            int n = parse_lsdisk(wz_out, rows2, 8);
            for (int i = 0; i < n; i++) wz_list_add(lst, rows2[i]);
            if (n > 0) {
                wz_list_set_sel(lst, 0);
                wz_have_disk = 1;
            }
        }
        wz_repaint();

        int act = page_pump();
        if (act == WZA_RESCAN) continue;
        if (act == WZA_CANCEL) return WZ_PG_CANCEL;
        if (act == WZA_NEXT) {
            if (!wz_have_disk || wz_list_count(lst) == 0) continue;
            const char* row = wz_list_row(lst, wz_list_sel(lst));
            const char* q = row;
            char t[40];
            while (*q == ' ') q++;
            (void)tok(&q, t, sizeof(t));
            strncpy(wz_disk, t, sizeof(wz_disk) - 1);
            wz_disk[sizeof(wz_disk) - 1] = 0;

            /* no-overwrite gate: mount every FAT32 partition, look
             * for /boot/opencube.elf, umount (real commands). The
             * lspart capture lines carry no device names ("  #1 FAT32
             * start=..."), so the gate walks the partition COUNT and
             * derives <disk>p<no> itself. */
            int existing = 0;
            char cmd[WZ_CMD_SZ];
            if (wz_runq("lsdisk") == 0) { }         /* warm the cache */
            wz_cat(cmd, sizeof(cmd), "lspart ", wz_disk, NULL, NULL);
            int have = (wz_runq(cmd) == 0);
            char partline[WZ_ROW_LEN];
            partline[0] = 0;
            if (have) strncpy(partline, wz_out, sizeof(partline) - 1);
            int nparts = have ? count_parts(partline) : 0;
            for (int pn = 1; pn <= 4 && pn <= nparts && !existing; pn++) {
                char dev[24];
                strcpy(dev, wz_disk);
                strcat(dev, "p");
                char num[4];
                u64_to_str((u64)pn, num);
                strcat(dev, num);
                wz_cat(cmd, sizeof(cmd), "mount ", dev, " /mnt", NULL);
                if (wz_runq(cmd) != 0) continue;
                if (wz_runq("ls /mnt/boot") == 0 &&
                    wz_strstr(wz_out, "opencube.elf"))
                    existing = 1;
                (void)wz_runq("umount /mnt");
            }
            if (existing) {
                strcpy(wz_line1, "OpenCubeOS is already installed on ");
                strcat(wz_line1, wz_disk);
                strncpy(wz_errcmd, wz_line1, sizeof(wz_errcmd) - 1);
                wz_errcmd[sizeof(wz_errcmd) - 1] = 0;
                strcpy(wz_errout,
                       "The installer never overwrites an existing"
                       " system. Remove it by hand (rmpart) or choose"
                       " another disk.");
                wz_failed_stage = WZ_ST_NONE;
                wz_err_page = WZ_PG_DISK;
                return WZ_PG_ERROR;
            }
            wz_have_part = 0;
            wz_fmt_ok = 0;
            return WZ_PG_PART;
        }
    }
}

/* ==================================================================
 * step 3: partitioning (lspart -> largest gap -> mkpart)
 * ================================================================== */
static int wz_page_part(void) {
    wz_reset();
    page_frame(ocse_tr(OCSE_W_STEP_PART), 3);
    wz_add_label(24, 96, OCSE_W_PART_HEAD, NULL);
    int lst = wz_add_list(24, 126, 700, 180);
    int lb_gap = wz_add_label(24, 330, 0, "");
    wz_add_label(24, 360, OCSE_W_PART_AUTO, NULL);
    int b_create = wz_add_button(24, 500, 220, 46, OCSE_W_BTN_CREATE,
                                 NULL, WZA_CREATE);
    wz_add_button(300, 500, 150, 46, OCSE_W_BTN_BACK, NULL, WZA_BACK);
    int b_next = wz_add_button(470, 500, 150, 46, OCSE_W_BTN_NEXT, NULL,
                               WZA_NEXT);
    wz_add_button(630, 500, 140, 46, OCSE_W_BTN_CANCEL, NULL, WZA_CANCEL);
    wz_focus_set(lst);

    for (;;) {
        int mb = 0;
        if (wz_runq("lsdisk") == 0)
            mb = parse_lsdisk_mb(wz_out);
        char cmd[WZ_CMD_SZ];
        wz_cat(cmd, sizeof(cmd), "lspart ", wz_disk, NULL, NULL);
        int have = (wz_runq(cmd) == 0);

        /* partition list */
        wz_list_clear(lst);
        if (have) {
            const char* p = wz_out;
            char line[WZ_ROW_LEN];
            while (*p) {
                int l = 0;
                while (*p && *p != '\n' && l < WZ_ROW_LEN - 1)
                    line[l++] = *p++;
                line[l] = 0;
                if (*p == '\n') p++;
                if (strncmp(line, "  #", 3) == 0) {
                    const char* q = line;
                    while (*q == ' ') q++;
                    wz_list_add(lst, q);
                }
            }
        }

        /* gap analysis + gap label */
        int best = have
            ? parse_lspart_gap(wz_out, mb, &wz_gap_lo, &wz_gap_hi) : 0;
        if (wz_have_part) {
            strcpy(wz_line3, ocse_tr(OCSE_W_PART_MADE));
            strcat(wz_line3, wz_dev);
            wz_set_raw(lb_gap, wz_line3);
        } else if (best > 0) {
            strcpy(wz_line3, ocse_tr(OCSE_W_PART_GAP));
            char n[16];
            u64_to_str((u64)best, n);
            strcat(wz_line3, n);
            strcat(wz_line3, " MiB");
            wz_set_raw(lb_gap, wz_line3);
        } else {
            wz_set_raw(lb_gap, ocse_tr(OCSE_W_PART_NOGAP));
        }
        wz_set_default(wz_have_part ? b_next
                       : (best > 0 ? b_create : b_next));
        wz_repaint();

        int act = page_pump();
        if (act == WZA_BACK) return WZ_PG_DISK;
        if (act == WZA_CANCEL) return WZ_PG_CANCEL;
        if (act == WZA_CREATE) {
            if (best <= 0) continue;
            char lo[16], hi[16];
            u64_to_str((u64)wz_gap_lo, lo);
            u64_to_str((u64)wz_gap_hi, hi);
            wz_cat(cmd, sizeof(cmd), "mkpart ", wz_disk, " ", lo);
            strcat(cmd, " ");
            strcat(cmd, hi);
            if (wz_run(cmd, WZ_ST_NONE, WZ_PG_PART) != 0)
                return WZ_PG_ERROR;
            /* re-read: the new partition is the last one */
            wz_cat(cmd, sizeof(cmd), "lspart ", wz_disk, NULL, NULL);
            if (wz_runq(cmd) == 0) {
                int n = count_parts(wz_out);
                wz_partno = n;
                strcpy(wz_dev, wz_disk);
                strcat(wz_dev, "p");
                char num[4];
                u64_to_str((u64)wz_partno, num);
                strcat(wz_dev, num);
                wz_have_part = 1;
            }
            continue;
        }
        if (act == WZA_NEXT) {
            if (wz_have_part) return WZ_PG_FMT;
            /* reuse: exactly one existing partition on the disk (the
             * no-overwrite gate already ran on the disk page). */
            if (wz_list_count(lst) == 1) {
                wz_partno = 1;
                strcpy(wz_dev, wz_disk);
                strcat(wz_dev, "p1");
                return WZ_PG_FMT;
            }
            continue;
        }
    }
}

/* ==================================================================
 * step 4: formatting (mkfs.fat32 confirm; the OCBL FAT contract)
 * ================================================================== */
static int wz_page_fmt(void) {
    wz_reset();
    page_frame(ocse_tr(OCSE_W_STEP_FMT), 4);
    strcpy(wz_line1, ocse_tr(OCSE_W_FMT_HEAD));
    strcat(wz_line1, wz_dev);
    wz_add_label(24, 96, 0, wz_line1);
    wz_add_label(24, 136, OCSE_W_FMT_ONLY, NULL);
    wz_add_label(24, 200, OCSE_W_FMT_WARN, NULL);
    int b_fmt = wz_add_button(24, 500, 220, 46, OCSE_W_BTN_FORMAT, NULL,
                              WZA_FORMAT);
    wz_add_button(300, 500, 150, 46, OCSE_W_BTN_BACK, NULL, WZA_BACK);
    int b_next = wz_add_button(470, 500, 150, 46, OCSE_W_BTN_NEXT, NULL,
                               WZA_NEXT);
    wz_add_button(630, 500, 140, 46, OCSE_W_BTN_CANCEL, NULL, WZA_CANCEL);
    wz_focus_set(b_fmt);
    wz_set_default(wz_fmt_ok ? b_next : b_fmt);

    for (;;) {
        int act = page_pump();
        if (act == WZA_BACK) return WZ_PG_PART;
        if (act == WZA_CANCEL) return WZ_PG_CANCEL;
        if (act == WZA_FORMAT) {
            char cmd[WZ_CMD_SZ];
            wz_cat(cmd, sizeof(cmd), "mkfs.fat32 ", wz_dev, " confirm",
                   NULL);
            if (wz_run(cmd, WZ_ST_NONE, WZ_PG_FMT) != 0)
                return WZ_PG_ERROR;
            wz_fmt_ok = 1;
            wz_written = 1;              /* data-destroying happened */
            wz_set_default(b_next);
            wz_set_raw(b_fmt, ocse_tr(OCSE_W_FMT_DONE));
            continue;
        }
        if (act == WZA_NEXT) {
            if (!wz_fmt_ok) continue;
            return WZ_PG_USER;
        }
    }
}

/* ==================================================================
 * step 5: user account (SHA-256 password)
 * ================================================================== */
static int valid_login(const char* s) {
    int l = (int)strlen(s);
    if (l < 1 || l > 31) return 0;
    for (int i = 0; i < l; i++) {
        char c = s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.'))
            return 0;
    }
    return 1;
}

static int wz_page_user(void) {
    wz_reset();
    page_frame(ocse_tr(OCSE_W_STEP_USER), 5);
    wz_add_label(24, 96, OCSE_W_USER_HEAD, NULL);
    int in_name = wz_add_input(24, 140, 320, OCSE_W_USER_NAME, NULL);
    int in_full = wz_add_input(24, 210, 320, OCSE_W_USER_FULL, NULL);
    int in_pass = wz_add_input(24, 280, 320, OCSE_W_USER_PASS, NULL);
    wz_input_mask(in_pass, 1);
    wz_input_set(in_name, wz_user);
    wz_input_set(in_full, wz_full);
    wz_input_set(in_pass, "");
    wz_add_label(24, 340, OCSE_W_USER_NOTE, NULL);
    wz_add_button(470, 500, 150, 46, OCSE_W_BTN_BACK, NULL, WZA_BACK);
    int b_next = wz_add_button(630, 500, 140, 46, OCSE_W_BTN_NEXT, NULL,
                               WZA_NEXT);
    wz_focus_set(in_name);
    wz_set_default(b_next);

    for (;;) {
        int act = page_pump();
        if (act == WZA_BACK) return WZ_PG_FMT;
        if (act == WZA_CANCEL) return WZ_PG_CANCEL;
        if (act == WZA_NEXT) {
            const char* nm = wz_input_text(in_name);
            if (!valid_login(nm)) {
                strcpy(wz_errcmd, ocse_tr(OCSE_W_USER_HEAD));
                strcpy(wz_errout, ocse_tr(OCSE_W_USER_SHORT));
                wz_failed_stage = WZ_ST_NONE;
                wz_err_page = WZ_PG_USER;
                return WZ_PG_ERROR;
            }
            strncpy(wz_user, nm, sizeof(wz_user) - 1);
            wz_user[sizeof(wz_user) - 1] = 0;
            strncpy(wz_full, wz_input_text(in_full),
                    sizeof(wz_full) - 1);
            wz_full[sizeof(wz_full) - 1] = 0;
            const char* pw = wz_input_text(in_pass);
            u8 hash[32];
            ocse_sha256((const u8*)pw, (int)strlen(pw), hash);
            ocse_sha256_hex(hash, wz_pass_sha);
            wz_input_set(in_pass, "");   /* plain text never persists */
            return WZ_PG_SYS;
        }
    }
}

/* ==================================================================
 * step 6: system configuration
 * ================================================================== */
static int wz_page_sys(void) {
    wz_reset();
    page_frame(ocse_tr(OCSE_W_STEP_SYS), 6);
    wz_add_label(24, 96, OCSE_W_SYS_HEAD, NULL);
    int in_host = wz_add_input(24, 140, 320, OCSE_W_SYS_HOST, NULL);
    int in_upd  = wz_add_input(24, 210, 480, OCSE_W_SYS_UPD, NULL);
    int in_auto = wz_add_input(24, 280, 160, OCSE_W_SYS_AUTO, NULL);
    wz_input_set(in_host, wz_host);
    wz_input_set(in_upd, wz_upd);
    wz_input_set(in_auto, wz_auto);
    wz_add_button(470, 500, 150, 46, OCSE_W_BTN_BACK, NULL, WZA_BACK);
    int b_next = wz_add_button(630, 500, 140, 46, OCSE_W_BTN_NEXT, NULL,
                               WZA_NEXT);
    wz_focus_set(in_host);
    wz_set_default(b_next);

    for (;;) {
        int act = page_pump();
        if (act == WZA_BACK) return WZ_PG_USER;
        if (act == WZA_CANCEL) return WZ_PG_CANCEL;
        if (act == WZA_NEXT) {
            const char* h = wz_input_text(in_host);
            if (!valid_login(h)) {
                strcpy(wz_errcmd, ocse_tr(OCSE_W_SYS_HEAD));
                strcpy(wz_errout, ocse_tr(OCSE_W_USER_SHORT));
                wz_failed_stage = WZ_ST_NONE;
                wz_err_page = WZ_PG_SYS;
                return WZ_PG_ERROR;
            }
            strncpy(wz_host, h, sizeof(wz_host) - 1);
            wz_host[sizeof(wz_host) - 1] = 0;
            const char* u = wz_input_text(in_upd);
            strncpy(wz_upd, u, sizeof(wz_upd) - 1);
            wz_upd[sizeof(wz_upd) - 1] = 0;
            const char* a = wz_input_text(in_auto);
            if (strcasecmp(a, "no") == 0) strcpy(wz_auto, "no");
            else strcpy(wz_auto, "yes");
            return WZ_PG_CONFIRM;
        }
    }
}

/* ==================================================================
 * step 7: review
 * ================================================================== */
static int wz_page_confirm(void) {
    wz_reset();
    page_frame(ocse_tr(OCSE_W_STEP_CONFIRM), 7);
    strcpy(wz_line1, ocse_tr(OCSE_W_CONF_DISK));
    strcat(wz_line1, wz_disk);
    strcpy(wz_line2, ocse_tr(OCSE_W_CONF_PART));
    strcat(wz_line2, wz_dev);
    strcpy(wz_line3, ocse_tr(OCSE_W_CONF_USER));
    strcat(wz_line3, wz_user);
    strcpy(wz_line4, ocse_tr(OCSE_W_CONF_HOST));
    strcat(wz_line4, wz_host);
    strcpy(wz_line5, "update_url: ");
    strcat(wz_line5, wz_upd[0] ? wz_upd : "(default)");
    strcpy(wz_line6, "auto_check: ");
    strcat(wz_line6, wz_auto);
    wz_add_label(40, 110, 0, wz_line1);
    wz_add_label(40, 140, 0, wz_line2);
    wz_add_label(40, 170, 0, wz_line3);
    wz_add_label(40, 200, 0, wz_line4);
    wz_add_label(40, 230, 0, wz_line5);
    wz_add_label(40, 260, 0, wz_line6);
    wz_add_label(40, 310, OCSE_W_CONF_GO, NULL);
    wz_add_button(24, 500, 150, 46, OCSE_W_BTN_BACK, NULL, WZA_BACK);
    wz_add_button(24, 440, 150, 46, OCSE_W_BTN_CANCEL, NULL, WZA_CANCEL);
    int b_start = wz_add_button(470, 500, 300, 46, OCSE_W_BTN_START,
                                NULL, WZA_START);
    wz_focus_set(b_start);
    wz_set_default(b_start);
    for (;;) {
        int act = page_pump();
        if (act == WZA_BACK) return WZ_PG_SYS;
        if (act == WZA_CANCEL) return WZ_PG_CANCEL;
        if (act == WZA_START) return WZ_PG_INSTALL;
    }
}

/* ==================================================================
 * step 8: install (the real command sequence, stage by stage)
 * ================================================================== */
static int wz_page_install(void) {
    wz_reset();
    page_frame(ocse_tr(OCSE_W_STEP_INSTALL), 8);
    int lb = wz_add_label(24, 110, 0, wz_line1);
    int pg = wz_add_progress(24, 150, 700, 30);
    wz_repaint();

    char num[4];
    u64_to_str((u64)wz_partno, num);
    wz_failed_stage = WZ_ST_NONE;

    /* stage 1: mount the target (install requires a mounted target).
     * A retry after a failed umount may find it still mounted - the
     * mount refusal then names it; drop the stale mount and retry
     * once before giving up. */
    wz_set_progress(pg, 10);
    strcpy(wz_line1, "mount ");
    strcat(wz_line1, wz_dev);
    strcat(wz_line1, " /mnt");
    wz_set_raw(lb, wz_line1);
    wz_repaint();
    if (wz_run(wz_line1, WZ_ST_MOUNT, WZ_PG_INSTALL) != 0) {
        (void)wz_runq("umount /mnt");
        if (wz_run(wz_line1, WZ_ST_MOUNT, WZ_PG_INSTALL) != 0)
            return WZ_PG_ERROR;
    }

    /* stage 2: install - the full b5 state machine (medium check,
     * FAT gate, extract, boot code, boot flag) */
    wz_set_progress(pg, 25);
    strcpy(wz_line1, "install ");
    strcat(wz_line1, wz_disk);
    strcat(wz_line1, " ");
    strcat(wz_line1, num);
    strcat(wz_line1, " confirm");
    wz_set_raw(lb, wz_line1);
    wz_repaint();
    if (wz_run(wz_line1, WZ_ST_INSTALL, WZ_PG_INSTALL) != 0)
        return WZ_PG_ERROR;
    wz_written = 2;

    /* stage 3: config (four keys) */
    wz_set_progress(pg, 65);
    strcpy(wz_line1, "config set hostname ");
    strcat(wz_line1, wz_host);
    wz_set_raw(lb, wz_line1);
    wz_repaint();
    if (wz_run(wz_line1, WZ_ST_CONFIG, WZ_PG_INSTALL) != 0)
        return WZ_PG_ERROR;
    strcpy(wz_line1, "config set lang ");
    strcat(wz_line1, ocse_i18n_lang() == OCSE_LANG_ZH ? "zh" : "en");
    if (wz_run(wz_line1, WZ_ST_CONFIG, WZ_PG_INSTALL) != 0)
        return WZ_PG_ERROR;
    if (wz_upd[0]) {
        strcpy(wz_line1, "config set update_url ");
        strcat(wz_line1, wz_upd);
        if (wz_run(wz_line1, WZ_ST_CONFIG, WZ_PG_INSTALL) != 0)
            return WZ_PG_ERROR;
    }
    strcpy(wz_line1, "config set auto_check ");
    strcat(wz_line1, wz_auto);
    if (wz_run(wz_line1, WZ_ST_CONFIG, WZ_PG_INSTALL) != 0)
        return WZ_PG_ERROR;

    /* stage 4: the user record (name:full:sha256 - never plain) */
    wz_set_progress(pg, 80);
    strcpy(wz_line1, "write /mnt/etc/users ");
    strcat(wz_line1, wz_user);
    strcat(wz_line1, ":");
    strcat(wz_line1, wz_full);
    strcat(wz_line1, ":");
    strcat(wz_line1, wz_pass_sha);
    wz_set_raw(lb, wz_line1);
    wz_repaint();
    if (wz_run(wz_line1, WZ_ST_USER, WZ_PG_INSTALL) != 0)
        return WZ_PG_ERROR;

    /* stage 5: sync */
    wz_set_progress(pg, 92);
    strcpy(wz_line1, "sync");
    wz_set_raw(lb, wz_line1);
    wz_repaint();
    if (wz_run(wz_line1, WZ_ST_SYNC, WZ_PG_INSTALL) != 0)
        return WZ_PG_ERROR;

    /* stage 6: umount */
    wz_set_progress(pg, 100);
    strcpy(wz_line1, "umount /mnt");
    wz_set_raw(lb, wz_line1);
    wz_repaint();
    if (wz_run(wz_line1, WZ_ST_UMOUNT, WZ_PG_INSTALL) != 0)
        return WZ_PG_ERROR;

    return WZ_PG_DONE;
}

/* ==================================================================
 * step 9: done
 * ================================================================== */
static int wz_page_done(void) {
    wz_reset();
    page_frame(ocse_tr(OCSE_W_STEP_DONE), 9);
    wz_add_label(24, 130, OCSE_W_INST_DONE, NULL);
    wz_add_label(24, 170, OCSE_W_INST_REMOVE, NULL);
    wz_add_button(470, 500, 150, 46, OCSE_W_BTN_FINISH, NULL,
                  WZA_FINISH);
    int b_reb = wz_add_button(630, 500, 140, 46, OCSE_W_BTN_REBOOT,
                              NULL, WZA_REBOOT);
    wz_focus_set(b_reb);
    wz_set_default(b_reb);
    for (;;) {
        int act = page_pump();
        if (act == WZA_REBOOT) {
            char out[256];
            (void)ocse_cl_exec("reboot", out, sizeof(out));
            for (;;) core_sched_yield();    /* never returns */
        }
        if (act == WZA_FINISH) return WZ_PG_EXIT;
        if (act == WZA_CANCEL) return WZ_PG_EXIT;
    }
}

/* ==================================================================
 * error page (failed command + captured output + Retry/Back/Cancel)
 * ================================================================== */
static int wz_page_error(void) {
    wz_reset();
    page_frame(ocse_tr(OCSE_W_ERR_HEAD), 0);
    strcpy(wz_line1, ocse_tr(OCSE_W_ERR_CMD));
    strcat(wz_line1, wz_errcmd);
    wz_add_label(24, 100, 0, wz_line1);
    wz_add_label(24, 130, OCSE_W_ERR_OUT, NULL);
    int y = 160;
    const char* p = wz_errout;
    char row[76];
    while (*p && y < 430) {
        int l = 0;
        while (*p && *p != '\n' && l < 74) row[l++] = *p++;
        row[l] = 0;
        if (*p == '\n') p++;
        int ri = (y - 160) / 30;
        if (ri < 8) {
            strncpy(wz_rows[ri], row, 75);
            wz_rows[ri][75] = 0;
            wz_add_label(24, y, 0, wz_rows[ri]);
        }
        y += 30;
    }
    int b_retry = wz_add_button(24, 500, 150, 46, OCSE_W_BTN_RETRY,
                                NULL, WZA_RETRY);
    wz_add_button(300, 500, 150, 46, OCSE_W_BTN_BACK, NULL, WZA_BACK);
    wz_add_button(630, 500, 140, 46, OCSE_W_BTN_CANCEL, NULL, WZA_CANCEL);
    wz_focus_set(b_retry);
    wz_set_default(b_retry);
    for (;;) {
        int act = page_pump();
        if (act == WZA_RETRY) {
            if (wz_failed_stage == WZ_ST_NONE)
                return wz_err_page;        /* validation error */
            return WZ_PG_INSTALL;          /* re-run the stages */
        }
        if (act == WZA_BACK) return wz_err_page;
        if (act == WZA_CANCEL) return WZ_PG_CANCEL;
    }
}

/* ==================================================================
 * cancel overlay
 * ================================================================== */
static int wz_page_cancel(void) {
    wz_reset();
    wz_add_label(24, 130, OCSE_W_CANCEL_Q, NULL);
    wz_add_label(24, 170, wz_written >= 2 ? OCSE_W_CANCEL_WROT
                                          : OCSE_W_CANCEL_CLEAN, NULL);
    wz_add_button(300, 500, 150, 46, OCSE_W_BTN_CANCEL, NULL, WZA_NO);
    int b_yes = wz_add_button(470, 500, 150, 46, OCSE_W_YES, NULL,
                              WZA_YES);
    wz_focus_set(b_yes);
    wz_set_default(b_yes);
    for (;;) {
        int act = page_pump();
        if (act == WZA_YES) return WZ_PG_EXIT;
        if (act == WZA_NO) return WZ_PG_LANG;
    }
}

/* ==================================================================
 * the wizard loop
 * ================================================================== */
void ocse_wizard_init(void) {
    wz_disk[0] = 0;
    wz_partno = 0;
    wz_dev[0] = 0;
    wz_user[0] = 0;
    wz_full[0] = 0;
    wz_pass_sha[0] = 0;
    strcpy(wz_host, "opencube");
    wz_upd[0] = 0;
    strcpy(wz_auto, "yes");
    wz_gap[0] = 0;
    wz_have_disk = 0;
    wz_have_part = 0;
    wz_fmt_ok = 0;
    wz_written = 0;
    wz_failed_stage = WZ_ST_NONE;
    wz_err_page = WZ_PG_DISK;
}

void ocse_wizard_run(void) {
    ocse_wizard_init();
    int pg = WZ_PG_LANG;
    while (pg != WZ_PG_EXIT && !g_ocse_hotkey_req) {
        switch (pg) {
        case WZ_PG_LANG:    pg = wz_page_lang();    break;
        case WZ_PG_DISK:    pg = wz_page_disk();    break;
        case WZ_PG_PART:    pg = wz_page_part();    break;
        case WZ_PG_FMT:     pg = wz_page_fmt();     break;
        case WZ_PG_USER:    pg = wz_page_user();    break;
        case WZ_PG_SYS:     pg = wz_page_sys();     break;
        case WZ_PG_CONFIRM: pg = wz_page_confirm(); break;
        case WZ_PG_INSTALL: pg = wz_page_install(); break;
        case WZ_PG_DONE:    pg = wz_page_done();    break;
        case WZ_PG_ERROR:   pg = wz_page_error();   break;
        case WZ_PG_CANCEL:  pg = wz_page_cancel();  break;
        default:            pg = WZ_PG_EXIT;        break;
        }
    }
    wz_reset();
}
