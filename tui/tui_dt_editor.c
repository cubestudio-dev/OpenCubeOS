/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-10-tui-native
 * File: tui/tui_dt_editor.c
 * Purpose: the desktop text editor - a NATIVE TUI editor (no command
 *          bridge, no external nano).  Everything runs against the VFS
 *          through fs_vfs_* and renders through the real TUI cell
 *          matrix with UTF-8 / CJK double-width support.
 *
 * Data model: an array of heap lines (same session limits as the shell
 * editor: 512 lines x 256 bytes).  A partial load (file bigger than
 * the session limits) is flagged and SAVING IS REFUSED - the honest
 * nano BUG-0242 rule - so a ^O can never truncate a file silently.
 *
 * Keys (nano-style where possible):
 *   Up/Down/Left/Right/Home/End/PgUp/PgDn  cursor movement
 *   Enter      split the line at the cursor
 *   Backspace  delete the char before the cursor (merge lines)
 *   Delete     delete the char at the cursor (merge lines)
 *   ^A / ^E    line start / end            ^K  delete line
 *   ^N         new (empty buffer, asks path on save)
 *   ^O         save (asks for the path when unnamed)
 *   F12        save as (always asks for the path)
 *   ^X         exit (confirms when modified)
 *   ^G         key guide
 *   F4         toggle ASCII / pinyin input (when shell.lang == zh)
 *   F6 or ^@   set/drop the selection mark at the cursor
 *   ^C / ^X_with_mark   copy / cut the selection into the clipboard
 *   ^V         paste the clipboard at the cursor
 *
 * The pinyin IME reuses the engine behind chinese/tui_chinese.c
 * (396 syllables, 3755 GB2312-level-1 chars): letters build a
 * syllable, digits 1-9 / Space pick a candidate, Backspace drops a
 * letter.  Committed glyphs are inserted as UTF-8 into the buffer and
 * round-trip through save -> cat with no mojibake.
 */
#include "tui_desktop.h"
#include "../services/fs/fs_vfs.h"
#include "../core/lib/lib_string.h"
#include "../core/mem/mem_heap.h"
#include "../chinese/encoding.h"
#include "../chinese/render_cn.h"
#include "../chinese/tui_chinese.h"
#include "../drivers/input/driver_input_keyboard.h"

#define ED_MAX_LINES   512      /* session limit (same as shell editor) */
#define ED_LINE_MAX    256      /* bytes per line, UTF-8 included       */
#define ED_FILE_MAX    (128 * 1024)
#define ED_CLIP_LINES  64

/* ------------------------------------------------------------------ */
/* editor state                                                        */
/* ------------------------------------------------------------------ */

static char **ed_lines = NULL;      /* ED_MAX_LINES pointers           */
static int    ed_nlines = 0;
static int    ed_cy = 0;            /* cursor line                     */
static int    ed_cx = 0;            /* cursor byte offset in the line  */
static int    ed_dirty = 0;
static int    ed_partial = 0;       /* load hit a session limit        */
static char   ed_path[128];
static int    ed_top = 0;           /* viewport top line               */
static int    ed_hoff = 0;          /* horizontal scroll (display col) */

/* selection: mark line/byte; -1 = no mark.  The selection spans
 * [mark..cursor] in buffer order regardless of direction. */
static int    ed_my = -1, ed_mx = 0;

/* clipboard: heap copy of cut/copied lines */
static char (*ed_clip)[ED_LINE_MAX] = NULL;
static int    ed_clip_n = 0;

/* IME state */
static char   ed_pinyin[8];
static int    ed_py_len = 0;
static int    ed_ime_on = 0;        /* 1 = pinyin composition active   */

static int ed_alloc(void) {
    if (ed_lines) return 0;
    ed_lines = kmalloc((u64)sizeof(char *) * ED_MAX_LINES);
    if (!ed_lines) return -1;
    for (int i = 0; i < ED_MAX_LINES; i++) ed_lines[i] = NULL;
    return 0;
}

static void ed_free_lines(void) {
    if (!ed_lines) return;
    for (int i = 0; i < ED_MAX_LINES; i++)
        if (ed_lines[i]) { kfree(ed_lines[i]); ed_lines[i] = NULL; }
    kfree(ed_lines);
    ed_lines = NULL;
    ed_nlines = 0;
}

static void ed_reset(void) {
    ed_free_lines();
    ed_nlines = 0;
    ed_cy = ed_cx = 0;
    ed_dirty = 0;
    ed_partial = 0;
    ed_path[0] = 0;
    ed_top = 0;
    ed_hoff = 0;
    ed_my = -1;
    ed_py_len = 0;
    ed_pinyin[0] = 0;
}

