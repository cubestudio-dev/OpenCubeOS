/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP03 - services/display: CJK surface op bodies.
 *
 * Rescued verbatim out of chinese/chinese_cmd.c (the user-plane
 * command registration died with the oc> shell; these bodies serve
 * the L1SVC "display" ops through disp_l1.c).  The IME command pair
 * (ime / ime-test) left in WP-03 together with the input-method
 * engine; 3.0.0-WP04 G11b revives both verbatim (ime_console_compose
 * + disp_cmd_ime + disp_cmd_ime_test below, engine in ime_engine.c).
 */
#include "chinese_font.h"
#include "encoding.h"
#include "render_cn.h"
#include "console_cn.h"
#include "screen_console.h"
#include "screen_font.h"
#include "lib_string.h"
#include "driver_input_keyboard.h"
#include "ime_engine.h"
#include "ime_session.h"
#include "pinyin_phrase.h"
#include "l0_i18n.h"

extern int chinese_test_run(int verbose);
/* ---- status -------------------------------------------------------- */

int disp_cmd_chinese(const char *args) {
    (void)args;
    chinese_font_stats_t st;
    chinese_font_status(&st);
    char buf[160];
    screen_console_puts("chinese support status:\n");
    screen_console_puts("  font     : ");
    screen_console_puts(st.source ? st.source : "-");
    screen_console_puts("\n");
    screen_console_puts("  coverage : ");
    u64_to_str(st.hanzi_count, buf);
    screen_console_puts(buf);
    screen_console_puts(" hanzi + ");
    u64_to_str(st.symbol_count, buf);
    screen_console_puts(buf);
    screen_console_puts(" symbols, 16x16, 2 bytes/row\n");
    screen_console_puts("  data     : ");
    u64_to_str(st.hanzi_data_size, buf);
    screen_console_puts(buf);
    screen_console_puts(" + ");
    u64_to_str(st.symbol_data_size, buf);
    screen_console_puts(buf);
    screen_console_puts(" bytes embedded\n");
    screen_console_puts("  slots    : ");
    u64_to_str(st.slots_used, buf);
    screen_console_puts(buf);
    screen_console_puts(" of ");
    u64_to_str(CHINESE_FONT_MAX_SLOTS, buf);
    screen_console_puts(buf);
    screen_console_puts(" used\n");
    encoding_status(buf, sizeof(buf));
    screen_console_puts("  ");
    screen_console_puts(buf);
    screen_console_putc('\n');
    screen_console_puts("  console encoding: ");
    screen_console_puts(console_cn_encoding_name(
        console_cn_get_encoding()));
    screen_console_puts("\n");
    return 0;
}

/* ---- test ---------------------------------------------------------- */

int disp_cmd_chinesetest(const char *args) {
    (void)args;
    screen_console_puts("chinesetest: full Chinese support suite\n");
    int failed = chinese_test_run(1);
    if (failed == 0) {
        screen_console_puts("chinesetest: ALL PASS\n");
        return 0;
    }
    screen_console_puts("chinesetest: FAILURES PRESENT\n");
    return 1;
}

/* ---- font ----------------------------------------------------------- */

int disp_cmd_font(const char *args) {
    (void)args;
    screen_console_puts("registered fonts:\n");
    screen_console_puts("  ascii-8x16     built-in screen_font8x16, 8x16\n");
    screen_console_puts("  gb2312-16x16   embedded GB2312, 6763+682 glyphs\n");
    char num[12];
    u64_to_str((u64)font_count(), num);
    screen_console_puts("  total live: ");
    screen_console_puts(num);
    screen_console_putc('\n');
    screen_console_puts("  active renderer: ");
    screen_renderer_t* r = screen_active_renderer;
    screen_console_puts(r && r->name ? r->name : "-");
    screen_console_putc('\n');
    return 0;
}

