/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP06g2 - L1 app "vi".
 *
 * The vi face over the shared editor engine: a REAL modal layer on
 * top of _shared/editor_core.h (the WP06g2 UTF-8/IME engine).
 *
 * COMMAND mode (default): h/j/k/l or arrows move (glyph-wise for
 *   h/l), 0/$ line start/end, x deletes the glyph at the cursor, dd
 *   cuts the line, yy copies it, p pastes below, i/a/o enter INSERT
 *   mode (at the cursor / after it / on a new line below), : opens
 *   the ex line (:w save, :q quit, :wq both, :q! discard).
 * INSERT mode: typing inserts (UTF-8 glyph-aware), Backspace/Delete
 *   remove whole glyphs, F2 toggles the SYSTEM IME (pinyin, 1-9 pick,
 *   space first, PgUp/PgDn paging - the same engine nano uses), Esc
 *   returns to COMMAND mode.
 * The edit primitives (motion, glyph deletion, multi-byte insert,
 * cut/copy/paste, save/load, the IME bridge) all come from the
 * shared engine - this file is ONLY the modal loop. */
#include "editor_core.h"

#define VI_MODE_CMD 0
#define VI_MODE_INS 1

static int vi_mode = VI_MODE_CMD;

static void vi_status(const char *file) {
    /* WP06g3-4.7: the vi modal bar is the same differential pinned
     * bottom row as nano's - mode, position, IME state and the preedit
     * strip, no key legend, ZERO output when unchanged. */
    char bar[192];
    int n = 0;
    /* #40 (WP-fix-p2-p3-cleanup): ONE bounded-append guard for every
     * write.  The old mixed limits (-32 file, -24 numbers, -1 tag)
     * let the tag loop reach bar[191], the closing ']' push n to 192,
     * and the final bar[n]=0 write ONE BYTE PAST the array; long file
     * names (>120 B, reachable via the path argument) also starved
     * the -24-guarded line-number loops into silently dropping the
     * total ("12/").  Same shape as nano's unano_statusbar guards. */
#define VI_BAR_PUT(c) do { if (n < (int)sizeof(bar) - 1) bar[n++] = (c); } while (0)
    const char *tag = tr_(vi_mode == VI_MODE_INS ? "vi_mode_ins" : "vi_mode_cmd");
    VI_BAR_PUT('[');
    if (unano_dirty) VI_BAR_PUT('*');
    VI_BAR_PUT(' ');
    for (int i = 0; file[i] && n < (int)sizeof(bar) - 1; i++) VI_BAR_PUT(file[i]);
    VI_BAR_PUT(' ');
    {
        char num[12]; int m = 0; int v = unano_cl + 1;
        if (v == 0) num[m++] = '0';
        while (v > 0 && m < 11) { num[m++] = (char)('0' + v % 10); v /= 10; }
        for (int i = 0; i < m && n < (int)sizeof(bar) - 1; i++) VI_BAR_PUT(num[m - 1 - i]);
        VI_BAR_PUT('/');
        m = 0; v = unano_nlines;
        if (v == 0) num[m++] = '0';
        while (v > 0 && m < 11) { num[m++] = (char)('0' + v % 10); v /= 10; }
        for (int i = 0; i < m && n < (int)sizeof(bar) - 1; i++) VI_BAR_PUT(num[m - 1 - i]);
    }
    VI_BAR_PUT(' ');
    for (int i = 0; tag[i] && n < (int)sizeof(bar) - 1; i++) VI_BAR_PUT(tag[i]);
    VI_BAR_PUT(']');
    if (unano_ime_err) {
        const char *err = tr_("editor_ime_err");
        for (int i = 0; err[i] && n < (int)sizeof(bar) - 1; i++) VI_BAR_PUT(err[i]);
    }
    /* n <= sizeof(bar)-1 is now invariant: the NUL can never overflow. */
    bar[n] = 0;
#undef VI_BAR_PUT
    if (unano_ime_composing && unano_ime_strip[0]) {
        int room = (int)sizeof(bar) - 2 - n;
        if (room > 2) {
            bar[n++] = ' ';
            bar[n++] = ' ';
            for (int i = 0; unano_ime_strip[i] && i < room; i++)
                bar[n++] = unano_ime_strip[i];
            bar[n] = 0;
        }
    }
    unano_bar_draw(bar);
}

static void vi_redraw(void) {
    unano_redraw_line();
}

static void vi_print_all(const char *file) {
    /* WP06g3-4.7: the addressed layout (legend row 1, viewport, bar) -
     * vi rides the same rewrite. */
    unano_ansi("\033[2J");
    unano_goto(1, 1);
    puts_("-- vi: ");
    puts_(file);
    puts_(" --");
    puts_(tr_("vi_helpbar"));
    puts_(" --");
    unano_view_ensure();
    unano_view_redraw();
    unano_bar_invalidate();
    vi_status(file);
    vi_redraw();
}

