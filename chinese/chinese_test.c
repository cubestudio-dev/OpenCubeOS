/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-10-chinese_support
 * File: chinese/chinese_test.c
 * Purpose: In-kernel regression suite for the Chinese support stack.
 *          Runs from the `chinesetest` shell command. Returns the
 *          number of failed checks (0 = all pass), mirroring
 *          tui/tui_test.c.
 */
#include "chinese_font.h"
#include "encoding.h"
#include "render_cn.h"
#include "console_cn.h"
#include "tui_chinese.h"
#include "../core/lib/lib_string.h"
#include "../drivers/display/screen_console.h"
#include "../drivers/display/screen_font.h"
#include "../tui/tui.h"
#include "../drivers/input/driver_input_keyboard.h"
#include "../recovery/shell_lineedit.h"
#include "../recovery/shell.h"
#include "../services/fs/fs_vfs.h"

static int g_total;
static int g_failed;

#define CHECK(cond, name) do {                                   \
        g_total++;                                               \
        if (cond) {                                              \
            screen_console_puts("  PASS ");                      \
            screen_console_puts(name);                           \
            screen_console_putc('\n');                           \
        } else {                                                 \
            g_failed++;                                          \
            screen_console_puts("  FAIL ");                      \
            screen_console_puts(name);                           \
            screen_console_putc('\n');                           \
        }                                                        \
    } while (0)

/* ------------------------------------------------ 1. font data tests */

static void test_font_data(void) {
    screen_console_puts("[1] font data\n");
    chinese_font_stats_t st;
    int rc = chinese_font_status(&st);
    CHECK(rc == 0, "status() returns 0");
    CHECK(st.hanzi_count == 6763, "hanzi count == 6763");
    CHECK(st.symbol_count == 682, "symbol count == 682");
    CHECK(st.glyph_bytes == 32, "glyph bytes == 32 (16x16, 2 B/row)");
    CHECK(st.hanzi_data_size == 6763u * 32, "hanzi size == 216416 B");
    CHECK(st.symbol_data_size == 682u * 32, "symbol size == 21824 B");
    CHECK(st.slots_used >= 1, "embedded slot loaded");

    /* zone 16 wei 1 = U+554A (a); zone 87 wei 94 = last hanzi */
    int first = chinese_font_lookup(0, 16, 1);
    CHECK(first == 0, "zone 16 wei 1 -> compact index 0");
    int last = chinese_font_lookup(0, 87, 94);
    CHECK(last == 6762, "zone 87 wei 94 -> compact index 6762");
    int gap = chinese_font_lookup(0, 55, 90);
    CHECK(gap == -1, "zone 55 wei 90 is a GB2312 gap slot");

    /* every glyph bitmap is present and (for hanzi) non-empty */
    int empty = 0;
    for (int i = 0; i < 6763; i++) {
        const u8* g = chinese_font_get_bitmap(0, i);
        if (!g) { empty = -1; break; }
        int ink = 0;
        for (int b = 0; b < 32; b++) ink |= g[b];
        if (!ink) { empty++; }
    }
    CHECK(empty == 0, "all 6763 hanzi bitmaps non-empty");
    const u8* sym = chinese_font_get_bitmap(0, 0x8000 | 0);
    CHECK(sym != NULL, "symbol table reachable via bit15");
}

/* ---------------------------------------------- 2. font lookup tests */