int disp_cmd_font_test(const char *args) {
    (void)args;
    render_chinese_init();
    /* Render a sample line of mixed text at the current cursor row.
     * The string is user content: Chinese glyphs + ASCII. */
    screen_console_puts("font-test: rendering sample row (16x16 CJK):\n");
    u32 y = screen_console_get()->cur_y;
    u32 x = 0;
    /* fix2: hide the underscore cursor while the sample row is drawn -
     * the visible cursor cell used to overwrite the first glyph's left
     * half (it sits exactly on the row the sample starts at). */
    int cursor_was_visible = screen_console_get()->cursor_visible;
    if (cursor_was_visible) screen_console_show_cursor(0);
    render_chinese_string("\xE4\xB8\xAD\xE6\x96\x87\xE6\xB5\x8B\xE8\xAF\x95"
                          " OpenCube \xE6\xB5\x8B\xE8\xAF\x95",
                          x, y * OC_FONT_CELL_H, 0xFFFFFF, 0x000000);
    /* advance the console cursor PAST the pixel-drawn row before
     * printing the label - the label used to start at the same row and
     * overwrite the sample glyphs it was supposed to describe. */
    screen_console_putc('\n');
    screen_console_puts("font-test: 16px row drawn above (zhongwen OpenCube shi)\n");
    screen_console_putc('\n');
    if (cursor_was_visible) screen_console_show_cursor(1);
    return 0;
}

/* ---- encoding -------------------------------------------------------- */

int disp_cmd_encoding(const char *args) {
    (void)args;
    char buf[192];
    encoding_status(buf, sizeof(buf));
    screen_console_puts(buf);
    screen_console_putc('\n');
    return 0;
}

int disp_cmd_encoding_test(const char *args) {
    (void)args;
    u32 cp, consumed;
    int fails = 0;
    /* spot checks (full suite: chinesetest) */
    if (encoding_utf8_decode((const u8*)"\xE4\xB8\xAD", 3, &cp,
                             &consumed) != 0 || cp != 0x4E2D) fails++;
    if (encoding_gb2312_decode((const u8*)"\xD6\xD0", 2, &cp,
                               &consumed) != 0 || cp != 0x4E2D) fails++;
    u8 out[8]; u32 produced;
    if (encoding_utf8_encode(0x4E2D, out, 8, &produced) != 0 ||
        produced != 3) fails++;
    if (encoding_gb2312_encode(0x4E2D, out, 8, &produced) != 0 ||
        produced != 2) fails++;
    if (fails == 0) {
        screen_console_puts("encoding-test: PASS (utf8+gb2312 spot checks)\n");
        return 0;
    }
    screen_console_puts("encoding-test: FAIL\n");
    return 1;
}

/* local substring probe (lib_string carries no strstr) */
static const char *cn_strstr(const char *hay, const char *needle) {
    if (!hay || !needle) return 0;
    for (; *hay; hay++) {
        const char *a = hay, *b = needle;
        while (*a && *b && *a == *b) { a++; b++; }
        if (!*b) return hay;
    }
    return 0;
}

/* ---- ime -------------------------------------------------------------- */
/* 3.0.0-WP04 G11b: revived verbatim from v2.2.0 chinese/chinese_cmd.c
 * (ime_console_compose + disp_cmd_ime + disp_cmd_ime_test). */

/* WP-10-chinese_support-fix1: real interactive console composition.
 * The status block used to promise an interactive flow ("type pinyin
 * letters, digits 1-9 pick, space = first, Enter commit") that the
 * command did not have - it only printed the status.  This line
 * mirrors the TUI pad contract on the scrolling console: lowercase
 * letters compose pinyin (max 6), the strip after "[pinyin]" shows
 * the live candidates with digit hints, digits 1-9 commit that
 * candidate, space commits the first, Backspace drops a letter (or
 * the last committed character when no letters are pending), plain
 * ASCII lands verbatim while no pinyin is pending, Enter ends the
 * line, Esc / Ctrl+C leave.  The whole line redraws through '\r' so
 * both the framebuffer grid and the serial tee show the same stream.
 *
 * Returns the committed UTF-8 byte count (also NUL-terminated), or
 * -1 when the line was cancelled with content discarded. */
