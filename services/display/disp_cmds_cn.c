/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP03 - services/display: CJK surface op bodies.
 *
 * Rescued verbatim out of chinese/chinese_cmd.c (the user-plane
 * command registration died with the oc> shell; these bodies serve
 * the L1SVC "display" ops through disp_l1.c).  The IME command pair
 * (ime / ime-test) is DELETED in WP-03 together with the input-method
 * engine itself (pinyin composition) - the spec removes the whole
 * input-method user plane; the status line that reported IME
 * syllable counts went with it.
 */
#include "chinese_font.h"
#include "encoding.h"
#include "render_cn.h"
#include "console_cn.h"
#include "screen_console.h"
#include "screen_font.h"
#include "lib_string.h"

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
