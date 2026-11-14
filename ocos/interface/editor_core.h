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

#include "l1_lib.h"
#include "l1svc_ops.h"
#include "fs_lib.h"

/* ==================================================================
 * WP-10-wp08fix1: nano-style editor (moved verbatim from ush)
 * ================================================================== */

#define UNANO_MAX_LINES 256
#define UNANO_LINE_CAP  128

static char unano_lines[UNANO_MAX_LINES][UNANO_LINE_CAP];
/* BUG #34 (P1, WP-fix-critical-security): set by unano_load when the
 * on-disk file exceeded the editor's fixed limits (256 lines / 128
 * bytes per line).  unano_save REFUSES to overwrite while this is set:
 * the loaded buffer only holds a prefix of the file, and the old code
 * wrote that prefix back silently - opening an oversized file and
 * saving destroyed everything past the limits with NO warning. */
static int  unano_file_truncated;
static int  unano_nlines;
static int  unano_dirty;
static int  unano_cl;    /* current line */
static int  unano_cc;    /* cursor col (BYTE offset into the line) */

/* ---- clipboard (one line, UTF-8 bytes) --------------------------- */
static char unano_clip[UNANO_LINE_CAP];
static int  unano_clip_len;
static int  unano_clip_valid;

/* ---- IME state (the engine itself lives in the L0 display service;
 * the editor only toggles + feeds keys + renders the preedit strip
 * on the status bar) ----------------------------------------------- */
static int  unano_ime_on;          /* F2 switch                        */
static int  unano_ime_composing;   /* a preedit strip is showing       */
static char unano_ime_strip[160];  /* the last strip the service sent  */
static int  unano_ime_err;         /* WP06g3-4.6: service-down notice  */

/* ---- WP06g3-4.7: the pinned status bar ----------------------------
 * The bar is drawn at the BOTTOM row via ANSI cursor addressing
 * (\033[s save, \033[999;1H pin-to-last-row, \033[K erase-line, then
 * the bar text, \033[u restore) and is DIFFERENTIAL: the bar text is
 * compared against the last drawn copy and nothing is emitted when
 * it has not changed.  Keystroke editing therefore scrolls nothing;
 * the key legend shows ONCE (in the print header / ^G help), never in
 * the per-keystroke bar (postmortem disaster 7: 3 scrolled lines per
 * letter, 8 full-bar prints in one short session). */
static char unano_bar_prev[192];

static void unano_ansi(const char *s) {
    sys_write(1, s, (int)strlen_(s));
}

/* Shared differential bar renderer (nano's own bar and vi's modal bar
 * both go through here): identical text emits NOTHING; changed text
 * is repainted at the pinned bottom row with cursor save/restore. */
static void unano_bar_draw(const char *bar) {
    for (int i = 0; ; i++) {
        if (bar[i] != unano_bar_prev[i]) {
            /* changed: redraw at the pinned bottom row */
            strncpy_(unano_bar_prev, bar, sizeof(unano_bar_prev));
            unano_ansi("\033[s");          /* save cursor            */
            unano_ansi("\033[999;1H");     /* pin to the last row   */
            unano_ansi("\r\033[K");       /* erase the whole line  */
            sys_write(1, bar, (int)strlen_(bar));
            unano_ansi("\033[u");          /* restore cursor        */
            return;
        }
        if (!bar[i] && !unano_bar_prev[i]) return;   /* identical */
    }
}

static void unano_bar_invalidate(void) {
    unano_bar_prev[0] = 0;   /* force the next statusbar() to draw */
}