static void test_font_lookup(void) {
    screen_console_puts("[2] font lookup\n");
    /* U+4E2D (zhong) = GB2312 0xD6D0 = zone 54 wei 48 */
    int idx = chinese_font_lookup_unicode(0, 0x4E2D);
    CHECK(idx >= 0, "U+4E2D found in unicode index");
    int idx2 = chinese_font_lookup(0, 54, 48);
    CHECK(idx2 == idx, "zone/wei lookup agrees with unicode lookup");
    u32 cp = chinese_font_index_to_unicode(0, idx);
    CHECK(cp == 0x4E2D, "index -> unicode round-trips to U+4E2D");

    /* U+3002 (fullwidth ideographic full stop) is a zone-1 symbol */
    int s = chinese_font_lookup_unicode(0, 0x3002);
    CHECK(s >= 0 && (s & 0x8000), "U+3002 in symbol table (bit15 set)");

    CHECK(chinese_font_lookup_unicode(0, 0x4E00) == -1 ||
          chinese_font_lookup_unicode(0, 0x4E00) >= 0,
          "unmapped/mapped U+4E00 handled without crash");
    CHECK(chinese_font_lookup(0, 95, 1) == -1, "zone 95 rejected");
    CHECK(chinese_font_lookup(0, 16, 0) == -1, "wei 0 rejected");
    u32 w, h, stride;
    CHECK(chinese_font_get_metrics(&w, &h, &stride) == 0 && w == 16 &&
          h == 16 && stride == 2, "metrics 16x16 stride 2");
}

/* --------------------------------------------- 3. font manage tests */

static void test_font_manage(void) {
    screen_console_puts("[3] font management\n");
    CHECK(chinese_font_load() == 0, "embedded load idempotent");
    /* runtime font: single-glyph table with a matching uto table */
    static const u8 one_glyph[32] = {0};
    static const u16 one_uto[2] = { 0x4E2D, 0 };
    int slot = chinese_font_load_mem("test-font", one_glyph, 1,
                                     NULL, 0, one_uto, 1);
    CHECK(slot >= 1, "runtime slot allocated (>=1)");
    if (slot >= 1) {
        int idx = chinese_font_lookup_unicode(slot, 0x4E2D);
        CHECK(idx == 0, "runtime font resolves its glyph");
        CHECK(chinese_font_unload(slot) == 0, "runtime unload ok");
    }
    CHECK(chinese_font_unload(0) == -5, "embedded slot unload rejected");
    CHECK(chinese_font_unload(99) == -3, "unknown slot rejected");
    CHECK(chinese_font_shutdown() == 0, "shutdown ok");
    chinese_font_load();   /* restore for later tests */
}

/* ------------------------------------------------ 4. GB2312 tests */

static void test_gb2312(void) {
    screen_console_puts("[4] GB2312 codec\n");
    u32 cp, consumed;
    /* 0xD6 0xD0 = zhong */
    CHECK(encoding_gb2312_decode((const u8*)"\xD6\xD0", 2, &cp,
                                 &consumed) == 0 && cp == 0x4E2D &&
          consumed == 2, "decode D6D0 -> U+4E2D");
    /* ASCII pass-through */
    CHECK(encoding_gb2312_decode((const u8*)"A", 1, &cp,
                                 &consumed) == 0 && cp == 'A' &&
          consumed == 1, "decode 'A' -> 0x41");
    /* invalid lead */
    CHECK(encoding_gb2312_decode((const u8*)"\x88\x40", 2, &cp,
                                 &consumed) == ENC_E_ILLEGAL,
          "lead 0x88 rejected");
    /* truncated pair */
    CHECK(encoding_gb2312_decode((const u8*)"\xD6", 1, &cp,
                                 &consumed) == ENC_E_TRUNC,
          "truncated pair rejected");
    /* encode back */
    u8 out[4]; u32 produced;
    CHECK(encoding_gb2312_encode(0x4E2D, out, 4, &produced) == 0 &&
          produced == 2 && out[0] == 0xD6 && out[1] == 0xD0,
          "encode U+4E2D -> D6 D0");
    CHECK(encoding_gb2312_encode(0x2E80, out, 4, &produced) == ENC_E_UNMAP,
          "encode non-GB2312 cp rejected");
}

/* -------------------------------------------------- 5. UTF-8 tests */

