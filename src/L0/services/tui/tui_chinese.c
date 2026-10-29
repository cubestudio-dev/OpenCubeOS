/* SPDX-License-Identifier: Apache-2.0
 * [3.0.0-WP04 G11c: revived verbatim from v2.2.0 chinese/tui_chinese.c (the Chinese
 *  twin pages the language-aware tui_cmd.c dispatchers launch); include
 *  paths flattened, shell_lang->disp_lang renamed (see tui_exec.h).]
 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-10-chinese_support
 * File: chinese/tui_chinese.c
 * Purpose: TUI Chinese layer: UTF-8 windows/printing, CJK-aware
 *          menu/form drivers, and the pinyin IME.
 */
#include "tui_chinese.h"
#include "encoding.h"
#include "render_cn.h"
#include "lib_string.h"

int tui_chinese_init(void) {
    render_chinese_init();
    return tui_started() ? 0 : tui_init();
}

tui_window_t* tui_chinese_window(const char* utf8_title,
                                 int y, int x, int h, int w,
                                 int bordered) {
    tui_window_t* win = tui_window_new(y, x, h, w);
    if (!win) return NULL;
    if (utf8_title) tui_window_title(win, utf8_title);
    (void)bordered;   /* border style is carried by the window itself */
    return win;
}

int tui_chinese_print(const char* utf8) {
    return tui_print(utf8);   /* tui_print decodes UTF-8 (see tui_text.c) */
}

int tui_chinese_print_at(int y, int x, const char* utf8) {
    return tui_print_at(y, x, utf8);
}

u32 tui_chinese_str_cols(const char* utf8) {
    if (!utf8) return 0;
    u32 cols = 0;
    const u8* p = (const u8*)utf8;
    while (*p) {
        u32 cp, consumed;
        if (encoding_utf8_decode(p, 4, &cp, &consumed) == 0) {
            cols += render_chinese_cols(cp);
            p += consumed;
        } else {
            cols += 1;
            p += 1;
        }
    }
    return cols;
}

/* -------------------------------------------------------------- menu */

/* CJK-aware menu driver: same interaction contract as tui_menu_post +
 * tui_menu_driver, but row padding uses display columns so highlighted
 * bars cover the full width with mixed-width text. */
int tui_chinese_menu(tui_window_t* win, tui_menu_item_t* items,
                     int count) {
    if (!win || !items || count <= 0) return -1;
    int current = 0;
    int top = 0;
    for (;;) {
        tui_window_clear(win);
        if (current < top) top = current;
        if (current >= top + win->h) top = current - win->h + 1;
        tui_attr_t fore = (tui_attr_t)TUI_MAKE_ATTR(TUI_COLOR_SILVER, TUI_COLOR_BLACK);
        tui_attr_t back = (tui_attr_t)TUI_MAKE_ATTR(TUI_COLOR_BLACK, TUI_COLOR_TEAL) |
                          TUI_ATTR_BOLD;
        for (int r = 0; r < win->h; r++) {
            int idx = top + r;
            if (idx >= count) break;
            tui_attr_t a = (idx == current) ? back : fore;
            tui_attr_t save = win->attr;
            win->attr = a;
            tui_window_focus_set(win);
            tui_move(r, 1);
            const char* name = items[idx].name;
            tui_print(name ? name : "");
            if (idx == current) {
                u32 used = tui_chinese_str_cols(name ? name : "");
                for (u32 i = used + 1; i + 1 < (u32)win->w; i++) tui_addch(' ');
            }
            win->attr = save;
        }
        if (items[current].desc) {
            tui_status_line("%s", items[current].desc);
        }
        tui_window_refresh(win);

        int k = tui_getch();
        switch (k) {
        case TUI_KEY_UP:
        case TUI_KEY_CTRL_P:
            if (current > 0) current--;
            break;
        case TUI_KEY_DOWN:
        case TUI_KEY_CTRL_N:
            if (current + 1 < count) current++;
            break;
        case TUI_KEY_PGUP:
            current = (current - win->h < 0) ? 0 : current - win->h;
            break;
        case TUI_KEY_PGDN: {
            int nx = current + win->h;
            current = (nx >= count) ? count - 1 : nx;
            break;
        }
        case TUI_KEY_HOME:
            current = 0;
            break;
        case TUI_KEY_END:
            current = count - 1;
            break;
        case TUI_KEY_ENTER:
            return current;
        case TUI_KEY_ESC:
        case 'q':
            return -1;
        default:
            break;
        }
    }
}

/* -------------------------------------------------------------- form */

/* Field paint with UTF-8 labels: label at (f->y, f->x), value box after
 * the label's display width. */
