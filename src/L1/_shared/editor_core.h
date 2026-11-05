/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP06g2 - shared L1 editor engine (editor_core).
 *
 * The nano-style editor engine every editor app includes (nano, vi
 * under its modal layer, edit).  WP06g2 upgrades:
 *
 *   - UTF-8 CHARACTER-based motion and deletion (u8_prev/u8_next/
 *     u8_cols): Left/Right step glyph boundaries, Backspace/Delete
 *     remove the whole sequence and wipe exactly as many cells as it
 *     rendered (the ush line-editor discipline from WP06g1, moved
 *     here so editors and the shell behave identically).
 *   - Multi-byte INSERT: the IME commits UTF-8 text into the line at
 *     the cursor.
 *   - The SYSTEM IME: F2 toggles Chinese input; while composing, the
 *     letters go through the L1SVC display IME_KEY session op (ONE
 *     engine in L0 - editors never embed it), the compose strip
 *     renders on the status line, commits land as text.
 *   - Cut/copy/paste: ^K cuts the current line, ^R copies it, ^U
 *     pastes the clipboard line below the cursor (UTF-8-safe: lines
 *     are byte strings; motion/deletion/display are glyph-aware).
 *
 * Keys (nano face): arrows move, typing inserts, BKSP/DEL delete,
 * ENTER splits, ^K cut line, ^R copy line, ^U paste, ^C position,
 * ^O save, ^X exit (asks when dirty), ^G help, F2 IME toggle.
 * Rendering: print the file once, redraw only the CURRENT line with
 * \r (the console has no ANSI escapes). */
#ifndef OC_L1_EDITOR_CORE_H
#define OC_L1_EDITOR_CORE_H

#include "_shared/l1_lib.h"
#include "_shared/l1svc_ops.h"
#include "_shared/fs_lib.h"

/* ==================================================================
 * WP-10-wp08fix1: nano-style editor (moved verbatim from ush)
 * ================================================================== */

#define UNANO_MAX_LINES 256
#define UNANO_LINE_CAP  128

static char unano_lines[UNANO_MAX_LINES][UNANO_LINE_CAP];
static int  unano_nlines;
static int  unano_dirty;
static int  unano_cl;    /* current line */
static int  unano_cc;    /* cursor col (BYTE offset into the line) */

/* ---- clipboard (one line, UTF-8 bytes) --------------------------- */
static char unano_clip[UNANO_LINE_CAP];
static int  unano_clip_len;
static int  unano_clip_valid;

/* ---- IME state (the engine itself lives in the L0 display service;
 * the editor only toggles + feeds keys + renders the strip) -------- */
static int  unano_ime_on;          /* F2 switch                        */
static int  unano_ime_composing;   /* a strip is showing               */
static char unano_ime_strip[160];  /* the last strip the service sent  */

/* ==================================================================
 * WP06g2: UTF-8 glyph helpers (the ush line-editor discipline)
 * ================================================================== */
static int u8_is_lead(char c) {
    return (((unsigned char)c & 0xC0) != 0x80);
}
/* byte offset of the glyph boundary at or before cur */
static int u8_prev(const char *line, int cur) {
    if (cur <= 0) return 0;
    int p = cur - 1;
    while (p > 0 && !u8_is_lead(line[p])) p--;
    return p;
}
/* byte offset just past the glyph at cur */
static int u8_next(const char *line, int cur, int llen) {
    if (cur >= llen) return llen;
    int n = cur + 1;
    while (n < llen && !u8_is_lead(line[n])) n++;
    return n;
}
/* display columns of the glyph at s[0..n): hanzi (and the GB2312
 * symbol zone) render 16px = 2 cells, everything else 1.  The exact
 * per-codepoint width table of the kernel renderer compressed to
 * ranges (mirrors ush's k_gb2312_narrow set for the narrow symbols
 * below U+2E80; CJK >= U+2E80 is wide by the generic check). */