static void test_utf8(void) {
    screen_console_puts("[5] UTF-8 codec\n");
    u32 cp, consumed;
    CHECK(encoding_utf8_decode((const u8*)"\xE4\xB8\xAD", 3, &cp,
                               &consumed) == 0 && cp == 0x4E2D &&
          consumed == 3, "decode E4 B8 AD -> U+4E2D");
    CHECK(encoding_utf8_decode((const u8*)"z", 1, &cp,
                               &consumed) == 0 && cp == 'z',
          "decode 'z' pass-through");
    /* overlong 2-byte 'A' (C1 81) rejected */
    CHECK(encoding_utf8_decode((const u8*)"\xC1\x81", 2, &cp,
                               &consumed) == ENC_E_ILLEGAL,
          "overlong C1 81 rejected");
    /* surrogate ED A0 80 rejected */
    CHECK(encoding_utf8_decode((const u8*)"\xED\xA0\x80", 3, &cp,
                               &consumed) == ENC_E_ILLEGAL,
          "surrogate ED A0 80 rejected");
    /* truncated */
    CHECK(encoding_utf8_decode((const u8*)"\xE4\xB8", 2, &cp,
                               &consumed) == ENC_E_TRUNC,
          "truncated 3-byte rejected");
    /* bad continuation */
    CHECK(encoding_utf8_decode((const u8*)"\xE4\x28\xAD", 3, &cp,
                               &consumed) == ENC_E_ILLEGAL,
          "bad continuation byte rejected");
    /* 4-byte emoji U+1F600 */
    CHECK(encoding_utf8_decode((const u8*)"\xF0\x9F\x98\x80", 4, &cp,
                               &consumed) == 0 && cp == 0x1F600,
          "decode 4-byte U+1F600");
    /* encode round trips */
    u8 out[4]; u32 produced;
    CHECK(encoding_utf8_encode(0x4E2D, out, 4, &produced) == 0 &&
          produced == 3 && out[0] == 0xE4 && out[1] == 0xB8 &&
          out[2] == 0xAD, "encode U+4E2D -> E4 B8 AD");
    CHECK(encoding_utf8_encode(0xD800, out, 4, &produced) == ENC_E_ILLEGAL,
          "encode surrogate rejected");
    CHECK(encoding_utf8_encode(0x110000, out, 4, &produced) == ENC_E_ILLEGAL,
          "encode > U+10FFFF rejected");
}

/* ----------------------------------------------- 6. conversion tests */

static void test_convert(void) {
    screen_console_puts("[6] encoding conversion\n");
    /* GB2312 "D6D0 CE C4" (zhong wen) -> UTF-8 */
    u8 out[32]; u32 out_len;
    CHECK(encoding_convert((const u8*)"\xD6\xD0\xCE\xC4", 4,
                           ENC_GB2312, ENC_UTF8, out, sizeof(out),
                           &out_len) == 0 && out_len == 6 &&
          out[0] == 0xE4 && out[5] == 0x87,
          "GB2312 zhongwen -> UTF-8");
    /* and back */
    u8 back[32];
    CHECK(encoding_convert(out, out_len, ENC_UTF8, ENC_GB2312,
                           back, sizeof(back), &out_len) == 0 &&
          out_len == 4 && back[0] == 0xD6 && back[3] == 0xC4,
          "UTF-8 -> GB2312 round trip");
    /* UTF-8 -> Unicode-cp via decode (identity of the internal form) */
    u32 cp, consumed;
    encoding_utf8_decode(out, out_len, &cp, &consumed);
    CHECK(cp == 0x4E2D && consumed == 3,
          "UTF-8 -> Unicode cp U+4E2D");
}

/* ------------------------------------------------- 7. detect tests */

static void test_detect(void) {
    screen_console_puts("[7] encoding detection\n");
    encoding_kind_t k;
    CHECK(encoding_detect((const u8*)"hello world", 11, &k) == 0 &&
          k == ENC_ASCII, "detect ascii");
    CHECK(encoding_detect((const u8*)"\xE4\xB8\xAD\xE6\x96\x87", 6,
                          &k) == 0 && k == ENC_UTF8, "detect utf-8");
    CHECK(encoding_detect((const u8*)"\xD6\xD0\xCE\xC4", 4, &k) == 0 &&
          k == ENC_GB2312, "detect gb2312");
    CHECK(encoding_detect((const u8*)"", 0, &k) == 0 && k == ENC_ASCII,
          "detect empty -> ascii");
}

/* ----------------------------------------------- 8. renderer tests */

