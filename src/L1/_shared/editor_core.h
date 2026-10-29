/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP04 - shared L1 editor engine (editor_core).
 *
 * The full nano-style editor, moved verbatim from the ush shell
 * (WP-10-wp08fix1): keys, rendering and all fix annotations are the
 * shell's own code, unchanged.  Every helper is static - the app
 * including this header owns its own copy (header-only style, exactly
 * like _shared/l1_lib.h and _shared/fs_lib.h).
 *
 * Entry point:  ush_nano_run(const char *file)
 *
 * Include from an app as:  #include "_shared/editor_core.h"
 * (the app build adds -I src/L1 to the compiler invocation). */
#ifndef OC_L1_EDITOR_CORE_H
#define OC_L1_EDITOR_CORE_H

#include "_shared/l1_lib.h"
#include "_shared/fs_lib.h"

/* ==================================================================
 * WP-10-wp08fix1: nano-style editor (moved verbatim from ush)
 * ==================================================================
 * Keys: arrows select/move, typing inserts, BKSP/DEL delete,
 * ENTER splits the line, ^K deletes the line, ^C reports the position,
 * ^O saves, ^X exits (asks before discarding), ^G shows the key guide.
 * Rendering: print the file once, then redraw only the CURRENT line
 * in place with \r (the console has no ANSI escapes). */

#define UNANO_MAX_LINES 256
#define UNANO_LINE_CAP  128

static char unano_lines[UNANO_MAX_LINES][UNANO_LINE_CAP];
static int  unano_nlines;
static int  unano_dirty;
static int  unano_cl;    /* current line */
static int  unano_cc;    /* cursor col */

static void unano_prefix(int lineno) {
    char num[12];
    putu_((u64)lineno);
    /* pad to width 3 + ": " */
    int n = 0;
    int v = lineno;
    while (v > 0) { v /= 10; n++; }
    while (n < 3) { putc_(' '); n++; }
    (void)num;
    putc_(':');
    putc_(' ');
}

static void unano_redraw_line(void) {
    char cr = '\r';
    char sp = ' ';
    sys_write(1, &cr, 1);
    unano_prefix(unano_cl + 1);
    sys_write(1, unano_lines[unano_cl], strlen_(unano_lines[unano_cl]));
    sys_write(1, &sp, 1);
    sys_write(1, &cr, 1);
    unano_prefix(unano_cl + 1);
    if (unano_cc > 0) sys_write(1, unano_lines[unano_cl], unano_cc);
}

static void unano_state(const char *file) {
    puts_("[nano ");
    puts_(file);
    puts_(" line ");
    putu_((u64)(unano_cl + 1));
    putc_('/');
    putu_((u64)unano_nlines);
    puts_(unano_dirty ? "  modified] ^O save  ^X exit  ^K del-line  ^C pos  ^G help\n"
                      : "  saved   ] ^O save  ^X exit  ^K del-line  ^C pos  ^G help\n");
}

static void unano_print_all(const char *file) {
    puts_("-- nano: ");
    puts_(file);
    puts_(" -- ^O save ^X exit ^K del line ^G help --\n");
    for (int i = 0; i < unano_nlines; i++) {
        unano_prefix(i + 1);
        puts_(unano_lines[i]);
        putc_('\n');
    }
}

static int unano_getch_blocking(void) {
    for (;;) {
        long k = sys_getch();
        if (k >= 0) return (int)k;
        for (volatile int s = 0; s < 100; s++);
    }
}

static int unano_save(const char *file) {
    /* BUG-0089 FIX (A16-2): save with O_TRUNC - without it, saving a
     * SHORTER document over a longer existing file left the old tail
     * bytes behind (corrupted file). */
    long fd = sys_open(file, 22);   /* WRONLY|CREAT|TRUNC */
    if (fd < 0) return -1;
    for (int i = 0; i < unano_nlines; i++) {
        sys_write(fd, unano_lines[i], strlen_(unano_lines[i]));
        sys_write(fd, "\n", 1);
    }
    sys_close(fd);
    unano_dirty = 0;
    return 0;
}

