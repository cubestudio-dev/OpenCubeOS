/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP-runtime-hardening-and-experience-rebase
 * - L1 app "serial-probe" (spec 4.5): the serial-encoding wizard.
 *
 * Which of the two sample lines reads correctly on the user's serial
 * TERMINAL is the one thing the machine cannot see from inside - so
 * this wizard asks.  The byte-exact half lives in the L0 display
 * service (L1SVC_DISP_SER_PROBE, services/display serial_enc.c):
 *
 *   emit    - two labeled samples RAW on COM1 (bypassing the tee
 *             hook: sample 1 is UTF-8 bytes, sample 2 the same text
 *             transcoded to GBK) + the VGA labels; the op's captured
 *             console text (labels + the "enter 1 or 2" ask line,
 *             all rendered through the kernel get_text resource)
 *             comes back in `out` and is printed here
 *   set 1|2  - set the serial encoding AND persist it
 *             (serial.encoding=utf8|gbk); the captured text carries
 *             the "configuration saved" line (+ which encoding)
 *   (empty)  - status
 *
 * Every human line this app prints by ITSELF goes through
 * get_text(MSG_*) - the 4.4 system message resource - so `lang zh`
 * flips the whole wizard live.  The single tr_() consumer is the
 * shared wizard failure line (wizard_lib.h wiz_svc -> tr_()). */
#include "l1_lib.h"
#include "l1svc_ops.h"
#include "wizard_lib.h"

int app_main(int argc, char **argv)
{
    (void)argc; (void)argv;
    static char out[WIZ_OUT_CAP];
    static char ans[16];

    /* 1. the banner: what this wizard wants from the user */
    puts_(get_text(MSG_SER_ENC_PROBE_HEAD));
    putc_('\n');

    /* 2. the samples.  COM1 receives both lines raw (the encoding
     * under test must never be re-transcoded by the tee); the VGA
     * console receives the two get_text labels + the readable UTF-8
     * sample - wiz_svc prints the op's captured console text, which
     * ends with the "enter 1 (UTF-8) or 2 (GBK)" ask line, so no
     * second ask is printed here. */
    if (wiz_svc(L1SVC_CAT_DISPLAY, L1SVC_DISP_SER_PROBE, "emit", out) != 0)
        return 1;

    /* 3. the answer: one line through the kernel line editor
     * (SYS_READLINE: echo + backspace; empty prompt - the ask line
     * above already carries the question). */
    wiz_ask("", ans, (int)sizeof(ans));

    /* 4. apply (or cancel).  Only the bare words count - anything
     * else (empty, q, a typo) cancels with no change. */
    if (streq_(ans, "1") || streq_(ans, "2")) {
        char op[8];
        op[0] = 's'; op[1] = 'e'; op[2] = 't'; op[3] = ' ';
        op[4] = ans[0]; op[5] = 0;
        /* the captured set text IS the closing line:
         * get_text(MSG_SER_ENC_PROBE_SAVED) + " (utf8|gbk)" */
        if (wiz_svc(L1SVC_CAT_DISPLAY, L1SVC_DISP_SER_PROBE, op, out) != 0)
            return 1;
    } else {
        puts_(get_text(MSG_SH_CANCELLED));
        putc_('\n');
    }
    return 0;
}