static void test_render(void) {
    screen_console_puts("[8] renderer extension\n");
    CHECK(render_chinese_init() == 0, "render_chinese_init");
    CHECK(render_chinese_width(0x4E2D) == 16, "CJK width 16 px");
    CHECK(render_chinese_width('A') == 8, "ASCII width 8 px");
    CHECK(render_chinese_cols(0x4E2D) == 2, "CJK cols 2");
    CHECK(render_chinese_cols('A') == 1, "ASCII cols 1");
    CHECK(font_select(0x4E2D) != NULL, "font_select finds CJK font");
    CHECK(font_get_bitmap(0x4E2D) != NULL, "font_get_bitmap CJK");
    /* registration */
    CHECK(font_count() >= 2, "ascii + gb2312 fonts registered");
    /* renderer active and ASCII-identical */
    screen_renderer_t* act = screen_active_renderer;
    CHECK(act != NULL && act->cell_w == 8 && act->cell_h == 16,
          "active renderer keeps 8x16 cell");
    /* drawing into the fb must not crash; exercise a few glyphs */
    render_chinese_char(0x4E2D, 0, 0, 0xFFFFFF, 0x000000);
    render_chinese_char('A', 16, 0, 0xFFFFFF, 0x000000);
    render_chinese_string("\xE4\xB8\xAD\xE6\x96\x87 A", 0, 16,
                          0xFFFFFF, 0x000000);
    render_chinese_char(0x2E80, 0, 32, 0xFFFFFF, 0x000000); /* box glyph */
    CHECK(1, "draw CJK/ASCII/missing-box without fault");
}

/* --------------------------------------------- 9. console ext tests */

static void test_console(void) {
    screen_console_puts("[9] console extension\n");
    CHECK(console_cn_init() == 0, "console_cn_init");
    CHECK(console_cn_get_encoding() == ENC_UTF8, "default encoding utf-8");
    CHECK(console_cn_set_encoding(ENC_GB2312) == 0 &&
          console_cn_get_encoding() == ENC_GB2312, "encoding switch");
    console_cn_set_encoding(ENC_UTF8);
    CHECK(console_cn_set_encoding(ENC_MIXED) == -2, "mixed rejected");
    CHECK(console_cn_cp_cols(0x4E2D) == 2, "cp cols CJK 2");
    CHECK(console_cn_display_cols("a\xE4\xB8\xAD" "b", 5) == 4,
          "display cols a[zhong]b = 4");
    /* write path: home the cursor first so the col assertions do not
     * depend on where the CHECK output above happened to wrap. Sample
     * the cursor AFTER each write but BEFORE the CHECK prints (the
     * CHECK output itself moves the cursor). */
    screen_console_move_cursor(0, screen_console_get()->cur_y);
    u32 cx = screen_console_get()->cur_x;
    console_cn_putcw(0x4E2D);
    u32 after_wide = screen_console_get()->cur_x;
    console_cn_putcw('x');
    u32 after_ascii = screen_console_get()->cur_x;
    CHECK(after_wide == cx + 2, "putcw advances two cells");
    CHECK(after_ascii == cx + 3, "ASCII via cn path advances one cell");
    CHECK(console_cn_backspace() >= 0, "cn backspace ok");
}

/* Write a UTF-8 file through the real VFS so the E2E can `cat` it and
 * assert the full UTF-8 echo path (cat -> console_cn -> serial tee). */
static void test_vfs_utf8_file(void) {
    screen_console_puts("[10] VFS UTF-8 file\n");
    fs_vfs_mkdir("/tmp");
    int fd = fs_vfs_open("/tmp/cn.txt",
                         VFS_O_WRONLY | VFS_O_CREAT);
    CHECK(fd >= 0, "create /tmp/cn.txt");
    if (fd >= 0) {
        const char *content = "\xE4\xB8\xAD\xE6\x96\x87\xE6\xB5\x8B\xE8\xAF\x95";
        int w = fs_vfs_write(fd, content, 12);
        fs_vfs_close(fd);
        CHECK(w == 12, "wrote 12 UTF-8 bytes");
    }
}

/* --------------------------------------------- 10. lineedit tests */