static void ush_nano_run(const char *file) {
    /* load */
    unano_nlines = 0;
    unano_dirty = 0;
    unano_cl = 0;
    unano_cc = 0;
    long fd = sys_open(file, 1);
    if (fd >= 0) {
        static char rbuf[4096];
        long n;
        char cur[UNANO_LINE_CAP];
        int cl = 0;
        cur[0] = 0;
        while ((n = sys_read(fd, rbuf, 4096)) > 0) {
            for (int i = 0; i < (int)n; i++) {
                if (rbuf[i] == '\n') {
                    if (unano_nlines < UNANO_MAX_LINES) {
                        strncpy_(unano_lines[unano_nlines++], cur, UNANO_LINE_CAP);
                    }
                    cl = 0;
                    cur[0] = 0;
                } else if (cl + 1 < UNANO_LINE_CAP) {
                    cur[cl++] = rbuf[i];
                    cur[cl] = 0;
                }
            }
        }
        sys_close(fd);
        if (cl > 0 || unano_nlines == 0) {
            if (unano_nlines < UNANO_MAX_LINES) {
                strncpy_(unano_lines[unano_nlines++], cur, UNANO_LINE_CAP);
            }
        }
    } else {
        unano_lines[0][0] = 0;
        unano_nlines = 1;
    }

    unano_print_all(file);
    unano_state(file);
    unano_redraw_line();

    for (;;) {
        int k = unano_getch_blocking();
        if (k == 0x80) {          /* Up */
            if (unano_cl > 0) {
                unano_cl--;
                int ll = (int)strlen_(unano_lines[unano_cl]);
                if (unano_cc > ll) unano_cc = ll;
                unano_redraw_line();
            }
        } else if (k == 0x81) {   /* Down */
            if (unano_cl + 1 < unano_nlines) {
                unano_cl++;
                int ll = (int)strlen_(unano_lines[unano_cl]);
                if (unano_cc > ll) unano_cc = ll;
                unano_redraw_line();
            }
        } else if (k == 0x82) {   /* Left */
            if (unano_cc > 0) { unano_cc--; unano_redraw_line(); }
        } else if (k == 0x83) {   /* Right */
            if (unano_cc < (int)strlen_(unano_lines[unano_cl])) {
                unano_cc++; unano_redraw_line();
            }
        } else if (k == 0x84 || k == 0x01) {   /* Home / ^A */
            unano_cc = 0; unano_redraw_line();
        } else if (k == 0x85 || k == 0x05) {   /* End / ^E */
            unano_cc = (int)strlen_(unano_lines[unano_cl]); unano_redraw_line();
        } else if (k == 0x89) {   /* Delete */
            char *l = unano_lines[unano_cl];
            int ll = (int)strlen_(l);
            if (unano_cc < ll) {
                for (int i = unano_cc; i < ll; i++) l[i] = l[i + 1];
                unano_dirty = 1;
                unano_redraw_line();
            }
        } else if (k == 0x08 || k == 0x7F) {   /* Backspace */
            char *l = unano_lines[unano_cl];
            if (unano_cc > 0) {
                int ll = (int)strlen_(l);
                for (int i = unano_cc - 1; i < ll; i++) l[i] = l[i + 1];
                unano_cc--;
                unano_dirty = 1;
                unano_redraw_line();
            } else if (unano_cl > 0) {
                /* merge with previous line */
                char *prev = unano_lines[unano_cl - 1];
                int plen = (int)strlen_(prev);
                int ll = (int)strlen_(l);
                if (plen + ll < UNANO_LINE_CAP - 1) {
                    strcat_(prev, l);
                    unano_cc = plen;
                    unano_cl--;
                    for (int i = unano_cl + 1; i + 1 < unano_nlines; i++) {
                        strncpy_(unano_lines[i], unano_lines[i + 1], UNANO_LINE_CAP);
                    }
                    unano_nlines--;
                    unano_dirty = 1;
                    unano_print_all(file);
                    unano_state(file);
                    unano_redraw_line();
                }
            }
        } else if (k == '\n' || k == '\r') {   /* ENTER: split line */
            if (unano_nlines < UNANO_MAX_LINES) {
                char *l = unano_lines[unano_cl];
                char tail[UNANO_LINE_CAP];
                strncpy_(tail, l + unano_cc, UNANO_LINE_CAP);
                l[unano_cc] = 0;
                /* shift lines down */
                for (int i = unano_nlines; i > unano_cl + 1; i--) {
                    strncpy_(unano_lines[i], unano_lines[i - 1], UNANO_LINE_CAP);
                }
                strncpy_(unano_lines[unano_cl + 1], tail, UNANO_LINE_CAP);
                unano_nlines++;
                unano_cl++;
                unano_cc = 0;
                unano_dirty = 1;
                unano_print_all(file);
                unano_state(file);
                unano_redraw_line();
            }
        } else if (k == 0x0B) {   /* ^K: delete line */
            for (int i = unano_cl; i + 1 < unano_nlines; i++) {
                strncpy_(unano_lines[i], unano_lines[i + 1], UNANO_LINE_CAP);
            }
            unano_nlines--;
            if (unano_nlines == 0) {
                unano_lines[0][0] = 0;
                unano_nlines = 1;
            }
            if (unano_cl >= unano_nlines) unano_cl = unano_nlines - 1;
            int ll = (int)strlen_(unano_lines[unano_cl]);
            if (unano_cc > ll) unano_cc = ll;
            unano_dirty = 1;
            unano_print_all(file);
            unano_state(file);
            unano_redraw_line();
        } else if (k == 0x03) {   /* ^C: report position (nano binding) */
            puts_("\n[line ");
            putu_((u64)(unano_cl + 1));
            puts_(", col ");
            putu_((u64)(unano_cc + 1));
            puts_("]\n");
            unano_state(file);
            unano_redraw_line();
        } else if (k == 0x07) {   /* ^G: help */
            puts_("\narrows move | type to insert | BKSP/DEL delete\n");
            puts_("ENTER split line | ^K del line | ^O save | ^X exit | ^C pos\n");
            unano_state(file);
            unano_redraw_line();
        } else if (k == 0x0F) {   /* ^O: save */
            if (unano_save(file) == 0) {
                puts_("\n[wrote ");
                putu_((u64)unano_nlines);
                puts_(" lines]\n");
            } else {
                puts_("\n[save failed]\n");
            }
            unano_state(file);
            unano_redraw_line();
        } else if (k == 0x18) {   /* ^X: exit */
            if (unano_dirty) {
                puts_("\nSave modified buffer? (y/n): ");
                int c = unano_getch_blocking();
                putc_((char)c);
                putc_('\n');
                if (c == 'y' || c == 'Y') {
                    if (unano_save(file) != 0) {
                        puts_("[save failed - stay in editor]\n");
                        unano_state(file);
                        unano_redraw_line();
                        continue;
                    }
                    return;
                }
                return;   /* quit without saving */
            }
            return;
        } else if (k >= 0x20 && k < 0x7F) {   /* insert */
            char *l = unano_lines[unano_cl];
            int ll = (int)strlen_(l);
            if (ll + 1 < UNANO_LINE_CAP) {
                for (int i = ll; i > unano_cc; i--) l[i] = l[i - 1];
                l[unano_cc] = (char)k;
                unano_cc++;
                unano_dirty = 1;
                unano_redraw_line();
            }
        }
        /* other keys ignored */
    }
}

#endif /* OC_L1_EDITOR_CORE_H */