static void unano_statusbar(const char *file) {
    char bar[192];
    int n = 0;
    /* "[* ]file 1/3 IME:on [nihao] 1你岄 2你呴" - no key legend */
    if (n < (int)sizeof(bar) - 1) bar[n++] = '[';
    if (unano_dirty && n < (int)sizeof(bar) - 1) bar[n++] = '*';
    if (n < (int)sizeof(bar) - 1) bar[n++] = ' ';
    for (int i = 0; file[i] && n < (int)sizeof(bar) - 1; i++) bar[n++] = file[i];
    if (n < (int)sizeof(bar) - 1) bar[n++] = ' ';
    /* line/col */
    {
        char num[12];
        int m = 0;
        int v = unano_cl + 1;
        if (v == 0) num[m++] = '0';
        while (v > 0 && m < 11) { num[m++] = (char)('0' + v % 10); v /= 10; }
        for (int i = 0; i < m && n < (int)sizeof(bar) - 1; i++) bar[n++] = num[m - 1 - i];
        if (n < (int)sizeof(bar) - 1) bar[n++] = '/';
        m = 0; v = unano_nlines;
        if (v == 0) num[m++] = '0';
        while (v > 0 && m < 11) { num[m++] = (char)('0' + v % 10); v /= 10; }
        for (int i = 0; i < m && n < (int)sizeof(bar) - 1; i++) bar[n++] = num[m - 1 - i];
    }
    if (unano_ime_on) {
        const char *tag = tr_(unano_ime_composing ? "editor_ime_on" : "editor_ime_idle");
        for (int i = 0; tag[i] && n < (int)sizeof(bar) - 1; i++) bar[n++] = tag[i];
    }
    if (unano_ime_err) {
        const char *err = tr_("editor_ime_err");
        for (int i = 0; err[i] && n < (int)sizeof(bar) - 1; i++) bar[n++] = err[i];
    }
    bar[n] = 0;
    /* preedit strip: rendered in the BAR (never in the text flow) */
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

/* ---- WP06g3-4.7: viewport rendering (the rewrite) -----------------
 * Old model: the typewriter stream - every keystroke re-PRINTED the
 * current line at the console cursor (wherever that was), so the text
 * landed on the wrong screen row and every print scrolled the stream.
 * New model: the screen is addressed.  Row 1 carries the key legend,
 * rows 2..N+1 carry a 20-line viewport of the buffer (clamped to keep
 * the current line visible), the LAST row carries the differential
 * status bar.  A keystroke repaints exactly ONE addressed row and
 * moves the console cursor to the addressed glyph cell - zero output
 * scrolls, the cursor is always on the real current line. */
#define ED_VIEW_ROWS 20          /* viewport height (any console >= 25 rows) */
#define ED_ROW_BASE 2            /* first buffer row's screen row (1-based) */

static int unano_view_top = 0;   /* buffer index at viewport row 0 */

static void unano_goto(int row, int col) {
    char seq[20];
    char num[12];
    int n = 0, m = 0, v = row;
    seq[n++] = 27; seq[n++] = '[';
    if (v == 0) num[m++] = '0';
    while (v > 0 && m < 11) { num[m++] = (char)('0' + v % 10); v /= 10; }
    for (int i = 0; i < m; i++) seq[n++] = num[m - 1 - i];
    seq[n++] = ';';
    m = 0; v = col;
    if (v == 0) num[m++] = '0';
    while (v > 0 && m < 11) { num[m++] = (char)('0' + v % 10); v /= 10; }
    for (int i = 0; i < m; i++) seq[n++] = num[m - 1 - i];
    seq[n++] = 'H';
    seq[n] = 0;
    sys_write(1, seq, n);
}

static void unano_view_ensure(void) {
    if (unano_cl < unano_view_top) unano_view_top = unano_cl;
    if (unano_cl >= unano_view_top + ED_VIEW_ROWS)
        unano_view_top = unano_cl - ED_VIEW_ROWS + 1;
    if (unano_view_top < 0) unano_view_top = 0;
}

/* repaint ONE addressed row: goto, erase-to-EOL, gutter, text */
static void unano_row_draw(int i) {
    unano_goto(i - unano_view_top + ED_ROW_BASE, 1);
    unano_ansi("\033[K");
    unano_prefix(i + 1);
    sys_write(1, unano_lines[i], strlen_(unano_lines[i]));
}

static void unano_view_redraw(void) {
    for (int i = unano_view_top;
         i < unano_nlines && i < unano_view_top + ED_VIEW_ROWS; i++)
        unano_row_draw(i);
    /* blank any leftover viewport rows below the buffer tail */
    for (int i = unano_nlines;
         i < unano_view_top + ED_VIEW_ROWS; i++) {
        unano_goto(i - unano_view_top + ED_ROW_BASE, 1);
        unano_ansi("\033[K");
    }
}

static void unano_redraw_line(void) {
    int old_top = unano_view_top;
    unano_view_ensure();
    if (unano_view_top != old_top) {
        unano_view_redraw();     /* the window moved: repaint it all */
    } else {
        unano_row_draw(unano_cl);
    }
    /* place the console cursor ON the current glyph cell (gutter is
     * 5 cells wide: "NNN : ") */
    {
        int col = 5 + u8_cols(unano_lines[unano_cl], unano_cc) + 1;
        unano_goto(unano_cl - unano_view_top + ED_ROW_BASE, col);
    }
}
static __attribute__((unused)) void unano_state(const char *file) {
    /* WP06g3-4.7: the differential pinned status bar replaces the old
     * per-keystroke full-bar reprint (postmortem disaster 7). */
    unano_statusbar(file);
}

static __attribute__((unused)) void unano_print_all(const char *file) {
    /* WP06g3-4.7: addressed full-screen layout - the key legend sits
     * ONCE on row 1, the viewport on rows 2..N+1, the bar on the last
     * row.  Nothing in the editing loop scrolls. */
    unano_ansi("\033[2J");       /* clear screen, cursor home */
    unano_goto(1, 1);
    puts_("-- ");
    puts_(file);
    puts_(" --");
    puts_(tr_("editor_helpbar"));
    puts_(" --");
    unano_view_ensure();
    unano_view_redraw();
    unano_bar_invalidate();
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
 *   1 = preedit strip updated (stored in unano_ime_strip)
 *   2 = committed - text already INSERTED into the line
 *   3 = committed + the caller still handles this key
 *   4 = SERVICE FAILURE (WP06g3-4.6): the call failed or the protocol
 *       is malformed - the caller must NOT let the key silently fall
 *       into the file text (postmortem disaster 6); the bar shows the
 *       unavailability notice instead. */
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
    if (rc != 0) return 4;              /* service down: NO passthrough */
    if (proto[0] < '0' || proto[0] > '3' || proto[1] != '\n')
        return 4;                        /* malformed: NO passthrough   */
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
        unano_ime_err = 0;
        return 1;
    }
    if (act == 2 || act == 3) {
        unano_insert_text(text, (int)strlen_(text));
        unano_ime_composing = 0;
        unano_ime_strip[0] = 0;
        unano_ime_err = 0;
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
    unano_ime_err = 0;
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
        unano_ime_err = 0;
    }
}