int ime_console_compose(u8 *out, int cap) {
    /* 3.0.0-WP06g2: rewritten onto the SYSTEM IME session engine
     * (ime_session.c) - the same engine the nano/vi editors drive
     * through L1SVC_DISP_IME_KEY.  Nothing IME-shaped lives here
     * anymore: this loop only parks for keys, feeds each one to the
     * engine, renders the strip the engine renders for the editors,
     * and accumulates the committed text.  Phrases, paging and the
     * compose model all come from the one engine. */
    if (!out || cap < 8) return -1;
    u8 composed[128];          /* committed UTF-8 (return value)   */
    int comp_len = 0;
    composed[0] = 0;
    int cancelled = 0;

    ime_session_reset();

    screen_console_t *con = screen_console_get();
    int pad = (con && con->cols > 8) ? (int)con->cols - 8 : 72;
    if (pad > 72) pad = 72;

    for (;;) {
        /* render: "IME> <committed>[strip]" on one line */
        char strip[160];
        ime_session_compose_line(strip, sizeof(strip));
        screen_console_putc('\r');
        for (int i = 0; i < pad; i++) screen_console_putc(' ');
        screen_console_putc('\r');
        screen_console_puts("IME> ");
        screen_console_puts((const char *)composed);
        if (strip[0]) {
            screen_console_puts(strip);
        }

        /* park until a key (same discipline as v2.2.0: sti;hlt in the
         * ring-3 service frame; the timer tick bounds the wait). */
        int k;
        for (;;) {
            k = driver_input_keyboard_getch();
            if (k > 0) break;
            __asm__ volatile("sti; hlt");
        }

        if ((k == '\r' || k == '\n') && !ime_session_active()) {
            break;               /* Enter ends the LINE (not compose) */
        }

        char proto[256];
        int act = ime_session_key(k, proto, sizeof(proto));
        const char *text = (proto[0] >= '0' && proto[0] <= '9' &&
                            proto[1] == '\n') ? proto + 2 : proto;
        if (act == 2 || act == 3) {
            /* commit: append the engine's UTF-8 text */
            int n = (int)strlen(text);
            if (comp_len + n < (int)sizeof(composed) - 1) {
                for (int i = 0; i < n; i++)
                    composed[comp_len++] = text[i];
                composed[comp_len] = 0;
            }
        }
        if (act == 3 || act == 0) {
            /* passthrough tail: plain ASCII lands verbatim (the
             * engine already committed its part for act==3) */
            if (!ime_session_active() && k >= 0x20 && k < 0x7F &&
                comp_len + 1 < (int)sizeof(composed) - 1) {
                composed[comp_len++] = (char)k;
                composed[comp_len] = 0;
            }
        }
        if ((k == 0x1B || k == OC_KEY_ESC_KEY || k == OC_KEY_CTRL_C) &&
            !ime_session_active() && comp_len == 0) {
            cancelled = 1;       /* Esc on an empty line cancels */
            break;
        }
        if ((k == 0x1B || k == OC_KEY_ESC_KEY || k == OC_KEY_CTRL_C) &&
            !ime_session_active() && comp_len > 0) {
            break;               /* Esc with content: end the line */
        }
    }

    ime_session_reset();
    screen_console_putc('\r');
    for (int i = 0; i < pad; i++) screen_console_putc(' ');
    screen_console_putc('\r');
    screen_console_putc('\n');
    char num[12];
    int ncp = 0;
    for (int i = 0; composed[i]; ) {
        /* count CHARACTERS (UTF-8 lead bytes) */
        if (((u8)composed[i] & 0xC0) != 0x80) ncp++;
        i++;
    }
    if (cancelled) {
        screen_console_puts(l0_tr_("ime_cancelled"));
        u64_to_str((u64)ncp, num);
        screen_console_puts(num);
        screen_console_puts(l0_tr_("ime_chars_discarded"));
        screen_console_putc('\n');
        return -1;
    }
    screen_console_puts(l0_tr_("ime_composed"));
    u64_to_str((u64)ncp, num);
    screen_console_puts(num);
    screen_console_puts(l0_tr_("ime_characters"));
    if (ncp > 0) {
        screen_console_puts(": ");
        console_cn_puts((const char *)composed);
    }
    screen_console_putc('\n');
    int n = comp_len;
    if (n > cap - 1) n = cap - 1;
    for (int i = 0; i < n; i++) out[i] = (u8)composed[i];
    out[n] = 0;
    return n;
}