static void cn_paint_field(tui_window_t* win, tui_field_t* f) {
    tui_attr_t label_a = (tui_attr_t)TUI_MAKE_ATTR(TUI_COLOR_AQUA, TUI_COLOR_BLACK);
    tui_attr_t value_a = (tui_attr_t)TUI_MAKE_ATTR(TUI_COLOR_WHITE, TUI_COLOR_GRAY);
    tui_attr_t save = win->attr;
    win->attr = label_a;
    tui_move(f->y, f->x);
    tui_print(f->label ? f->label : "");
    u32 lab_cols = tui_chinese_str_cols(f->label ? f->label : "");
    win->attr = value_a;
    tui_move(f->y, f->x + (int)lab_cols);
    for (int i = 0; i < f->width; i++) tui_addch(' ');
    tui_move(f->y, f->x + (int)lab_cols);
    /* value may hold CJK: print through the UTF-8 path */
    char savech = 0;
    if (f->len < (int)sizeof(f->buffer)) {
        savech = f->buffer[f->len];
        f->buffer[f->len] = 0;
    }
    tui_print(f->buffer);
    if (savech || f->len < (int)sizeof(f->buffer)) {
        f->buffer[f->len] = savech;
    }
    /* cursor: land after the last glyph of the value */
    if (f->editable) {
        int cx = f->x + (int)lab_cols + (int)tui_chinese_str_cols(f->buffer);
        tui_move(f->y, cx);
    }
    win->attr = save;
}

int tui_chinese_form(tui_window_t* win, tui_field_t* fields, int count) {
    if (!win || !fields || count <= 0) return -1;
    int current = 0;
    for (;;) {
        tui_window_clear(win);
        for (int i = 0; i < count; i++) {
            cn_paint_field(win, &fields[i]);
        }
        tui_status_line("Tab/上下键：切换字段 | 回车：提交 | Esc：取消");
        tui_window_refresh(win);

        int k = tui_getch();
        tui_field_t* f = &fields[current];
        switch (k) {
        case TUI_KEY_TAB:
        case TUI_KEY_CTRL_N:
        case TUI_KEY_DOWN:
            current = (current + 1) % count;
            break;
        case TUI_KEY_UP:
        case TUI_KEY_CTRL_P:
            current = (current + count - 1) % count;
            break;
        case TUI_KEY_ESC:
            return -1;
        case TUI_KEY_ENTER:
            if (current == count - 1) return 0;
            current++;
            break;
        case TUI_KEY_BACKSPACE:
            if (f->cursor > 0) {
                /* character-aware delete (UTF-8) */
                int p = f->cursor - 1;
                while (p > 0 && ((u8)f->buffer[p] & 0xC0) == 0x80) p--;
                int removed = f->cursor - p;
                for (int i = p; i <= f->len - removed; i++) {
                    f->buffer[i] = f->buffer[i + removed];
                }
                f->cursor = p;
                f->len -= removed;
                f->buffer[f->len] = 0;
            }
            break;
        default:
            if (k >= 0x20 && k < 0x7F && f->editable &&
                f->len < TUI_FIELD_MAX && f->cursor < f->width) {
                for (int i = f->len; i > f->cursor; i--) {
                    f->buffer[i] = f->buffer[i - 1];
                }
                f->buffer[f->cursor++] = (char)k;
                f->len++;
                f->buffer[f->len] = 0;
            }
            break;
        }
    }
}

/* --------------------------------------------------------------- IME */
/* 3.0.0-WP04 G11b: the engine bodies (tables + ime_match/ime_candidates
 * + tui_chinese_ime_syllables/chars/test) moved into
 * services/display/ime_engine.[ch] when the ime/ime-test commands were
 * revived; the compose pad below consumes the exported
 * ime_match()/ime_candidates() and the public tui_chinese_ime_* API.
 * (The v2.2.0 file carried the section inline; keeping both would be
 * a multiple-definition at link.) */
#include "ime_engine.h"