/* ==================================================================
 * save / load
 * ================================================================== */
static int unano_save(const char *file) {
    /* BUG #34 FIX: never write a truncated buffer back over the file. */
    if (unano_file_truncated) {
        puts_("editor: file exceeds editor limits (256 lines / 128 bytes "
              "per line) - save REFUSED, original file untouched\n");
        return -1;
    }
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
    unano_file_truncated = 0;   /* BUG #34 */
    long fd = sys_open(file, 1);
    if (fd >= 0) {
        static char rbuf[4096];
        long n2;
        char cur[UNANO_LINE_CAP];
        int cl = 0;
        int dropped_lines = 0, long_line = 0;
        cur[0] = 0;
        while ((n2 = sys_read(fd, rbuf, 4096)) > 0) {
            for (int i = 0; i < (int)n2; i++) {
                if (rbuf[i] == '\n') {
                    if (unano_nlines < UNANO_MAX_LINES) {
                        strncpy_(unano_lines[unano_nlines++], cur,
                                 UNANO_LINE_CAP);
                    } else {
                        dropped_lines++;   /* BUG #34: track, don't drop silently */
                    }
                    cl = 0;
                    cur[0] = 0;
                } else if (cl + 1 < UNANO_LINE_CAP) {
                    cur[cl++] = rbuf[i];
                    cur[cl] = 0;
                } else {
                    long_line = 1;        /* BUG #34: this line exceeds 128 bytes */
                }
            }
        }
        sys_close(fd);
        if (cl > 0 || unano_nlines == 0) {
            if (unano_nlines < UNANO_MAX_LINES) {
                strncpy_(unano_lines[unano_nlines++], cur,
                         UNANO_LINE_CAP);
            } else {
                dropped_lines++;
            }
        }
        if (dropped_lines > 0 || long_line) {
            unano_file_truncated = 1;
            puts_("editor: WARNING - file exceeds editor limits; only the "
                  "first 256 lines / 128 bytes per line are editable. "
                  "Saving is DISABLED to protect the original file.\n");
        }
    } else {
        unano_lines[0][0] = 0;
        unano_nlines = 1;
    }
}