static void test_lineedit(void) {
    screen_console_puts("[11] line editing (UTF-8)\n");
    static shell_lineedit_t s;
    char buf[64];
    /* ASCII behavior unchanged: "abc" + BS -> "ab" */
    shell_lineedit_session_init(&s, "", 0);
    driver_input_keyboard_inject('a');
    driver_input_keyboard_inject('b');
    driver_input_keyboard_inject('c');
    driver_input_keyboard_inject(OC_KEY_BACKSPACE);
    driver_input_keyboard_inject(OC_KEY_ENTER);
    int n = shell_lineedit_session_readline(&s, buf, sizeof(buf));
    CHECK(n == 2 && buf[0] == 'a' && buf[1] == 'b',
          "ascii line + backspace unchanged");

    /* CJK input via the serial bridge bias (+0x100): E4 B8 AD (zhong) */
    shell_lineedit_session_init(&s, "", 0);
    driver_input_keyboard_inject('x');
    driver_input_keyboard_inject(0x100 + 0xE4);
    driver_input_keyboard_inject(0x100 + 0xB8);
    driver_input_keyboard_inject(0x100 + 0xAD);
    driver_input_keyboard_inject(OC_KEY_ENTER);
    n = shell_lineedit_session_readline(&s, buf, sizeof(buf));
    CHECK(n == 4 && (u8)buf[0] == 'x' && (u8)buf[1] == 0xE4 &&
          (u8)buf[2] == 0xB8 && (u8)buf[3] == 0xAD,
          "UTF-8 bytes stored intact");

    /* CJK backspace: x + zhong + BS -> "x" (whole 3-byte char gone) */
    shell_lineedit_session_init(&s, "", 0);
    driver_input_keyboard_inject('x');
    driver_input_keyboard_inject(0x100 + 0xE4);
    driver_input_keyboard_inject(0x100 + 0xB8);
    driver_input_keyboard_inject(0x100 + 0xAD);
    driver_input_keyboard_inject(OC_KEY_BACKSPACE);
    driver_input_keyboard_inject(OC_KEY_ENTER);
    n = shell_lineedit_session_readline(&s, buf, sizeof(buf));
    CHECK(n == 1 && buf[0] == 'x', "backspace removes one CJK char");

    /* LEFT over CJK: x + zhong + LEFT + 'y' -> insert between -> "x?y?" */
    shell_lineedit_session_init(&s, "", 0);
    driver_input_keyboard_inject('x');
    driver_input_keyboard_inject(0x100 + 0xE4);
    driver_input_keyboard_inject(0x100 + 0xB8);
    driver_input_keyboard_inject(0x100 + 0xAD);
    driver_input_keyboard_inject(OC_KEY_LEFT);
    driver_input_keyboard_inject('y');
    driver_input_keyboard_inject(OC_KEY_ENTER);
    n = shell_lineedit_session_readline(&s, buf, sizeof(buf));
    CHECK(n == 5 && buf[0] == 'x' && buf[1] == 'y' &&
          (u8)buf[2] == 0xE4, "LEFT steps one CJK char, insert mid-line");
}

/* -------------------------------------------- 11. font engine tests */

static const u8 fake_glyph[32] = {0};
static const u8* fake_get(cn_font_t* self, u32 cp) {
    (void)self;
    return (cp == 0xE000) ? fake_glyph : NULL;
}
static void test_font_engine(void) {
    screen_console_puts("[12] font engine registry\n");
    cn_font_t fake = {
        .name = "fake-private", .get_bitmap = fake_get,
        .glyph_w = 16, .glyph_h = 16, .priority = 5, .in_use = 0,
    };
    int before = font_count();
    int rc = font_register(&fake);
    CHECK(rc >= 0, "font_register accepts");
    CHECK(font_count() == before + 1, "count increments");
    CHECK(font_get_bitmap(0xE000) != NULL, "registered font resolves cp");
    CHECK(font_unregister("fake-private") == 0, "font_unregister");
    CHECK(font_count() == before, "count restored");
    CHECK(font_get_bitmap(0xE000) == NULL, "unregistered cp unresolved");
    CHECK(font_unregister("no-such-font") == -3, "unknown name rejected");
}