struct u8_range { unsigned short lo, hi; };
static const struct u8_range k_wide_extra[] = {
    /* the most common wide ranges above U+2E80 (CJK, fullwidth) */
    {0x2E80,0x2EFF},{0x3000,0x303F},{0x3041,0x30FF},{0x31F0,0x31FF},
    {0x3400,0x4DBF},{0x4E00,0x9FFF},{0xF900,0xFAFF},{0xFF00,0xFF60},
    {0xFFE0,0xFFE6},
};
static int u8_glyph_cols(const char *s, int n) {
    if (n <= 0) return 0;
    unsigned cp = (unsigned char)s[0];
    int need = 1;
    if ((cp & 0xE0) == 0xC0) need = 2;
    else if ((cp & 0xF0) == 0xE0) need = 3;
    else if ((cp & 0xF8) == 0xF0) need = 4;
    if (need > n) return 1;              /* broken tail: 1 cell */
    for (int i = 1; i < need; i++)
        cp = (cp << 8) | (unsigned char)s[i];
    /* decode to a codepoint */
    unsigned v = 0;
    if (need == 1) v = cp;
    else if (need == 2) v = (cp >> 8 & 0x1F) << 6 | (cp & 0xFF & 0x3F);
    else if (need == 3)
        v = ((cp >> 16) & 0x0F) << 12 | ((cp >> 8) & 0x3F) << 6 | (cp & 0x3F);
    else
        v = ((cp >> 24) & 0x07) << 18 | ((cp >> 16) & 0x3F) << 12 |
            ((cp >> 8) & 0x3F) << 6 | (cp & 0x3F);
    for (unsigned i = 0;
         i < sizeof(k_wide_extra) / sizeof(k_wide_extra[0]); i++)
        if (v >= k_wide_extra[i].lo && v <= k_wide_extra[i].hi)
            return 2;
    return 1;
}
/* total display width of s[0..len) */
static int u8_cols(const char *s, int len) {
    int cols = 0, i = 0;
    while (i < len) {
        int n = u8_next(s, i, len);
        cols += u8_glyph_cols(s + i, n - i);
        i = n;
    }
    return cols;
}

/* ==================================================================
 * line primitives
 * ================================================================== */