int disp_cmd_ime(const char *args) {
    /* 3.0.0-WP06g2: the status text renders through the L0 i18n
     * resource (etc/i18n/l0), and the usage block documents the
     * session engine: phrases, paging, the editors' F2 entry. */
    (void)args;
    char num[12], buf[160];
    screen_console_puts(l0_tr_("ime_status_title"));
    screen_console_puts(l0_tr_("ime_status_syllables"));
    u64_to_str(tui_chinese_ime_syllables(), num);
    screen_console_puts(num);
    screen_console_putc('\n');
    screen_console_puts(l0_tr_("ime_status_level1"));
    u64_to_str(tui_chinese_ime_chars(), num);
    screen_console_puts(num);
    screen_console_puts(l0_tr_("ime_status_level1_tail"));
    screen_console_putc('\n');
    screen_console_puts(l0_tr_("ime_status_phrases"));
    u64_to_str((u64)PINYIN_PHRASE_COUNT, num);
    screen_console_puts(num);
    screen_console_putc('\n');
    screen_console_puts(l0_tr_("ime_usage_1"));
    screen_console_puts(l0_tr_("ime_usage_2"));
    screen_console_puts(l0_tr_("ime_usage_3"));
    screen_console_puts(l0_tr_("ime_usage_4"));
    encoding_status(buf, sizeof(buf));
    screen_console_puts("  ");
    screen_console_puts(buf);
    screen_console_putc('\n');

    /* fix1: the composition line is opt-in via "ime -i" so scripted
     * serial sessions (E2E drivers, pipes) keep the plain status
     * output; interactive users type ime -i and compose on the
     * console.  The summary (composed characters + glyphs) is printed
     * by ime_console_compose itself. */
    if (args && args[0] == '-' && args[1] == 'i') {
        u8 line[128];
        (void)ime_console_compose(line, sizeof(line));
    }
    return 0;
}

/* 3.0.0-WP06g2: the SYSTEM IME session battery.  The single-char
 * checks ride the v2.2.0 engine contract (ni/hao/qqq); the session
 * checks drive ime_session_key through the exact key sequence an
 * editor sends and assert the action protocol + committed text:
 * phrase-first (zhongguo -> 中国), smart compose (xiezi -> 写字,
 * NOT in the phrase list), paging (yi has 18 candidates), digit
 * pick, space pick, Enter raw-letters, Backspace letter-drop, Esc
 * clear. */