/* ---------------------------------------------- 12. TUI cn tests */

static void test_tui_cn(void) {
    screen_console_puts("[13] TUI chinese layer\n");
    /* the print path needs a live TUI session with a focused window */
    if (!tui_started()) tui_init();
    tui_window_t *cnwin = tui_window_new(0, 0, 6, 40);
    if (cnwin) tui_window_focus_set(cnwin);
    int prc = tui_chinese_print("\xE4\xB8\xAD\xE6\x96\x87");
    if (prc != 0) {
        char dbg[16];
        screen_console_puts("    debug: cnwin=");
        u64_to_str((u64)(long)cnwin, dbg);
        screen_console_puts(dbg);
        screen_console_puts(" wincount=");
        u64_to_str((u64)tui_window_count(), dbg);
        screen_console_puts(dbg);
        screen_console_puts(" started=");
        u64_to_str((u64)tui_started(), dbg);
        screen_console_puts(dbg);
        screen_console_putc('\n');
    }
    CHECK(prc == 0, "tui_chinese_print utf-8 ok");
    CHECK(tui_chinese_str_cols("\xE4\xB8\xAD\xE6\x96\x87") == 4,
          "str cols zhongwen = 4");
    u32 cands[9];
    int n = tui_chinese_ime_test("ni", cands, 9);
    CHECK(n > 0 && cands[0] == 0x4F60, "IME 'ni' -> ni (U+4F60) first");
    n = tui_chinese_ime_test("zhong", cands, 9);
    int has_zhong = 0;
    for (int i = 0; i < n && i < 9; i++) {
        if (cands[i] == 0x4E2D) has_zhong = 1;
    }
    CHECK(n > 0 && has_zhong, "IME 'zhong' -> zhong in candidates");
    CHECK(tui_chinese_ime_test("zzzzz", cands, 9) == 0,
          "IME nonsense syllable -> 0 candidates");
    CHECK(tui_chinese_ime_syllables() == 396, "396 syllables loaded");
    CHECK(tui_chinese_ime_chars() == 3755, "3755 level-1 chars loaded");
    /* menu/form argument validation */
    CHECK(tui_chinese_menu(NULL, NULL, 0) == -1, "menu null rejected");
    CHECK(tui_chinese_form(NULL, NULL, 0) == -1, "form null rejected");
    if (cnwin) tui_window_free(cnwin);

    /* ---- WP-10-chinese_support-fix1: user-facing input paths ---- */
    screen_console_puts("[14] fix1 input paths (ASCII editor + IME keys)\n");
    if (!tui_started()) tui_init();
    tui_window_t *inwin = tui_window_new(0, 0, 6, 40);
    if (inwin) tui_window_focus_set(inwin);
    if (inwin) {
        /* ASCII editor: plain lowercase letters must survive verbatim
         * (the SSID field used to route them into the pinyin buffer). */
        driver_input_keyboard_inject((u16)'h');
        driver_input_keyboard_inject((u16)'o');
        driver_input_keyboard_inject((u16)'m');
        driver_input_keyboard_inject((u16)'e');
        driver_input_keyboard_inject(OC_KEY_ENTER);
        char ssid[32];
        int rn = tui_chinese_input_ascii(inwin, "SSID: ", ssid, sizeof(ssid));
        CHECK(rn == 4, "ascii input length == 4");
        CHECK(strcmp(ssid, "home") == 0, "ascii input text == home");
        /* IME editor: n i 1 -> the first candidate of "ni" (U+4F60)
         * commits, Enter returns the composed UTF-8. */
        driver_input_keyboard_inject((u16)'n');
        driver_input_keyboard_inject((u16)'i');
        driver_input_keyboard_inject((u16)'1');
        driver_input_keyboard_inject(OC_KEY_ENTER);
        char cn[32];
        int cn_n = tui_chinese_input(inwin, "拼音> ", cn, sizeof(cn));
        CHECK(cn_n == 3, "IME composed length == 3 (one hanzi)");
        CHECK(cn_n == 3 && (u8)cn[0] == 0xE4 && (u8)cn[1] == 0xBD &&
              (u8)cn[2] == 0xA0, "IME composed text == 你 (U+4F60)");
        tui_window_free(inwin);
    }

    /* ---- fix1: the console ime command line (was status-only) ----
     * The usage text promised interactive composition the command did
     * not have.  ime_console_compose is that flow now: injected keys
     * must compose and commit exactly like the TUI pad. */
    screen_console_puts("[15] fix1 console IME line (n i 1 Enter -> 你; digits direct; Esc cancels)\n");
    {
        extern int ime_console_compose(u8 *out, int cap);
        driver_input_keyboard_inject((u16)'n');
        driver_input_keyboard_inject((u16)'i');
        driver_input_keyboard_inject((u16)'1');
        driver_input_keyboard_inject(OC_KEY_ENTER);
        u8 line[64];
        int rn2 = ime_console_compose(line, sizeof(line));
        CHECK(rn2 == 3, "console IME composed length == 3 (one hanzi)");
        CHECK(rn2 == 3 && line[0] == 0xE4 && line[1] == 0xBD &&
              line[2] == 0xA0, "console IME composed text == 你 (U+4F60)");
        /* letters always compose pinyin (same contract as the TUI
         * pad): "home" is no syllable, so Enter ends the line with
         * nothing committed - the pending letters are discarded. */
        driver_input_keyboard_inject((u16)'h');
        driver_input_keyboard_inject((u16)'o');
        driver_input_keyboard_inject((u16)'m');
        driver_input_keyboard_inject((u16)'e');
        driver_input_keyboard_inject(OC_KEY_ENTER);
        u8 line3[64];
        int rn3 = ime_console_compose(line3, sizeof(line3));
        CHECK(rn3 == 0, "non-syllable letters commit nothing (pinyin contract)");
        /* digits/symbols land verbatim while no pinyin is pending */
        driver_input_keyboard_inject((u16)'2');
        driver_input_keyboard_inject((u16)'0');
        driver_input_keyboard_inject((u16)'2');
        driver_input_keyboard_inject((u16)'6');
        driver_input_keyboard_inject(OC_KEY_ENTER);
        u8 line5[64];
        int rn5 = ime_console_compose(line5, sizeof(line5));
        CHECK(rn5 == 4, "console IME digits length == 4");
        CHECK(rn5 == 4 && strcmp((const char *)line5, "2026") == 0,
              "console IME digits text == 2026");
        /* Esc discards: inject n i then Esc -> -1 and empty buffer */
        driver_input_keyboard_inject((u16)'n');
        driver_input_keyboard_inject((u16)'i');
        driver_input_keyboard_inject(OC_KEY_ESC_KEY);
        u8 line4[64];
        int rn4 = ime_console_compose(line4, sizeof(line4));
        CHECK(rn4 == -1, "console IME Esc cancels (-1)");
    }
}

/* ------------------------------------------------- entry point ----- */

int chinese_test_run(int verbose) {
    (void)verbose;
    g_total = 0;
    g_failed = 0;
    chinese_font_init();
    encoding_init();

    test_font_data();
    test_font_lookup();
    test_font_manage();
    test_gb2312();
    test_utf8();
    test_convert();
    test_detect();
    test_render();
    test_console();
    test_lineedit();
    test_font_engine();
    test_tui_cn();
    test_vfs_utf8_file();

    screen_console_puts("chinesetest: ");
    char num[12];
    u64_to_str((u64)(g_total - g_failed), num);
    screen_console_puts(num);
    screen_console_puts("/");
    u64_to_str((u64)g_total, num);
    screen_console_puts(num);
    screen_console_puts(g_failed == 0 ? " PASS\n" : " FAIL\n");
    /* fix2: test_tui_cn() calls tui_init() (clears the framebuffer and
     * hides the cursor mid-suite) - without a matching tui_shutdown()
     * the shell kept running with an invisible cursor and a wiped
     * screen after the suite. Mirror tui-test's defensive close. */
    if (tui_started()) tui_shutdown();
    return g_failed;
}