static void unano_prefix(int lineno) {
    putu_((u64)lineno);
    int n = 0;
    int v = lineno;
    while (v > 0) { v /= 10; n++; }
    while (n < 3) { putc_(' '); n++; }
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

static __attribute__((unused)) void unano_state(const char *file) {
    puts_("[");
    puts_(unano_dirty ? "* " : "  ");
    puts_(file);
    puts_(" ");
    putu_((u64)(unano_cl + 1));
    putc_('/');
    putu_((u64)unano_nlines);
    if (unano_ime_on) {
        puts_(unano_ime_composing ? " IME:ON]" : " IME:on]");
    } else {
        puts_("]");
    }
    if (unano_ime_composing && unano_ime_strip[0]) {
        puts_("  ");
        puts_(unano_ime_strip);
    }
    puts_(" ^O save ^X exit ^K cut ^R copy ^U paste ^G help F2 ime\n");
}

static __attribute__((unused)) void unano_print_all(const char *file) {
    puts_("-- ");
    puts_(file);
    puts_(" -- ^O save ^X exit ^K cut ^R copy ^U paste ^G help F2 ime --\n");
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

/* insert one UTF-8 sequence (or any bytes) at the cursor */
static int unano_insert_text(const char *bytes, int len) {
    char *l = unano_lines[unano_cl];
    int ll = (int)strlen_(l);
    if (len <= 0) return 0;
    if (ll + len >= UNANO_LINE_CAP) return 0;
    for (int i = ll; i >= unano_cc; i--) l[i + len] = l[i];
    for (int i = 0; i < len; i++) l[unano_cc + i] = bytes[i];
    unano_cc += len;
    unano_dirty = 1;
    return 1;
}

/* ---- WP06g2: character-based backspace/delete ------------------- */
/* returns the new cursor (byte offset) */
static int unano_backspace(void) {
    char *l = unano_lines[unano_cl];
    int ll = (int)strlen_(l);
    if (unano_cc <= 0) return unano_cc;
    int pstart = u8_prev(l, unano_cc);
    int n = unano_cc - pstart;
    int cols = u8_cols(l + pstart, n);
    for (int i = pstart; i <= ll; i++) l[i] = l[i + n];
    unano_cc = pstart;
    unano_dirty = 1;
    /* wipe exactly the cells the glyph occupied */
    for (int c = 0; c < cols; c++) {
        putc_('\b'); putc_(' '); putc_('\b');
    }
    return unano_cc;
}

static void unano_delete(void) {
    char *l = unano_lines[unano_cl];
    int ll = (int)strlen_(l);
    if (unano_cc >= ll) return;
    /* snap to a boundary defensively, then drop the whole glyph */
    if (!u8_is_lead(l[unano_cc])) unano_cc = u8_prev(l, unano_cc);
    int n = u8_next(l, unano_cc, ll) - unano_cc;
    for (int i = unano_cc; i <= ll; i++) l[i] = l[i + n];
    unano_dirty = 1;
}

/* ---- cut / copy / paste (whole-line, UTF-8-safe) ----------------- */
static void unano_copy_line(void) {
    char *l = unano_lines[unano_cl];
    int ll = (int)strlen_(l);
    for (int i = 0; i < ll && i < UNANO_LINE_CAP - 1; i++)
        unano_clip[i] = l[i];
    unano_clip[ll < UNANO_LINE_CAP - 1 ? ll : UNANO_LINE_CAP - 1] = 0;
    unano_clip_len = (int)strlen_(unano_clip);
    unano_clip_valid = 1;
}

static void unano_cut_line(void) {
    unano_copy_line();
    for (int i = unano_cl; i + 1 < unano_nlines; i++)
        strncpy_(unano_lines[i], unano_lines[i + 1], UNANO_LINE_CAP);
    unano_nlines--;
    if (unano_nlines == 0) {
        unano_lines[0][0] = 0;
        unano_nlines = 1;
    }
    if (unano_cl >= unano_nlines) unano_cl = unano_nlines - 1;
    int ll = (int)strlen_(unano_lines[unano_cl]);
    if (unano_cc > ll) unano_cc = ll;
    unano_dirty = 1;
}

static void unano_paste_line(void) {
    if (!unano_clip_valid || unano_nlines >= UNANO_MAX_LINES) return;
    for (int i = unano_nlines; i > unano_cl + 1; i--)
        strncpy_(unano_lines[i], unano_lines[i - 1], UNANO_LINE_CAP);
    strncpy_(unano_lines[unano_cl + 1], unano_clip, UNANO_LINE_CAP);
    unano_nlines++;
    unano_cl++;
    unano_cc = 0;
    unano_dirty = 1;
}

/* ==================================================================
 * WP06g2: the SYSTEM IME bridge (L1SVC display IME_KEY)
 * ================================================================== */
/* Feed one key to the system IME.  Returns the action:
 *   0 = not IME business (caller handles the key)
 *   1 = strip updated (already stored in unano_ime_strip)
 *   2 = committed - text already INSERTED into the line
 *   3 = committed + the caller still handles this key */
static int unano_ime_feed(int key) {
    char arg[16];
    char num[12];
    int n = 0;
    if (key == 0) { arg[0] = 0; }
    else {
        int v = key, m = 0;
        char t[12];
        if (v == 0) { t[m++] = '0'; }
        while (v > 0 && m < 11) { t[m++] = (char)('0' + v % 10); v /= 10; }
        for (int i = 0; i < m; i++) num[i] = t[m - 1 - i];
        n = m;
        for (int i = 0; i < n; i++) arg[i] = num[i];
        arg[n] = 0;
    }
    char proto[256];
    long rc = sys_l1svc(L1SVC_CAT_DISPLAY, L1SVC_DISP_IME_KEY,
                        (long)arg, (long)proto, (long)sizeof(proto));
    if (rc != 0) return 0;              /* service down: passthrough */
    if (proto[0] < '0' || proto[0] > '3' || proto[1] != '\n')
        return 0;                        /* malformed: passthrough */
    int act = proto[0] - '0';
    const char *text = proto + 2;
    if (act == 1) {
        int i = 0;
        while (text[i] && i < (int)sizeof(unano_ime_strip) - 1) {
            unano_ime_strip[i] = text[i];
            i++;
        }
        unano_ime_strip[i] = 0;
        unano_ime_composing = (unano_ime_strip[0] != 0);
        return 1;
    }
    if (act == 2 || act == 3) {
        unano_insert_text(text, (int)strlen_(text));
        unano_ime_composing = 0;
        unano_ime_strip[0] = 0;
        return act;
    }
    return 0;
}

static void unano_ime_off(void) {
    if (unano_ime_on) {
        char out[8];
        (void)sys_l1svc(L1SVC_CAT_DISPLAY, L1SVC_DISP_IME_END,
                        0, (long)out, (long)sizeof(out));
    }
    unano_ime_on = 0;
    unano_ime_composing = 0;
    unano_ime_strip[0] = 0;
}

static void unano_ime_toggle(void) {
    if (unano_ime_on) {
        unano_ime_off();
    } else {
        char out[8];
        (void)sys_l1svc(L1SVC_CAT_DISPLAY, L1SVC_DISP_IME_BEGIN,
                        0, (long)out, (long)sizeof(out));
        unano_ime_on = 1;
        unano_ime_composing = 0;
        unano_ime_strip[0] = 0;
    }
}

/* ==================================================================
 * save / load
 * ================================================================== */
static int unano_save(const char *file) {
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

static void unano_load(const char *file) {
    unano_nlines = 0;
    unano_dirty = 0;
    unano_cl = 0;
    unano_cc = 0;
    long fd = sys_open(file, 1);
    if (fd >= 0) {
        static char rbuf[4096];
        long n2;
        char cur[UNANO_LINE_CAP];
        int cl = 0;
        cur[0] = 0;
        while ((n2 = sys_read(fd, rbuf, 4096)) > 0) {
            for (int i = 0; i < (int)n2; i++) {
                if (rbuf[i] == '\n') {
                    if (unano_nlines < UNANO_MAX_LINES) {
                        strncpy_(unano_lines[unano_nlines++], cur,
                                 UNANO_LINE_CAP);
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
                strncpy_(unano_lines[unano_nlines++], cur,
                         UNANO_LINE_CAP);
            }
        }
    } else {
        unano_lines[0][0] = 0;
        unano_nlines = 1;
    }
}

/* ==================================================================
 * the nano face: modal-free editing loop
 * ================================================================== */
static __attribute__((unused)) void ush_nano_run(const char *file) {
    unano_load(file);
    unano_ime_off();

    unano_print_all(file);
    unano_state(file);
    unano_redraw_line();

    for (;;) {
        int k = unano_getch_blocking();

        /* F2: toggle the system IME (works in every mode) */
        if (k == 0x91) {
            unano_ime_toggle();
            unano_state(file);
            unano_redraw_line();
            continue;
        }

        /* IME compose: letters/digits/space/paging/enter/esc/backspace
         * go to the ENGINE first while composing or (for letters)
         * while IME is on */
        if (unano_ime_on &&
            (unano_ime_composing ||
             (k >= 'a' && k <= 'z'))) {
            int act = unano_ime_feed(k);
            if (act == 1) {
                unano_state(file);
                unano_redraw_line();
                continue;
            }
            if (act == 2) {
                unano_redraw_line();
                unano_state(file);
                unano_redraw_line();
                continue;
            }
            if (act == 3) {
                /* committed; fall through so the editor handles the
                 * key too (punctuation, navigation) */
                unano_redraw_line();
            }
            /* act == 0: not IME business after all */
        }

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
        } else if (k == 0x82) {   /* Left: one GLYPH */
            if (unano_cc > 0) {
                unano_cc = u8_prev(unano_lines[unano_cl], unano_cc);
                unano_redraw_line();
            }
        } else if (k == 0x83) {   /* Right: one GLYPH */
            {
                int ll = (int)strlen_(unano_lines[unano_cl]);
                if (unano_cc < ll) {
                    unano_cc = u8_next(unano_lines[unano_cl],
                                       unano_cc, ll);
                    unano_redraw_line();
                }
            }
        } else if (k == 0x84 || k == 0x01) {   /* Home / ^A */
            unano_cc = 0; unano_redraw_line();
        } else if (k == 0x85 || k == 0x05) {   /* End / ^E */
            unano_cc = (int)strlen_(unano_lines[unano_cl]);
            unano_redraw_line();
        } else if (k == 0x89) {   /* Delete: whole glyph */
            unano_delete();
            unano_redraw_line();
        } else if (k == 0x08 || k == 0x7F) {   /* Backspace */
            char *l = unano_lines[unano_cl];
            if (unano_cc > 0) {
                (void)unano_backspace();
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
                        strncpy_(unano_lines[i], unano_lines[i + 1],
                                 UNANO_LINE_CAP);
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
                for (int i = unano_nlines; i > unano_cl + 1; i--) {
                    strncpy_(unano_lines[i], unano_lines[i - 1],
                             UNANO_LINE_CAP);
                }
                strncpy_(unano_lines[unano_cl + 1], tail,
                         UNANO_LINE_CAP);
                unano_nlines++;
                unano_cl++;
                unano_cc = 0;
                unano_dirty = 1;
                unano_print_all(file);
                unano_state(file);
                unano_redraw_line();
            }
        } else if (k == 0x0B) {   /* ^K: cut line */
            unano_cut_line();
            unano_print_all(file);
            unano_state(file);
            unano_redraw_line();
        } else if (k == 0x12) {   /* ^R: copy line */
            unano_copy_line();
            unano_state(file);
            unano_redraw_line();
        } else if (k == 0x15) {   /* ^U: paste clipboard line */
            unano_paste_line();
            unano_print_all(file);
            unano_state(file);
            unano_redraw_line();
        } else if (k == 0x03) {   /* ^C: report position */
            puts_("\n[line ");
            putu_((u64)(unano_cl + 1));
            puts_(", col ");
            putu_((u64)(u8_cols(unano_lines[unano_cl], unano_cc) + 1));
            puts_("]\n");
            unano_state(file);
            unano_redraw_line();
        } else if (k == 0x07) {   /* ^G: help */
            puts_("\narrows move (glyph-wise) | type to insert | BKSP/DEL delete\n");
            puts_("ENTER split | ^K cut line | ^R copy line | ^U paste\n");
            puts_("F2 Chinese IME (type pinyin; 1-9 pick; space first; PgUp/PgDn page)\n");
            puts_("^O save | ^X exit | ^C pos\n");
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
                    unano_ime_off();
                    return;
                }
                unano_ime_off();
                return;   /* quit without saving */
            }
            unano_ime_off();
            return;
        } else if (k >= 0x20 && k < 0x7F) {   /* insert ASCII */
            char one[2];
            one[0] = (char)k;
            one[1] = 0;
            if (unano_insert_text(one, 1))
                unano_redraw_line();
        }
        /* other keys ignored */
    }
}

#endif /* OC_L1_EDITOR_CORE_H */