int tui_chinese_input(tui_window_t* win, const char* prompt,
                      char* buf, int cap) {
    if (!win || !buf || cap < 8) return -2;
    char composed[128];      /* committed UTF-8 text */
    int comp_len = 0;
    char pinyin[8];          /* typed letters */
    int py_len = 0;
    int done = 0, cancelled = 0;
    composed[0] = 0;

    while (!done) {
        /* Render the input row: prompt + committed text + pinyin +
         * candidate strip. */
        int row = win->h - 1;
        tui_attr_t a = (tui_attr_t)TUI_MAKE_ATTR(TUI_COLOR_WHITE, TUI_COLOR_NAVY);
        tui_attr_t save = win->attr;
        win->attr = a;
        tui_window_focus_set(win);
        for (int x = 0; x < win->w; x++) {
            tui_move(row, x);
            tui_addch(' ');
        }
        tui_move(row, 0);
        tui_print(prompt ? prompt : "");
        tui_print(composed);
        if (py_len > 0) {
            tui_print("[");
            tui_print(pinyin);
            tui_print("]");
            u32 used = tui_chinese_str_cols(prompt ? prompt : "") +
                       tui_chinese_str_cols(composed) +
                       (u32)py_len + 2;
            int m = ime_match(pinyin, py_len);
            u32 cands[9];
            int ncand = (m >= 0) ? ime_candidates(m, cands, 9) : 0;
            if (ncand > 0 && used + 2 < (u32)win->w) {
                tui_print(" ");
                char dig[3];
                for (int i = 0; i < ncand && used + 4 < (u32)win->w; i++) {
                    u8 u8buf[5];
                    u32 produced = 0;
                    if (encoding_utf8_encode(cands[i], u8buf, 4,
                                             &produced) == 0) {
                        u8buf[produced] = 0;
                        char one[5];
                        for (u32 b = 0; b < produced; b++) one[b] = (char)u8buf[b];
                        one[produced] = 0;
                        tui_print(one);
                        dig[0] = (char)('1' + i);
                        dig[1] = ' ';
                        dig[2] = 0;
                        tui_print(dig);
                        used += 4;
                    }
                }
            }
        }
        tui_window_refresh(win);
        win->attr = save;

        int k = tui_getch();
        if (k == TUI_KEY_ESC) {
            cancelled = 1;
            done = 1;
        } else if (k == TUI_KEY_ENTER) {
            done = 1;
        } else if (k == TUI_KEY_BACKSPACE) {
            if (py_len > 0) {
                py_len--;
                pinyin[py_len] = 0;
            } else if (comp_len > 0) {
                /* drop the last UTF-8 character of the composition */
                int p = comp_len - 1;
                while (p > 0 && ((u8)composed[p] & 0xC0) == 0x80) p--;
                comp_len = p;
                composed[comp_len] = 0;
            }
        } else if (k >= 'a' && k <= 'z' && py_len < 6) {
            pinyin[py_len++] = (char)k;
            pinyin[py_len] = 0;
            /* auto-commit when the typed prefix is already a full
             * syllable and no longer syllable extends it */
            int exact = -1, ext = 0;
            for (int i = 0; i < PINYIN_SYLLABLE_COUNT; i++) {
                char s[8];
                ime_syllable_str(i, s, sizeof(s));
                int sl = (int)strlen(s);
                if (sl == py_len && strncmp(s, pinyin, (usize)py_len) == 0) {
                    exact = i;
                } else if (sl > py_len &&
                           strncmp(s, pinyin, (usize)py_len) == 0) {
                    ext = 1;
                }
            }
            if (exact >= 0 && !ext) {
                u32 cands[9];
                int ncand = ime_candidates(exact, cands, 9);
                if (ncand == 1) {
                    /* unique common reading: commit candidate 1 */
                    u8 u8buf[4];
                    u32 produced = 0;
                    if (encoding_utf8_encode(cands[0], u8buf, 4,
                                             &produced) == 0 &&
                        comp_len + (int)produced < (int)sizeof(composed) - 1) {
                        for (u32 b = 0; b < produced; b++) {
                            composed[comp_len++] = (char)u8buf[b];
                        }
                        composed[comp_len] = 0;
                    }
                    py_len = 0;
                    pinyin[0] = 0;
                }
            }
        } else if (k >= '1' && k <= '9' && py_len > 0) {
            int m = ime_match(pinyin, py_len);
            u32 cands[9];
            int ncand = (m >= 0) ? ime_candidates(m, cands, 9) : 0;
            int pick = k - '1';
            if (pick < ncand) {
                u8 u8buf[4];
                u32 produced = 0;
                if (encoding_utf8_encode(cands[pick], u8buf, 4,
                                         &produced) == 0 &&
                    comp_len + (int)produced < (int)sizeof(composed) - 1) {
                    for (u32 b = 0; b < produced; b++) {
                        composed[comp_len++] = (char)u8buf[b];
                    }
                    composed[comp_len] = 0;
                }
                py_len = 0;
                pinyin[0] = 0;
            }
        } else if (k == ' ') {
            if (py_len > 0) {
                int m = ime_match(pinyin, py_len);
                u32 cands[9];
                int ncand = (m >= 0) ? ime_candidates(m, cands, 9) : 0;
                if (ncand > 0) {
                    u8 u8buf[4];
                    u32 produced = 0;
                    if (encoding_utf8_encode(cands[0], u8buf, 4,
                                             &produced) == 0 &&
                        comp_len + (int)produced < (int)sizeof(composed) - 1) {
                        for (u32 b = 0; b < produced; b++) {
                            composed[comp_len++] = (char)u8buf[b];
                        }
                        composed[comp_len] = 0;
                    }
                }
                py_len = 0;
                pinyin[0] = 0;
            } else if (comp_len + 1 < (int)sizeof(composed) - 1) {
                composed[comp_len++] = ' ';
                composed[comp_len] = 0;
            }
        } else if (k >= 0x20 && k < 0x7F) {
            /* ASCII direct input when no pinyin is being composed */
            if (py_len == 0 &&
                comp_len + 1 < (int)sizeof(composed) - 1) {
                composed[comp_len++] = (char)k;
                composed[comp_len] = 0;
            }
        }
    }

    if (cancelled) return -1;
    int n = comp_len;
    if (n > cap - 1) n = cap - 1;
    memcpy(buf, composed, (usize)n);
    buf[n] = 0;
    return n;
}