/* the ex line (:w :q :wq :q!) - returns 1 when the editor quits */
static int vi_ex_line(const char *file) {
    char cmd[16];
    int n = 0;
    puts_(":");
    for (;;) {
        int k = unano_getch_blocking();
        if (k == '\n' || k == '\r') break;
        if (k == 0x1B || k == 0x9C) { putc_('\n'); return 0; }
        if ((k == 0x08 || k == 0x7F) && n > 0) {
            n--;
            putc_('\b'); putc_(' '); putc_('\b');
            continue;
        }
        if (k >= 0x20 && k < 0x7F && n < (int)sizeof(cmd) - 1) {
            cmd[n++] = (char)k;
            putc_((char)k);
        }
    }
    cmd[n] = 0;
    putc_('\n');
    int want_w = 0, want_q = 0, bang = 0;
    for (int i = 0; cmd[i]; i++) {
        if (cmd[i] == 'w') want_w = 1;
        else if (cmd[i] == 'q') want_q = 1;
        else if (cmd[i] == '!') bang = 1;
    }
    if (want_w) {
        if (unano_save(file) == 0) {
            puts_(tr_("editor_wrote_head"));
            putu_((u64)unano_nlines);
            puts_(tr_("editor_wrote_tail"));
        } else {
            puts_(tr_("editor_save_failed"));
            puts_("\n");
        }
    }
    if (want_q) {
        if (unano_dirty && !want_w && !bang) {
            puts_(tr_("vi_modified"));
            puts_("\n");
            return 0;
        }
        unano_ime_off();
        return 1;                     /* quit */
    }
    if (!want_w && !want_q && n > 0) {
        puts_(tr_("vi_unknown_ex"));
        puts_("\n");
    }
    return 0;
}

static void vi_insert_mode(const char *file) {
    vi_mode = VI_MODE_INS;
    vi_status(file);
    vi_redraw();
    for (;;) {
        int k = unano_getch_blocking();

        if (k == 0x1B || k == 0x9C) {         /* Esc: back to CMD */
            unano_ime_composing = 0;
            unano_ime_strip[0] = 0;
            vi_mode = VI_MODE_CMD;
            vi_status(file);
            vi_redraw();
            return;
        }
        if (k == 0x91) {                       /* F2: IME toggle */
            unano_ime_toggle();
            vi_status(file);
            vi_redraw();
            continue;
        }
        /* WP06g3-4.6: preedit routing with NO passthrough - a failed
         * service call shows the bar notice and swallows the key. */
        if (unano_ime_on &&
            (unano_ime_composing || (k >= 'a' && k <= 'z'))) {
            int act = unano_ime_feed(k);
            if (act == 4) {
                unano_ime_err = 1;
                vi_status(file);
                continue;
            }
            if (act == 1) {
                vi_status(file);      /* preedit on the modal bar */
                continue;
            }
            if (act == 2) {
                vi_redraw();
                vi_status(file);
                continue;
            }
            if (act == 3) {
                vi_redraw();
                vi_status(file);
                if (unano_ime_composing) continue;
                /* fall through: the editor handles the key too */
            }
        }

        if (k == 0x80) {                       /* Up */
            if (unano_cl > 0) {
                unano_cl--;
                int ll = (int)strlen_(unano_lines[unano_cl]);
                if (unano_cc > ll) unano_cc = ll;
                vi_redraw();
            }
        } else if (k == 0x81) {                /* Down */
            if (unano_cl + 1 < unano_nlines) {
                unano_cl++;
                int ll = (int)strlen_(unano_lines[unano_cl]);
                if (unano_cc > ll) unano_cc = ll;
                vi_redraw();
            }
        } else if (k == 0x82) {                /* Left: glyph */
            if (unano_cc > 0) {
                unano_cc = u8_prev(unano_lines[unano_cl], unano_cc);
                vi_redraw();
            }
        } else if (k == 0x83) {                /* Right: glyph */
            int ll = (int)strlen_(unano_lines[unano_cl]);
            if (unano_cc < ll) {
                unano_cc = u8_next(unano_lines[unano_cl], unano_cc, ll);
                vi_redraw();
            }
        } else if (k == 0x84 || k == 0x01) {   /* Home */
            unano_cc = 0; vi_redraw();
        } else if (k == 0x85 || k == 0x05) {   /* End */
            unano_cc = (int)strlen_(unano_lines[unano_cl]);
            vi_redraw();
        } else if (k == 0x89) {                /* Delete: glyph */
            unano_delete();
            vi_redraw();
        } else if (k == 0x08 || k == 0x7F) {   /* Backspace: glyph */
            if (unano_cc > 0) {
                (void)unano_backspace();
                vi_redraw();
            } else if (unano_cl > 0) {
                char *prev = unano_lines[unano_cl - 1];
                char *l = unano_lines[unano_cl];
                int plen = (int)strlen_(prev);
                int ll = (int)strlen_(l);
                if (plen + ll < UNANO_LINE_CAP - 1) {
                    strcat_(prev, l);
                    unano_cc = plen;
                    unano_cl--;
                    for (int i = unano_cl + 1; i + 1 < unano_nlines; i++)
                        strncpy_(unano_lines[i], unano_lines[i + 1],
                                 UNANO_LINE_CAP);
                    unano_nlines--;
                    unano_dirty = 1;
                    vi_print_all(file);
                }
            }
        } else if (k == '\n' || k == '\r') {   /* ENTER: split */
            if (unano_nlines < UNANO_MAX_LINES) {
                char *l = unano_lines[unano_cl];
                char tail[UNANO_LINE_CAP];
                strncpy_(tail, l + unano_cc, UNANO_LINE_CAP);
                l[unano_cc] = 0;
                for (int i = unano_nlines; i > unano_cl + 1; i--)
                    strncpy_(unano_lines[i], unano_lines[i - 1],
                             UNANO_LINE_CAP);
                strncpy_(unano_lines[unano_cl + 1], tail, UNANO_LINE_CAP);
                unano_nlines++;
                unano_cl++;
                unano_cc = 0;
                unano_dirty = 1;
                vi_print_all(file);
            }
        } else if (k >= 0x20 && k < 0x7F) {    /* insert ASCII */
            char one[2];
            one[0] = (char)k;
            one[1] = 0;
            if (unano_insert_text(one, 1))
                vi_redraw();
        }
        /* other keys ignored in insert mode */
    }
}