int disp_cmd_ime_test(const char *args) {
    (void)args;
    u32 cands[9];
    int fails = 0;
    int n = tui_chinese_ime_test("ni", cands, 9);
    if (n <= 0 || cands[0] != 0x4F60)
        { fails++; screen_console_puts("  [E1 ni]\n"); }
    n = tui_chinese_ime_test("hao", cands, 9);
    int has_hao = 0;
    for (int i = 0; i < n && i < 9; i++)
        if (cands[i] == 0x597D) has_hao = 1;         /* hao -> hao */
    if (n <= 0 || !has_hao) { fails++; screen_console_puts("  [E2 hao]\n"); }
    if (tui_chinese_ime_test("qqq", cands, 9) != 0)
        { fails++; screen_console_puts("  [E3 qqq]\n"); }

    /* ---- session battery ---- */
    char proto[256];
    const char *text;
    int act;

    ime_session_reset();

    /* S1: phrase-first - "zhongguo" composes and SPACE commits 中国 */
    for (const char *k = "zhongguo"; *k; k++) {
        act = ime_session_key(*k, proto, sizeof(proto));
        if (act != 1) { fails++; screen_console_puts("  [S1a keys]\n"); break; }
    }
    if (act == 1) {
        char line[160];
        (void)ime_session_compose_line(line, sizeof(line));
        if (cn_strstr(line, "zhongguo") == 0)
            { fails++; screen_console_puts("  [S1b strip]\n"); }
    }
    act = ime_session_key(' ', proto, sizeof(proto));
    text = proto + 2;
    if (act != 2 || strcmp(text, "\xe4\xb8\xad\xe5\x9b\xbd") != 0)
        { fails++; screen_console_puts("  [S1c zhongguo commit]\n"); }

    /* S2: smart compose - "xiezi" (NOT in the phrase list) composes
     * 写字 (xie's + zi's first candidates) and SPACE commits it */
    for (const char *k = "xiezi"; *k; k++)
        (void)ime_session_key(*k, proto, sizeof(proto));
    act = ime_session_key(' ', proto, sizeof(proto));
    text = proto + 2;
    if (act != 2 || strlen(text) != 6)
        { fails++; screen_console_puts("  [S2 xiezi compose]\n"); }
    /* S3: paging - "yi" has many candidates; PageDown flips to 2/2
     * and the strip shows the page marker */
    (void)ime_session_key('y', proto, sizeof(proto));
    (void)ime_session_key('i', proto, sizeof(proto));
    act = ime_session_key(OC_KEY_PGDN, proto, sizeof(proto));
    {
        char line[160];
        int ln = ime_session_compose_line(line, sizeof(line));
        int has_page2 = 0;
        for (int i = 0; i + 4 < ln; i++)
            if (line[i] == '(' && line[i+1] == '2' && line[i+2] == '/')
                has_page2 = 1;
        if (act != 1 || !has_page2)
            { fails++; screen_console_puts("  [S3 paging]\n"); }
    }

    /* S4: digit pick - restart "ni", digit 1 commits 你 */
    ime_session_reset();
    (void)ime_session_key('n', proto, sizeof(proto));
    (void)ime_session_key('i', proto, sizeof(proto));
    act = ime_session_key('1', proto, sizeof(proto));
    text = proto + 2;
    if (act != 2 || strcmp(text, "\xe4\xbd\xa0") != 0)
        { fails++; screen_console_puts("  [S4 digit pick]\n"); }

    /* S5: Enter commits the RAW letters */
    ime_session_reset();
    (void)ime_session_key('a', proto, sizeof(proto));
    (void)ime_session_key('b', proto, sizeof(proto));
    act = ime_session_key('\r', proto, sizeof(proto));
    text = proto + 2;
    if (act != 2 || strcmp(text, "ab") != 0)
        { fails++; screen_console_puts("  [S5 enter raw]\n"); }

    /* S6: Backspace drops a letter; Esc clears the compose */
    ime_session_reset();
    (void)ime_session_key('n', proto, sizeof(proto));
    (void)ime_session_key('i', proto, sizeof(proto));
    (void)ime_session_key('m', proto, sizeof(proto));
    (void)ime_session_key('a', proto, sizeof(proto));
    act = ime_session_key(OC_KEY_BACKSPACE, proto, sizeof(proto));
    if (act != 1 || ime_session_py_len() != 3)
        { fails++; screen_console_puts("  [S6a backspace]\n"); }
    act = ime_session_key(OC_KEY_ESC_KEY, proto, sizeof(proto));
    if (act != 1 || ime_session_active())
        { fails++; screen_console_puts("  [S6b esc]\n"); }

    /* S7: passthrough - a digit with NO compose is action 0 */
    ime_session_reset();
    act = ime_session_key('5', proto, sizeof(proto));
    if (act != 0) { fails++; screen_console_puts("  [S7 passthrough]\n"); }

    ime_session_reset();

    if (fails == 0) {
        screen_console_puts(l0_tr_("ime_test_pass"));
        return 0;
    }
    {
        char num[12];
        screen_console_puts(l0_tr_("ime_test_fail"));
        u64_to_str((u64)fails, num);
        screen_console_puts(" (");
        screen_console_puts(num);
        screen_console_puts(" subtest(s))\n");
    }
    return 1;
}