/* ensure line i exists and is allocated */
static int ed_line_grow(int i) {
    if (i < 0 || i >= ED_MAX_LINES) return -1;
    if (!ed_lines[i]) {
        ed_lines[i] = kmalloc(ED_LINE_MAX);
        if (!ed_lines[i]) return -1;
        ed_lines[i][0] = 0;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* UTF-8 helpers (byte offset <-> display column)                      */
/* ------------------------------------------------------------------ */

/* display columns the first `len` bytes of `s` occupy */
static int ed_cols(const char *s, int len) {
    int cols = 0, i = 0;
    while (i < len && s[i]) {
        u32 cp, used = 0;
        if (encoding_utf8_decode((const u8 *)(s + i), (u32)(len - i),
                                 &cp, &used) == 0 && used > 0) {
            cols += (int)render_chinese_cols(cp);
            i += (int)used;
        } else {
            cols += 1;
            i += 1;
        }
    }
    return cols;
}

/* byte offset of display column `want` inside the line (clamped) */
static int ed_byte_at_col(const char *s, int want) {
    int cols = 0, i = 0;
    while (s[i]) {
        u32 cp, used = 0;
        int w;
        if (encoding_utf8_decode((const u8 *)(s + i), 4, &cp, &used) == 0 &&
            used > 0) {
            w = (int)render_chinese_cols(cp);
        } else {
            w = 1;
            used = 1;
        }
        if (cols + w > want) return i;      /* cursor lands before glyph */
        cols += w;
        i += (int)used;
    }
    return i;
}

/* byte length of the character starting at s[i] (>=1) */
static int ed_char_len(const char *s, int i) {
    u32 cp, used = 0;
    if (encoding_utf8_decode((const u8 *)(s + i), 4, &cp, &used) == 0 && used > 0)
        return (int)used;
    return 1;
}

/* byte length of the character BEFORE s[i] (>=1) */
static int ed_char_len_prev(const char *s, int i) {
    int p = i - 1;
    while (p > 0 && ((u8)s[p] & 0xC0) == 0x80) p--;   /* UTF-8 lead byte */
    return i - p;
}

static int ed_line_len(int i) {
    return ed_lines[i] ? (int)strlen(ed_lines[i]) : 0;
}

/* ------------------------------------------------------------------ */
/* buffer edit primitives                                              */
/* ------------------------------------------------------------------ */

/* insert `ins` (a UTF-8 string, byte length n) at (y, x); returns 0 ok */
static int ed_insert(int y, int x, const char *ins, int n) {
    if (y < 0 || y >= ED_MAX_LINES) return -1;
    if (ed_line_grow(y) != 0) return -1;
    char *L = ed_lines[y];
    int len = (int)strlen(L);
    if (x < 0 || x > len) return -1;
    if (len + n + 1 > ED_LINE_MAX) {
        /* would overflow the line: split the tail onto a new line when
         * possible; otherwise refuse (honest limit) */
        return -1;
    }
    for (int i = len; i >= x; i--) L[i + n] = L[i];
    for (int i = 0; i < n; i++) L[x + i] = ins[i];
    ed_dirty = 1;
    return 0;
}

/* insert a new empty line at y (lines below shift down) */
static int ed_insert_line(int y) {
    if (y < 0 || y > ed_nlines || y >= ED_MAX_LINES) return -1;
    if (ed_nlines >= ED_MAX_LINES) return -1;
    if (ed_line_grow(ed_nlines) != 0) return -1;
    for (int i = ed_nlines; i > y; i--) {
        char *t = ed_lines[i];
        ed_lines[i] = ed_lines[i - 1];
        ed_lines[i - 1] = t;
    }
    if (ed_line_grow(y) != 0) return -1;
    ed_lines[y][0] = 0;
    ed_nlines++;
    ed_dirty = 1;
    return 0;
}

/* remove line y (lines below shift up) */
static void ed_remove_line(int y) {
    if (y < 0 || y >= ed_nlines) return;
    if (ed_lines[y]) { kfree(ed_lines[y]); ed_lines[y] = NULL; }
    for (int i = y; i < ed_nlines - 1; i++)
        ed_lines[i] = ed_lines[i + 1];
    ed_lines[ed_nlines - 1] = NULL;
    ed_nlines--;
    ed_dirty = 1;
}

/* delete the character at (y, x); merges with the next line at EOL */
static int ed_del_char(int y, int x) {
    if (y < 0 || y >= ed_nlines) return -1;
    char *L = ed_lines[y];
    int len = ed_line_len(y);
    if (x < len) {
        int cl = ed_char_len(L, x);
        for (int i = x; i <= len - cl; i++) L[i] = L[i + cl];
        ed_dirty = 1;
        return 0;
    }
    if (y + 1 < ed_nlines) {           /* merge next line into this one */
        char *N = ed_lines[y + 1];
        int nl = (int)strlen(N);
        if (len + nl + 1 <= ED_LINE_MAX) {
            strncpy(L + len, N, (usize)(ED_LINE_MAX - len - 1));
            L[ED_LINE_MAX - 1] = 0;
            ed_remove_line(y + 1);
            return 0;
        }
        return -1;                      /* merged line would overflow */
    }
    return 0;                           /* deleting past last char: nop */
}

/* split line y at byte x; the tail becomes line y+1 */
static int ed_split(int y, int x) {
    if (y < 0 || y >= ed_nlines) return -1;
    if (ed_insert_line(y + 1) != 0) return -1;
    char *L = ed_lines[y];
    char *N = ed_lines[y + 1];
    int len = ed_line_len(y);
    if (x < 0 || x > len) x = len;
    strncpy(N, L + x, (usize)(ED_LINE_MAX - 1));
    N[ED_LINE_MAX - 1] = 0;
    L[x] = 0;
    ed_dirty = 1;
    return 0;
}

/* delete the text between (y0,x0) and (y1,x1) in buffer order; the
 * remaining text is joined.  Returns 0 ok. */
static int ed_delete_range(int y0, int x0, int y1, int x1) {
    if (y0 == y1) {
        char *L = ed_lines[y0];
        int len = ed_line_len(y0);
        if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
        if (x1 > len) x1 = len;
        for (int i = x0; i <= len - (x1 - x0); i++)
            L[i] = L[i + (x1 - x0)];
        ed_dirty = 1;
        return 0;
    }
    /* multi-line: join head of y0 with tail of y1, drop lines between */
    char *H = ed_lines[y0];
    char *T = ed_lines[y1];
    int hl = ed_line_len(y0), tl = ed_line_len(y1);
    if (x0 > hl) x0 = hl;
    if (x1 > tl) x1 = tl;
    if (x0 + (tl - x1) + 1 > ED_LINE_MAX) return -1;
    char joined[ED_LINE_MAX];
    memcpy(joined, H, (usize)x0);
    joined[x0] = 0;
    /* bounded append of the tail (kernel lib has no strncat) */
    {
        usize len = (usize)x0, k = (usize)x1;
        while (T[k] && len + 1 < (usize)ED_LINE_MAX)
            joined[len++] = T[k++];
        joined[len] = 0;
    }
    strncpy(H, joined, (usize)(ED_LINE_MAX - 1));
    H[ED_LINE_MAX - 1] = 0;
    for (int y = y1; y > y0; y--) ed_remove_line(y);
    ed_dirty = 1;
    return 0;
}

/* ------------------------------------------------------------------ */
/* clipboard                                                           */
/* ------------------------------------------------------------------ */

static int ed_clip_alloc(void) {
    if (ed_clip) return 0;
    ed_clip = kmalloc((u64)ED_CLIP_LINES * ED_LINE_MAX);
    if (!ed_clip) return -1;
    ed_clip_n = 0;
    return 0;
}

/* copy [mark..cursor] into the clipboard (normalized buffer order) */
static int ed_clip_copy(int *out_lines) {
    if (ed_clip_alloc() != 0) return -1;
    int y0 = ed_my, x0 = ed_mx, y1 = ed_cy, x1 = ed_cx;
    if (y0 > y1 || (y0 == y1 && x0 > x1)) {
        int ty = y0; y0 = y1; y1 = ty;
        int tx = x0; x0 = x1; x1 = tx;
    }
    int n = 0;
    if (y0 == y1) {
        if (x1 > ed_line_len(y0)) x1 = ed_line_len(y0);
        if (x1 > x0 && n < ED_CLIP_LINES) {
            int l = x1 - x0;
            if (l > ED_LINE_MAX - 1) l = ED_LINE_MAX - 1;
            memcpy(ed_clip[n], ed_lines[y0] + x0, (usize)l);
            ed_clip[n][l] = 0;
            n++;
        }
    } else {
        if (n < ED_CLIP_LINES) {
            strncpy(ed_clip[n], ed_lines[y0] + x0, ED_LINE_MAX - 1);
            ed_clip[n][ED_LINE_MAX - 1] = 0;
            n++;
        }
        for (int y = y0 + 1; y < y1 && n < ED_CLIP_LINES; y++) {
            strncpy(ed_clip[n], ed_lines[y], ED_LINE_MAX - 1);
            ed_clip[n][ED_LINE_MAX - 1] = 0;
            n++;
        }
        if (n < ED_CLIP_LINES) {
            if (x1 > ed_line_len(y1)) x1 = ed_line_len(y1);
            ed_clip[n][x1] = 0;
            ed_clip[n][ED_LINE_MAX - 1] = 0;
            n++;
        }
    }
    ed_clip_n = n;
    if (out_lines) *out_lines = n;
    return 0;
}

/* paste the clipboard at the cursor; multi-line paste splits lines */
static int ed_clip_paste(void) {
    if (!ed_clip || ed_clip_n == 0) return -1;
    for (int i = 0; i < ed_clip_n; i++) {
        if (i == 0) {
            int n = (int)strlen(ed_clip[i]);
            if (ed_insert(ed_cy, ed_cx, ed_clip[i], n) != 0) return -1;
            ed_cx += n;
        } else {
            if (ed_split(ed_cy, ed_cx) != 0) return -1;
            ed_cy++;
            int n = (int)strlen(ed_clip[i]);
            if (n > 0) {
                if (ed_insert(ed_cy, 0, ed_clip[i], n) != 0) return -1;
            }
            ed_cx = n;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* file I/O (real VFS)                                                 */
/* ------------------------------------------------------------------ */

/* load path into the buffer; returns 0 ok, <0 error.  Partial loads
 * (beyond session limits) set ed_partial and refuse saves. */
static int ed_load(const char *path) {
    int fd = fs_vfs_open(path, VFS_O_RDONLY);
    if (fd < 0) return -1;
    char *buf = kmalloc(ED_FILE_MAX + 1);
    if (!buf) { fs_vfs_close(fd); return -2; }
    int total = 0;
    for (;;) {
        int n = fs_vfs_read(fd, buf + total, ED_FILE_MAX - total);
        if (n < 0) { kfree(buf); fs_vfs_close(fd); return -3; }
        if (n == 0) break;
        total += n;
        if (total >= ED_FILE_MAX) break;
    }
    fs_vfs_close(fd);
    buf[total] = 0;

    ed_reset();
    if (ed_alloc() != 0) { kfree(buf); return -2; }
    int y = 0, x = 0, dropped_lines = 0, dropped_chars = 0;
    if (ed_line_grow(0) != 0) { kfree(buf); return -2; }
    for (int i = 0; i < total; i++) {
        if (buf[i] == '\n') {
            y++;
            x = 0;
            if (y >= ED_MAX_LINES) { y = ED_MAX_LINES - 1; dropped_lines++; break; }
            if (ed_line_grow(y) != 0) break;
        } else if (buf[i] != '\r') {
            if (x + 1 >= ED_LINE_MAX) { dropped_chars++; continue; }
            ed_lines[y][x++] = buf[i];
            ed_lines[y][x] = 0;
        }
    }
    ed_nlines = y + 1;
    ed_partial = (dropped_lines > 0 || dropped_chars > 0 ||
                  total >= ED_FILE_MAX) ? 1 : 0;
    strncpy(ed_path, path, sizeof(ed_path) - 1);
    ed_path[sizeof(ed_path) - 1] = 0;
    kfree(buf);
    return 0;
}

/* write the buffer to path; returns 0 ok */
static int ed_save(const char *path) {
    if (ed_partial) return -10;            /* refuse partial-view saves */
    int fd = fs_vfs_open(path, VFS_O_WRONLY | VFS_O_CREAT | VFS_O_TRUNC);
    if (fd < 0) return -1;
    for (int y = 0; y < ed_nlines; y++) {
        int len = ed_line_len(y);
        if (len > 0) {
            int w = fs_vfs_write(fd, ed_lines[y], len);
            if (w != len) { fs_vfs_close(fd); return -2; }
        }
        if (y + 1 < ed_nlines || ed_line_len(y) > 0) {
            /* newline after every line except a final empty one */
            if (y + 1 < ed_nlines) {
                if (fs_vfs_write(fd, "\n", 1) != 1) {
                    fs_vfs_close(fd);
                    return -3;
                }
            }
        }
    }
    fs_vfs_close(fd);
    strncpy(ed_path, path, sizeof(ed_path) - 1);
    ed_path[sizeof(ed_path) - 1] = 0;
    ed_dirty = 0;
    return 0;
}

/* ------------------------------------------------------------------ */
/* UI: prompt for a path (small form over the editor page)             */
/* ------------------------------------------------------------------ */

static int ed_prompt_path(const char *title_en, const char *title_zh,
                          char *out, int cap) {
    int rows = tui_dt_page_rows(), cols = tui_dt_page_cols();
    tui_window_t *fwin = tui_window_new(rows / 2 - 3, (cols - 60) / 2, 5, 60);
    if (!fwin) return -1;
    tui_window_focus_set(fwin);
    tui_window_clear(fwin);
    tui_window_border(fwin, &TUI_BORDER_DOUBLE);
    tui_set_attr(TUI_MAKE_ATTR(TUI_COLOR_WHITE, TUI_COLOR_TEAL) | TUI_ATTR_BOLD);
    tui_print_at(0, 2, tui_tr(title_en, title_zh));
    tui_field_t f[1];
    memset(f, 0, sizeof(f));
    f[0].y = 2; f[0].x = 2; f[0].width = 54;
    f[0].editable = 1; f[0].label = "";
    tui_form_t *form = tui_form_new(fwin, f, 1);
    tui_form_set_field(form, 0, out);
    tui_form_post(form);
    tui_window_refresh(fwin);
    int rc = -1;
    for (;;) {
        int k = tui_getch();
        if (k == TUI_KEY_ESC) break;
        int d = tui_form_driver(form, k);
        if (d == 1) {
            const char *v = tui_form_field_buffer(form, 0);
            strncpy(out, v ? v : "", cap - 1);
            out[cap - 1] = 0;
            rc = 0;
            break;
        }
        if (d == -1) break;
        tui_window_refresh(fwin);
    }
    tui_form_unpost(form);
    tui_window_free(fwin);
    tui_window_t *pg = tui_dt_page_window();
    if (pg) tui_window_focus_set(pg);
    return rc;
}

/* ------------------------------------------------------------------ */
/* UI: rendering                                                       */
/* ------------------------------------------------------------------ */

/* selection test for byte range [x0,x1) of line y (partial overlap
 * counts - the standard selection semantic) */
static int ed_sel_hit(int y, int x0, int x1) {
    if (ed_my < 0) return 0;
    int y0 = ed_my, x0s = ed_mx, y1 = ed_cy, x1s = ed_cx;
    if (y0 > y1 || (y0 == y1 && x0s > x1s)) {
        int ty = y0; y0 = y1; y1 = ty;
        int tx = x0s; x0s = x1s; x1s = tx;
    }
    if (y < y0 || y > y1) return 0;
    if (y == y0 && y == y1) return x1 > x0s && x0 < x1s;
    if (y == y0) return x1 > x0s;
    if (y == y1) return x0 < x1s;
    return 1;
}

static void ed_render(void) {
    int rows = tui_dt_page_rows(), cols = tui_dt_page_cols();
    /* title bar */
    tui_attr_t hdr = TUI_MAKE_ATTR(TUI_COLOR_WHITE, TUI_COLOR_TEAL) | TUI_ATTR_BOLD;
    tui_set_attr(hdr);
    for (int x = 0; x < cols; x++) tui_print_at(0, x, " ");
    {
        char t[96];
        strncpy(t, " ", sizeof(t) - 1);
        tui_dt_str_append(t, (int)sizeof(t), ed_path[0] ? ed_path
                                : tui_tr("(new file)", "(新文件)"));
        if (ed_dirty) tui_dt_str_append(t, (int)sizeof(t), " *");
        if (ed_partial)
            tui_dt_str_append(t, (int)sizeof(t),
                              tui_tr(" [PARTIAL: save refused]",
                                     " [部分加载：拒绝保存]"));
        tui_print_at(0, 1, t);
        /* right: input mode */
        const char *mode = ed_ime_on ? tui_tr("[CN pinyin]", "[中文拼音]")
                                     : "[EN]";
        tui_print_at(0, cols - 14, mode);
    }
    tui_set_attr(tui_default_attr());

    /* body: viewport rows 1..rows-3 */
    int body_top = 1, body_rows = rows - 3 - body_top;
    if (body_rows < 1) body_rows = 1;
    for (int r = body_top; r < body_top + body_rows && r < rows - 3; r++) {
        tui_set_attr(tui_default_attr());
        for (int x = 0; x < cols; x++) tui_print_at(r, x, " ");
    }
    if (ed_nlines == 0 && ed_line_grow(0) == 0) ed_nlines = 1;

    tui_attr_t norm = TUI_MAKE_ATTR(TUI_COLOR_SILVER, TUI_COLOR_BLACK);
    tui_attr_t sel  = TUI_MAKE_ATTR(TUI_COLOR_BLACK, TUI_COLOR_SILVER)
                      | TUI_ATTR_BOLD;
    for (int i = 0; i < body_rows; i++) {
        int y = ed_top + i;
        if (y >= ed_nlines) break;
        int row = body_top + i;
        char *L = ed_lines[y] ? ed_lines[y] : (char *)"";
        int len = (int)strlen(L);
        /* render with horizontal scroll + selection segmentation */
        tui_set_attr(norm);
        int col = 0, x = 0;
        while (x < len) {
            int cl = ed_char_len(L, x);
            int w = ed_cols(L + x, cl);
            if (col + w > ed_hoff + cols - 2) break;
            if (col + w > ed_hoff) {
                int in_sel = ed_sel_hit(y, x, x + cl);
                tui_set_attr(in_sel ? sel : norm);
                char piece[8];
                for (int k = 0; k < cl && k < 7; k++) piece[k] = L[x + k];
                piece[cl > 7 ? 7 : cl] = 0;
                tui_print_at(row, col - ed_hoff + 1, piece);
            }
            col += w;
            x += cl;
        }
        /* cursor row underline hint: draw cursor cell highlight when
         * the cursor is on this row (reverse block under the glyph) */
        if (y == ed_cy) {
            int ccol = ed_cols(L, ed_cx > len ? len : ed_cx) - ed_hoff;
            if (ccol >= 0 && ccol < cols - 2) {
                tui_set_attr(TUI_MAKE_ATTR(TUI_COLOR_WHITE, TUI_COLOR_NAVY)
                             | TUI_ATTR_BOLD);
                tui_print_at(row, ccol + 1, " ");
            }
        }
    }
    tui_set_attr(tui_default_attr());

    /* status row (rows-3): path cursor state */
    {
        char st[112], num[16];
        strncpy(st, tui_tr(" Ln ", " 行 "), sizeof(st) - 1);
        u64_to_str((u64)(ed_cy + 1), num);
        tui_dt_str_append(st, (int)sizeof(st), num);
        tui_dt_str_append(st, (int)sizeof(st), tui_tr(", Col ", ", 列 "));
        u64_to_str((u64)(ed_cols(ed_lines[ed_cy] ? ed_lines[ed_cy] : (char *)"",
                                ed_cx) + 1), num);
        tui_dt_str_append(st, (int)sizeof(st), num);
        tui_dt_str_append(st, (int)sizeof(st), " / ");
        u64_to_str((u64)ed_nlines, num);
        tui_dt_str_append(st, (int)sizeof(st), num);
        tui_dt_str_append(st, (int)sizeof(st),
                          tui_tr(" lines  ", " 行  "));
        if (ed_my >= 0)
            tui_dt_str_append(st, (int)sizeof(st),
                              tui_tr(" [SEL]", " [选区]"));
        tui_attr_t sa = TUI_MAKE_ATTR(TUI_COLOR_AQUA, TUI_COLOR_BLACK);
        tui_set_attr(sa);
        for (int x = 0; x < cols; x++) tui_print_at(rows - 3, x, " ");
        tui_print_at(rows - 3, 1, st);
        tui_set_attr(tui_default_attr());
    }
    /* IME composition row (rows-2) */
    tui_set_attr(tui_default_attr());
    for (int x = 0; x < cols; x++) tui_print_at(rows - 2, x, " ");
    if (ed_py_len > 0) {
        char comp[64];
        strncpy(comp, tui_tr(" pinyin [", " 拼音 ["), sizeof(comp) - 1);
        tui_dt_str_append(comp, (int)sizeof(comp), ed_pinyin);
        tui_dt_str_append(comp, (int)sizeof(comp), "]  ");
        u32 cands[9];
        int nc = tui_chinese_ime_test(ed_pinyin, cands, 9);
        for (int i = 0; i < nc && i < 9; i++) {
            char num[4];
            num[0] = (char)('1' + i);
            num[1] = ' ';
            num[2] = 0;
            u8 u8b[4];
            u32 produced = 0;
            if (encoding_utf8_encode(cands[i], u8b, 4, &produced) == 0) {
                for (u32 b = 0; b < produced && comp[0]; b++) {
                    char one[2] = { (char)u8b[b], 0 };
                    tui_dt_str_append(comp, (int)sizeof(comp), one);
                }
            }
            tui_dt_str_append(comp, (int)sizeof(comp), num);
        }
        tui_attr_t ia = TUI_MAKE_ATTR(TUI_COLOR_YELLOW, TUI_COLOR_BLACK)
                        | TUI_ATTR_BOLD;
        tui_set_attr(ia);
        tui_print_at(rows - 2, 1, comp);
        tui_set_attr(tui_default_attr());
    } else if (ed_ime_on) {
        tui_set_attr(TUI_MAKE_ATTR(TUI_COLOR_GREEN, TUI_COLOR_BLACK));
        tui_print_at(rows - 2, 1,
                     tui_tr(" CN input: type a-z, digits/space pick, "
                            "Backspace edits",
                            " 中文输入：输入 a-z 组拼音，数字/空格选字，"
                            "退格删除字母"));
        tui_set_attr(tui_default_attr());
    }
    /* key bar (rows-1) */
    tui_attr_t bar = TUI_MAKE_ATTR(TUI_COLOR_BLACK, TUI_COLOR_AQUA);
    tui_set_attr(bar);
    for (int x = 0; x < cols; x++) tui_print_at(rows - 1, x, " ");
    tui_print_at(rows - 1, 0, tui_tr(
        " ^O:Save F12:As ^N:New ^X:Exit ^K:CutLn F4:Cn F6:Sel ^C/^X/^V:Clip ^G:?",
        " ^O:保存 F12:另存 ^N:新建 ^X:退出 ^K:删行 F4:中英 F6:选区 ^C/^X/^V:剪贴 ^G:帮助"));
    tui_set_attr(tui_default_attr());
    tui_dt_page_flush();
}

/* ------------------------------------------------------------------ */
/* IME commit                                                          */
/* ------------------------------------------------------------------ */

/* insert candidate `pick` for the current pinyin; 0 ok */
static int ed_ime_pick(int pick) {
    u32 cands[9];
    int nc = tui_chinese_ime_test(ed_pinyin, cands, 9);
    if (nc <= 0 || pick >= nc) return -1;
    u8 u8b[4];
    u32 produced = 0;
    if (encoding_utf8_encode(cands[pick], u8b, 4, &produced) != 0) return -1;
    if (ed_insert(ed_cy, ed_cx, (const char *)u8b, (int)produced) != 0)
        return -1;
    ed_cx += (int)produced;
    ed_py_len = 0;
    ed_pinyin[0] = 0;
    return 0;
}

/* ------------------------------------------------------------------ */
/* help page                                                           */
/* ------------------------------------------------------------------ */

static void ed_help_page(void) {
    if (tui_dt_page_open() != 0) return;
    tui_print_at(0, 1, tui_tr("Editor keys", "编辑器按键"));
    static const struct { const char *k, *en, *zh; } H[] = {
        { "Up/Down/Left/Right", "move the cursor", "移动光标" },
        { "Home/End  ^A/^E",    "line start / end", "行首 / 行尾" },
        { "PgUp/PgDn",          "scroll the viewport", "滚动视口" },
        { "Enter",              "split the line at the cursor", "在光标处拆分行" },
        { "Backspace/Delete",   "delete before / at the cursor", "删除光标前/处的字符" },
        { "^K",                 "delete the current line", "删除当前行" },
        { "^N",                 "new empty buffer", "新建空缓冲区" },
        { "^O",                 "save (asks the path when unnamed)", "保存（未命名时询问路径）" },
        { "F12",                "save as", "另存为" },
        { "^X",                 "exit (confirms when modified)", "退出（有修改时确认）" },
        { "F4",                 "toggle EN / pinyin input", "切换 英文/拼音 输入" },
        { "F6 or ^@",           "set the selection mark", "设置选区标记" },
        { "^C",                 "copy the selection", "复制选区" },
        { "^X with mark",       "cut the selection", "剪切选区" },
        { "^V",                 "paste the clipboard", "粘贴剪贴板" },
        { "^G",                 "this help", "本帮助" },
        { NULL, NULL, NULL },
    };
    int row = 2;
    for (int i = 0; H[i].k; i++) {
        char line[128];
        int n = 0;
        for (const char *p = H[i].k; *p && n < 26; p++) line[n++] = *p;
        while (n < 24) line[n++] = ' ';
        line[n++] = '-'; line[n++] = ' ';
        const char *d = tui_tr(H[i].en, H[i].zh);
        for (const char *p = d; *p && n < 120; p++) line[n++] = *p;
        line[n] = 0;
        tui_print_at(row++, 1, line);
        if (row >= tui_dt_page_rows() - 2) break;
    }
    tui_dt_page_wait_close(NULL);
}

/* ------------------------------------------------------------------ */
/* main loop                                                           */
/* ------------------------------------------------------------------ */

int tui_dt_editor_run(tui_dt_node_t *self) {
    (void)self;

    /* ---- open-or-new prompt (native form over the desktop page) ---- */
    if (tui_dt_page_open() != 0) return -1;
    tui_print_at(0, 1, tui_tr("Text editor (native)", "文本编辑器（原生）"));
    tui_print_at(2, 1, tui_tr(
        "File to edit (created when missing):",
        "要编辑的文件（不存在则新建）："));
    char path[128];
    path[0] = 0;
    if (ed_prompt_path("file path", "文件路径", path, (int)sizeof(path)) != 0 ||
        !path[0]) {
        return 0;                       /* user changed their mind */
    }

    ed_reset();
    if (ed_alloc() != 0) {
        tui_dt_toast(0, tui_tr("out of memory", "内存不足"));
        return -1;
    }
    fs_vfs_stat_t st;
    int exists = fs_vfs_stat(path, &st) == 0;
    if (exists) {
        int rc = ed_load(path);
        if (rc != 0) {
            tui_dt_toast(0, tui_tr("open %s failed (rc=%d)",
                                   "打开 %s 失败 (rc=%d)"), path, rc);
            return rc;
        }
        if (ed_partial)
            tui_dt_toast(0, tui_tr("%s: partial load, save refused",
                                   "%s：部分加载，拒绝保存"), path);
    } else {
        if (ed_line_grow(0) != 0) return -1;
        ed_nlines = 1;
        strncpy(ed_path, path, sizeof(ed_path) - 1);
        ed_path[sizeof(ed_path) - 1] = 0;
    }

    /* default: IME follows the desktop language */
    ed_ime_on = (tui_lang_current() == SHELL_LANG_ZH) ? 1 : 0;

    /* ---- editing loop ---- */
    int quit = 0;
    while (!quit) {
        ed_render();
        int k = tui_getch();

        /* IME first: while composing, letters/digits/space/backspace
         * belong to the composition; Esc drops the composition. */
        if (ed_py_len > 0) {
            if (k >= 'a' && k <= 'z' && ed_py_len < 7) {
                ed_pinyin[ed_py_len++] = (char)k;
                ed_pinyin[ed_py_len] = 0;
                continue;
            }
            if (k >= '1' && k <= '9') {
                ed_ime_pick(k - '1');
                continue;
            }
            if (k == ' ') {
                ed_ime_pick(0);
                continue;
            }
            if (k == TUI_KEY_BACKSPACE) {
                ed_py_len--;
                ed_pinyin[ed_py_len] = 0;
                continue;
            }
            if (k == TUI_KEY_ESC) {
                ed_py_len = 0;
                ed_pinyin[0] = 0;
                continue;
            }
            /* any other key falls through to normal handling */
        }

        switch (k) {
        case TUI_KEY_UP:
            if (ed_cy > 0) {
                ed_cy--;
                ed_cx = ed_byte_at_col(ed_lines[ed_cy] ? ed_lines[ed_cy]
                                                       : (char *)"",
                                       ed_cols(ed_lines[ed_cy + 1] ?
                                               ed_lines[ed_cy + 1] : (char *)"",
                                               ed_cx));
            }
            break;
        case TUI_KEY_DOWN:
            if (ed_cy + 1 < ed_nlines) {
                int want = ed_cols(ed_lines[ed_cy] ? ed_lines[ed_cy]
                                                   : (char *)"", ed_cx);
                ed_cy++;
                ed_cx = ed_byte_at_col(ed_lines[ed_cy] ? ed_lines[ed_cy]
                                                       : (char *)"", want);
            }
            break;
        case TUI_KEY_LEFT:
            if (ed_cx > 0) {
                ed_cx -= ed_char_len_prev(ed_lines[ed_cy], ed_cx);
            } else if (ed_cy > 0) {
                ed_cy--;
                ed_cx = ed_line_len(ed_cy);
            }
            break;
        case TUI_KEY_RIGHT:
            if (ed_cx < ed_line_len(ed_cy)) {
                ed_cx += ed_char_len(ed_lines[ed_cy], ed_cx);
            } else if (ed_cy + 1 < ed_nlines) {
                ed_cy++;
                ed_cx = 0;
            }
            break;
        case TUI_KEY_HOME:
        case TUI_KEY_CTRL_A:
            ed_cx = 0;
            break;
        case TUI_KEY_END:
        case TUI_KEY_CTRL_E:
            ed_cx = ed_line_len(ed_cy);
            break;
        case TUI_KEY_PGUP:
            ed_cy -= 8; if (ed_cy < 0) ed_cy = 0;
            ed_cx = 0;
            break;
        case TUI_KEY_PGDN:
            ed_cy += 8; if (ed_cy >= ed_nlines) ed_cy = ed_nlines - 1;
            ed_cx = 0;
            break;
        case TUI_KEY_ENTER: {
            if (ed_my >= 0) {            /* typing replaces the selection */
                ed_clip_copy(NULL);
                ed_delete_range(ed_my, ed_mx, ed_cy, ed_cx);
                ed_my = -1;
                ed_cx = ed_line_len(ed_cy) < ed_cx ? ed_line_len(ed_cy) : ed_cx;
            }
            ed_split(ed_cy, ed_cx);
            ed_cy++;
            ed_cx = 0;
            break;
        }
        case TUI_KEY_BACKSPACE: {
            if (ed_my >= 0) {            /* backspace deletes the selection */
                ed_clip_copy(NULL);
                int y0 = ed_my, x0 = ed_mx, y1 = ed_cy, x1 = ed_cx;
                if (y0 > y1 || (y0 == y1 && x0 > x1)) {
                    int t; t = y0; y0 = y1; y1 = t;
                    t = x0; x0 = x1; x1 = t;
                }
                ed_delete_range(y0, x0, y1, x1);
                ed_my = -1;
                ed_cy = y0;
                ed_cx = x0;
                break;
            }
            if (ed_cx > 0) {
                ed_cx -= ed_char_len_prev(ed_lines[ed_cy], ed_cx);
                ed_del_char(ed_cy, ed_cx);
            } else if (ed_cy > 0) {
                ed_cy--;
                ed_cx = ed_line_len(ed_cy);
                ed_del_char(ed_cy, ed_cx);   /* merge next line up */
            }
            break;
        }
        case TUI_KEY_DELETE:
            ed_del_char(ed_cy, ed_cx);
            break;
        case TUI_KEY_CTRL_K:            /* delete the current line */
            if (ed_nlines > 1) {
                ed_remove_line(ed_cy);
                if (ed_cy >= ed_nlines) ed_cy = ed_nlines - 1;
                if (ed_cx > ed_line_len(ed_cy)) ed_cx = ed_line_len(ed_cy);
            } else {
                ed_lines[0][0] = 0;
                ed_cx = 0;
                ed_dirty = 1;
            }
            ed_my = -1;
            break;
        case TUI_KEY_CTRL_N: {          /* new buffer */
            if (ed_dirty) {
                int c = tui_dt_confirm("discard unsaved changes?",
                                       "放弃未保存的修改？");
                if (c != 1) break;
            }
            ed_reset();
            ed_alloc();
            ed_line_grow(0);
            ed_nlines = 1;
            break;
        }
        case TUI_KEY_CTRL_O: {          /* save / save-with-prompt */
            if (ed_partial) {
                tui_dt_toast(0, tui_tr("partial buffer: saving refused",
                                       "部分缓冲区：拒绝保存"));
                break;
            }
            char p[128];
            strncpy(p, ed_path, sizeof(p) - 1);
            p[sizeof(p) - 1] = 0;
            if (!p[0]) {
                if (ed_prompt_path("save as", "另存为", p, (int)sizeof(p)) != 0
                    || !p[0])
                    break;
            }
            int rc = ed_save(p);
            tui_dt_toast(rc == 0, tui_tr("save %s rc=%d", "保存 %s rc=%d"),
                         p, rc);
            break;
        }
        case TUI_KEY_F(12): {           /* save as */
            if (ed_partial) {
                tui_dt_toast(0, tui_tr("partial buffer: saving refused",
                                       "部分缓冲区：拒绝保存"));
                break;
            }
            char p[128];
            if (ed_prompt_path("save as", "另存为", p, (int)sizeof(p)) != 0
                || !p[0])
                break;
            int rc = ed_save(p);
            tui_dt_toast(rc == 0, tui_tr("save %s rc=%d", "保存 %s rc=%d"),
                         p, rc);
            break;
        }
        case TUI_KEY_CTRL_X: {          /* exit (or cut with a selection) */
            if (ed_my >= 0) {           /* cut */
                int n = 0;
                ed_clip_copy(&n);
                int y0 = ed_my, x0 = ed_mx, y1 = ed_cy, x1 = ed_cx;
                if (y0 > y1 || (y0 == y1 && x0 > x1)) {
                    int t; t = y0; y0 = y1; y1 = t;
                    t = x0; x0 = x1; x1 = t;
                }
                ed_delete_range(y0, x0, y1, x1);
                ed_my = -1;
                ed_cy = y0;
                ed_cx = x0;
                tui_dt_toast(1, tui_tr("cut %d lines", "已剪切 %d 行"), n);
                break;
            }
            if (ed_dirty) {
                int c = tui_dt_confirm("save before exiting? (y=save)",
                                       "退出前保存修改？（y=保存）");
                if (c == 1) {
                    char p[128];
                    strncpy(p, ed_path, sizeof(p) - 1);
                    p[sizeof(p) - 1] = 0;
                    if (!p[0] &&
                        ed_prompt_path("save as", "另存为", p,
                                       (int)sizeof(p)) != 0)
                        break;
                    if (p[0]) ed_save(p);
                } else if (c < 0) {
                    break;              /* cancelled */
                }
            }
            quit = 1;
            break;
        }
        case TUI_KEY_CTRL_C: {          /* copy selection */
            if (ed_my >= 0) {
                int n = 0;
                ed_clip_copy(&n);
                tui_dt_toast(1, tui_tr("copied %d lines", "已复制 %d 行"), n);
            } else {
                tui_dt_toast(0, tui_tr("no selection (F6 marks it)",
                                       "无选区（F6 设置标记）"));
            }
            break;
        }
        case TUI_KEY_CTRL_V: {          /* paste */
            if (ed_my >= 0) {           /* paste replaces the selection */
                ed_delete_range(ed_my, ed_mx, ed_cy, ed_cx);
                ed_my = -1;
            }
            int rc = ed_clip_paste();
            if (rc != 0)
                tui_dt_toast(0, tui_tr("clipboard empty", "剪贴板为空"));
            break;
        }
        case TUI_KEY_CTRL_G:
            ed_help_page();
            tui_dt_page_open();         /* reopen the editor page */
            break;
        case TUI_KEY_F(4):
            ed_ime_on = !ed_ime_on;
            ed_py_len = 0;
            ed_pinyin[0] = 0;
            break;
        case TUI_KEY_F(6):
        case 0x00: {                    /* ^@ = Ctrl-Space */
            if (ed_my < 0) {
                ed_my = ed_cy;
                ed_mx = ed_cx;
            } else {
                ed_my = -1;
            }
            break;
        }
        case TUI_KEY_ESC:
            ed_my = -1;
            ed_py_len = 0;
            ed_pinyin[0] = 0;
            break;
        default: {
            if (k >= 0x20 && k < 0x7F) {
                if (ed_ime_on && k >= 'a' && k <= 'z') {
                    /* start a pinyin composition */
                    ed_pinyin[ed_py_len++] = (char)k;
                    ed_pinyin[ed_py_len] = 0;
                    break;
                }
                if (ed_my >= 0) {        /* typing replaces the selection */
                    ed_delete_range(ed_my, ed_mx, ed_cy, ed_cx);
                    ed_my = -1;
                }
                char one[2] = { (char)k, 0 };
                if (ed_insert(ed_cy, ed_cx, one, 1) == 0) ed_cx++;
            }
            break;
        }
        }

        /* clamp the cursor after any structural change */
        if (ed_cy >= ed_nlines) ed_cy = ed_nlines - 1;
        if (ed_cy < 0) ed_cy = 0;
        int llen = ed_line_len(ed_cy);
        if (ed_cx > llen) ed_cx = llen;
        if (ed_cx < 0) ed_cx = 0;

        /* keep the viewport on screen */
        int body_rows = tui_dt_page_rows() - 4;
        if (body_rows < 1) body_rows = 1;
        if (ed_cy < ed_top) ed_top = ed_cy;
        if (ed_cy >= ed_top + body_rows) ed_top = ed_cy - body_rows + 1;
        if (ed_top < 0) ed_top = 0;
        /* horizontal scroll: keep the cursor column visible */
        {
            char *L = ed_lines[ed_cy] ? ed_lines[ed_cy] : (char *)"";
            int ccol = ed_cols(L, ed_cx);
            int width = tui_dt_page_cols() - 3;
            if (ccol < ed_hoff) ed_hoff = ccol;
            if (ccol >= ed_hoff + width) ed_hoff = ccol - width + 1;
            if (ed_hoff < 0) ed_hoff = 0;
        }
    }

    /* report what happened through the desktop toast */
    if (ed_path[0])
        tui_dt_toast(1, tui_tr("editor closed: %s", "编辑器已关闭：%s"),
                     ed_path);
    ed_reset();
    return 0;
}