/* BUG #111 FIX (P1, WP-fix-critical-security): editor exit residue.
 * The editors drew their chrome (top legend, viewport lines, pinned
 * status bar) with absolute ANSI positioning and NEVER cleared on
 * exit - the shell prompt then printed into the middle of the leftover
 * screen and every later redraw fought the residue.  unano_leave()
 * wipes the panel and homes the cursor before the editor returns. */
static void unano_leave(void) {
    unano_ansi("\033[2J");       /* clear screen */
    unano_ansi("\033[H");        /* cursor home */
    unano_ansi("\033[u");        /* drop any saved cursor slot */
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

        /* WP06g3-4.6 IME preedit routing: while IME is ON, letters go
         * through the engine FIRST and NEVER leak into the file text -
         * the pinyin lives in the preedit strip on the status bar until
         * a candidate commits (postmortem disaster 6: a failed service
         * call used to pass the raw letters straight into the buffer,
         * silently).  While composing, the IME's own keys (digits,
         * space, paging, enter, esc, backspace) route to the engine
         * too; act==4 (service down/malformed) shows the bar notice
         * and swallows the key - visible degradation, never silence. */
        if (unano_ime_on &&
            (unano_ime_composing ||
             (k >= 'a' && k <= 'z'))) {
            int act = unano_ime_feed(k);
            if (act == 4) {
                unano_ime_err = 1;
                unano_state(file);      /* bar shows the notice */
                continue;               /* the key does NOT enter the text */
            }
            if (act == 1) {
                unano_state(file);      /* preedit strip on the bar */
                continue;
            }
            if (act == 2) {
                unano_redraw_line();
                unano_state(file);
                continue;
            }
            if (act == 3) {
                /* committed; fall through so the editor handles the
                 * key too (punctuation, navigation) */
                unano_redraw_line();
                unano_state(file);
                if (unano_ime_composing) continue;
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
            puts_("\n");
            puts_(tr_("editor_pos_line"));
            putu_((u64)(unano_cl + 1));
            puts_(tr_("editor_pos_col"));
            putu_((u64)(u8_cols(unano_lines[unano_cl], unano_cc) + 1));
            puts_("]\n");
            unano_state(file);
            unano_redraw_line();
        } else if (k == 0x07) {   /* ^G: help */
            puts_("\n");
            puts_(tr_("editor_help1"));
            puts_(tr_("editor_help2"));
            puts_(tr_("editor_help3"));
            puts_(tr_("editor_help4"));
            unano_state(file);
            unano_redraw_line();
        } else if (k == 0x0F) {   /* ^O: save */
            if (unano_save(file) == 0) {
                puts_("\n");
                puts_(tr_("editor_wrote_head"));
                putu_((u64)unano_nlines);
                puts_(tr_("editor_wrote_tail"));
            } else {
                puts_("\n");
                puts_(tr_("editor_save_failed"));
                puts_("\n");
            }
            unano_state(file);
            unano_redraw_line();
        } else if (k == 0x18) {   /* ^X: exit */
            if (unano_dirty) {
                puts_("\n");
                puts_(tr_("editor_save_prompt"));
                int c = unano_getch_blocking();
                putc_((char)c);
                putc_('\n');
                if (c == 'y' || c == 'Y') {
                    if (unano_save(file) != 0) {
                        puts_(tr_("editor_save_failed_stay"));
                        puts_("\n");
                        unano_state(file);
                        unano_redraw_line();
                        continue;
                    }
                    unano_ime_off();
                    unano_leave();
                    return;
                }
                unano_ime_off();
                unano_leave();
                return;   /* quit without saving */
            }
            unano_ime_off();
            unano_leave();
            return;
        } else if (k >= 0x20 && k < 0x7F) {   /* insert ASCII */
            char one[2];
            one[0] = (char)k;
            one[1] = 0;
            if (unano_insert_text(one, 1)) {
                unano_redraw_line();
                unano_state(file);   /* differential: col change only */
            }
        }
        /* other keys ignored */
    }
}

#endif /* OC_L1_EDITOR_CORE_H */