static void vi_run(const char *file) {
    unano_load(file);
    unano_ime_off();
    vi_mode = VI_MODE_CMD;
    vi_print_all(file);

    int pending_d = 0;    /* saw one 'd' (waiting for the second) */
    int pending_y = 0;    /* saw one 'y' */
    for (;;) {
        int k = unano_getch_blocking();
        if (pending_d || pending_y) {
            pending_d = pending_y = 0;
            if (k == 'd') {
                unano_cut_line();
                vi_print_all(file);
            } else if (k == 'y') {
                unano_copy_line();
                vi_status(file);
                vi_redraw();
            }
            continue;
        }
        if (k == 'h' || k == 0x82) {
            if (unano_cc > 0) {
                unano_cc = u8_prev(unano_lines[unano_cl], unano_cc);
                vi_redraw();
            }
        } else if (k == 'l' || k == 0x83) {
            int ll = (int)strlen_(unano_lines[unano_cl]);
            if (unano_cc < ll) {
                unano_cc = u8_next(unano_lines[unano_cl], unano_cc, ll);
                vi_redraw();
            }
        } else if (k == 'j' || k == 0x81) {
            if (unano_cl + 1 < unano_nlines) {
                unano_cl++;
                int ll = (int)strlen_(unano_lines[unano_cl]);
                if (unano_cc > ll) unano_cc = ll;
                vi_redraw();
            }
        } else if (k == 'k' || k == 0x80) {
            if (unano_cl > 0) {
                unano_cl--;
                int ll = (int)strlen_(unano_lines[unano_cl]);
                if (unano_cc > ll) unano_cc = ll;
                vi_redraw();
            }
        } else if (k == '0' || k == 0x84) {
            unano_cc = 0; vi_redraw();
        } else if (k == '$' || k == 0x85) {
            unano_cc = (int)strlen_(unano_lines[unano_cl]);
            vi_redraw();
        } else if (k == 'x' || k == 0x89) {
            unano_delete();
            vi_redraw();
        } else if (k == 'd') {
            pending_d = 1;
        } else if (k == 'y') {
            pending_y = 1;
        } else if (k == 'p') {
            unano_paste_line();
            vi_print_all(file);
        } else if (k == 'i') {
            vi_insert_mode(file);
        } else if (k == 'a') {
            {
                int ll = (int)strlen_(unano_lines[unano_cl]);
                if (unano_cc < ll)
                    unano_cc = u8_next(unano_lines[unano_cl],
                                       unano_cc, ll);
            }
            vi_insert_mode(file);
        } else if (k == 'o') {
            if (unano_nlines < UNANO_MAX_LINES) {
                for (int i = unano_nlines; i > unano_cl + 1; i--)
                    strncpy_(unano_lines[i], unano_lines[i - 1],
                             UNANO_LINE_CAP);
                unano_lines[unano_cl + 1][0] = 0;
                unano_nlines++;
                unano_cl++;
                unano_cc = 0;
                unano_dirty = 1;
            }
            vi_insert_mode(file);
        } else if (k == ':') {
            if (vi_ex_line(file)) {   /* BUG #111: clear on exit */
                puts_("\033[2J\033[H");
                return;
            }
            vi_print_all(file);
        }
        /* other keys ignored in command mode */
    }
}

int app_main(int argc, char **argv) {
    if (argc < 2) {
        puts_("usage: ");
        puts_(argv[0]);
        puts_(" <file>\n");
        return 0;
    }
    vi_run(argv[1]);
    return 0;
}
