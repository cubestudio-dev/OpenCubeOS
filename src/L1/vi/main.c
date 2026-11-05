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
#include "_shared/editor_core.h"

#define VI_MODE_CMD 0
#define VI_MODE_INS 1

static int vi_mode = VI_MODE_CMD;

static void vi_status(const char *file) {
    puts_("[");
    puts_(unano_dirty ? "* " : "  ");
    puts_(file);
    puts_(" ");
    putu_((u64)(unano_cl + 1));
    putc_('/');
    putu_((u64)unano_nlines);
    puts_(vi_mode == VI_MODE_INS ? " INSERT]" : " CMD]");
    if (vi_mode == VI_MODE_INS) {
        if (unano_ime_composing && unano_ime_strip[0]) {
            puts_("  ");
            puts_(unano_ime_strip);
        } else if (unano_ime_on) {
            puts_("  [IME on]");
        }
    }
    puts_("\n");
}

static void vi_redraw(void) {
    unano_redraw_line();
}

static void vi_print_all(const char *file) {
    puts_("-- vi: ");
    puts_(file);
    puts_(" -- CMD: hjkl move, x del, dd cut, yy copy, p paste, i/a/o insert, :w/:q --\n");
    for (int i = 0; i < unano_nlines; i++) {
        unano_prefix(i + 1);
        puts_(unano_lines[i]);
        putc_('\n');
    }
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
            puts_("[wrote ");
            putu_((u64)unano_nlines);
            puts_(" lines]\n");
        } else {
            puts_("[save failed]\n");
        }
    }
    if (want_q) {
        if (unano_dirty && !want_w && !bang) {
            puts_("[modified - :wq to save, :q! to discard]\n");
            return 0;
        }
        unano_ime_off();
        return 1;                     /* quit */
    }
    if (!want_w && !want_q && n > 0)
        puts_("[unknown ex command]\n");
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
        if (unano_ime_on &&
            (unano_ime_composing || (k >= 'a' && k <= 'z'))) {
            int act = unano_ime_feed(k);
            if (act == 1) {
                vi_status(file);
                vi_redraw();
                continue;
            }
            if (act == 2) {
                vi_redraw();
                vi_status(file);
                vi_redraw();
                continue;
            }
            if (act == 3) {
                vi_redraw();
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
            if (vi_ex_line(file)) return;    /* quit */
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