/* Direct ASCII line input on a TUI window: every printable character
 * goes into the buffer as-is, Enter commits, Esc cancels.  Used for
 * fields that must never enter pinyin composition (SSID, host names,
 * numeric fields).  The IME editor above would otherwise turn lowercase
 * letters into pinyin buffers and make plain names impossible to type. */
int tui_chinese_input_ascii(tui_window_t* win, const char* prompt,
                            char* buf, int cap) {
    if (!win || !buf || cap < 2) return -2;
    int len = 0;
    buf[0] = 0;
    int row = win->h - 1;
    for (;;) {
        tui_attr_t a = (tui_attr_t)TUI_MAKE_ATTR(TUI_COLOR_WHITE, TUI_COLOR_NAVY);
        tui_attr_t save = win->attr;
        win->attr = a;
        tui_window_focus_set(win);
        for (int x = 0; x < win->w; x++) {
            tui_move(row, x);
            tui_addch(' ');
        }
        tui_move(row, 0);
        tui_print(prompt ? prompt : "");
        tui_print(buf);
        tui_window_refresh(win);
        win->attr = save;

        int k = tui_getch();
        if (k == TUI_KEY_ESC) return -1;
        if (k == TUI_KEY_ENTER) {
            int n = len;
            if (n > cap - 1) n = cap - 1;
            buf[n] = 0;
            return n;
        }
        if (k == TUI_KEY_BACKSPACE) {
            if (len > 0) buf[--len] = 0;
        } else if (k >= 0x20 && k < 0x7F && len < cap - 1) {
            buf[len++] = (char)k;
            buf[len] = 0;
        }
    }
}

/* ---- Chinese text input application (the user-facing IME entry) ---- */

/* Full-screen pinyin composition pad: type letters, watch the candidate
 * strip, pick with digits 1-9 (or space for the first candidate), Enter
 * commits the composed text and displays it on screen, Esc leaves.
 * This is the reachable IME entry the launcher menu exposes - without
 * it the IME engine had no user-facing surface outside the SSID field. */
int tui_cn_input_app(void) {
    if (tui_chinese_init() != 0) return -1;
    int cols, rows;
    tui_term_size(&cols, &rows);
    tui_window_t *win = tui_window_new(0, 0, rows, cols);
    if (!win) return -1;
    tui_window_title(win, "中文输入（拼音）");
    char text[256];
    text[0] = 0;
    for (;;) {
        tui_window_clear(win);
        tui_window_focus_set(win);
        tui_set_attr((tui_attr_t)TUI_MAKE_ATTR(TUI_COLOR_YELLOW, TUI_COLOR_BLACK) |
                     TUI_ATTR_BOLD);
        tui_print_at(0, 1, "拼音输入  [打字即拼音  数字1-9选字  空格首选  回车上屏  Esc退出]");
        tui_set_attr((tui_attr_t)TUI_MAKE_ATTR(TUI_COLOR_AQUA, TUI_COLOR_BLACK));
        tui_print_at(1, 1, "输入区（回车后全文在此显示）:");
        tui_set_attr((tui_attr_t)TUI_MAKE_ATTR(TUI_COLOR_WHITE, TUI_COLOR_BLACK));
        tui_print_at(2, 1, text);
        tui_set_attr((tui_attr_t)TUI_MAKE_ATTR(TUI_COLOR_SILVER, TUI_COLOR_BLACK));
        tui_print_at(3, 1, "示例: 输入 ni 看候选，按 1 或空格得到 你");
        tui_window_refresh(win);

        char line[128];
        int n = tui_chinese_input(win, "拼音> ", line, sizeof(line));
        if (n < 0) break;                     /* Esc: leave the pad */
        int cur = (int)strlen(text);
        if (n > 0 && cur + n < (int)sizeof(text) - 1) {
            for (int i = 0; i < n; i++) text[cur + i] = line[i];
            text[cur + n] = 0;
        }
    }
    tui_window_free(win);
    tui_update_panels();
    return 0;
}
