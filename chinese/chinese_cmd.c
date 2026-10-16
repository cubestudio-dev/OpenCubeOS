/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-10-chinese_support
 * File: chinese/chinese_cmd.c
 * Purpose: Shell commands for the Chinese support stack:
 *   chinese      - subsystem status (font / encoding / render / IME)
 *   chinesetest  - full in-kernel regression suite
 *   font         - registered font engines
 *   font-test    - render sample glyphs to the console
 *   encoding     - codec status line
 *   encoding-test- codec round-trip quick check
 *   ime          - pinyin IME status
 *   ime-test     - IME syllable resolution quick check
 *
 * All command names, help text and output labels are English (system
 * strings stay ASCII); Chinese only appears in user content such as
 * rendered glyphs and TUI pages.
 */
#include "chinese_font.h"
#include "encoding.h"
#include "render_cn.h"
#include "console_cn.h"
#include "tui_chinese.h"
#include "../shell/shell.h"
#include "../kernel/lib/lib_string.h"
#include "../drivers/display/screen_console.h"
#include "../drivers/display/screen_font.h"
#include "../tui/tui.h"

extern int chinese_test_run(int verbose);
extern int tui_cn_hw_main(void);
extern int tui_cn_net_main(void);
extern int tui_cn_sysinfo_main(void);

/* ---- status -------------------------------------------------------- */

static int cmd_chinese(const char *args) {
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
    screen_console_puts("  ime      : pinyin, ");
    u64_to_str(tui_chinese_ime_syllables(), buf);
    screen_console_puts(buf);
    screen_console_puts(" syllables / ");
    u64_to_str(tui_chinese_ime_chars(), buf);
    screen_console_puts(buf);
    screen_console_puts(" level-1 chars\n");
    screen_console_puts("  console encoding: ");
    screen_console_puts(console_cn_encoding_name(
        console_cn_get_encoding()));
    screen_console_puts("\n");
    return 0;
}

/* ---- test ---------------------------------------------------------- */

static int cmd_chinesetest(const char *args) {
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

static int cmd_font(const char *args) {
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

static int cmd_font_test(const char *args) {
    (void)args;
    render_chinese_init();
    /* Render a sample line of mixed text at the current cursor row.
     * The string is user content: Chinese glyphs + ASCII. */
    screen_console_puts("font-test: rendering sample row (16x16 CJK):\n");
    u32 y = screen_console_get()->cur_y;
    u32 x = 0;
    render_chinese_string("\xE4\xB8\xAD\xE6\x96\x87\xE6\xB5\x8B\xE8\xAF\x95"
                          " OpenCube \xE6\xB5\x8B\xE8\xAF\x95",
                          x, y * OC_FONT_CELL_H, 0xFFFFFF, 0x000000);
    screen_console_puts("font-test: 16px row drawn above (zhongwen OpenCube shi)\n");
    /* advance the console cursor past the drawn row */
    screen_console_putc('\n');
    screen_console_putc('\n');
    return 0;
}

/* ---- encoding -------------------------------------------------------- */

static int cmd_encoding(const char *args) {
    (void)args;
    char buf[192];
    encoding_status(buf, sizeof(buf));
    screen_console_puts(buf);
    screen_console_putc('\n');
    return 0;
}

static int cmd_encoding_test(const char *args) {
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

/* ---- ime -------------------------------------------------------------- */

static int cmd_ime(const char *args) {
    (void)args;
    char num[12], buf[160];
    screen_console_puts("pinyin IME status:\n");
    screen_console_puts("  syllables : ");
    u64_to_str(tui_chinese_ime_syllables(), num);
    screen_console_puts(num);
    screen_console_putc('\n');
    screen_console_puts("  level-1   : ");
    u64_to_str(tui_chinese_ime_chars(), num);
    screen_console_puts(num);
    screen_console_puts(" chars (GB2312 zones 16-55)\n");
    screen_console_puts("  usage     : type pinyin letters, digits 1-9 pick, space = first, Enter commit\n");
    encoding_status(buf, sizeof(buf));
    screen_console_puts("  ");
    screen_console_puts(buf);
    screen_console_putc('\n');
    return 0;
}

static int cmd_ime_test(const char *args) {
    (void)args;
    u32 cands[9];
    int fails = 0;
    int n = tui_chinese_ime_test("ni", cands, 9);
    if (n <= 0 || cands[0] != 0x4F60) fails++;       /* ni -> ni */
    n = tui_chinese_ime_test("hao", cands, 9);
    int has_hao = 0;
    for (int i = 0; i < n && i < 9; i++)
        if (cands[i] == 0x597D) has_hao = 1;         /* hao -> hao */
    if (n <= 0 || !has_hao) fails++;
    if (tui_chinese_ime_test("qqq", cands, 9) != 0) fails++;
    if (fails == 0) {
        screen_console_puts("ime-test: PASS (ni/hao resolve, qqq empty)\n");
        return 0;
    }
    screen_console_puts("ime-test: FAIL\n");
    return 1;
}

/* ---- tui-cn ----------------------------------------------------------- */

static int cmd_tui_cn(const char *args) {
    (void)args;
    if (tui_chinese_init() != 0) {
        screen_console_puts("tui-cn: TUI init failed\n");
        return 1;
    }
    tui_menu_item_t items[3];
    items[0].name = "硬件配置";
    items[0].desc = "hardware config (Chinese labels)";
    items[0].userptr = 0;
    items[1].name = "无线网络";
    items[1].desc = "wireless scan/connect/status + bluetooth";
    items[1].userptr = 0;
    items[2].name = "系统信息";
    items[2].desc = "overview/processes/log/monitor/power/update/time";
    items[2].userptr = 0;
    int pages = 0;
    for (;;) {
        int cols, rows;
        tui_term_size(&cols, &rows);
        tui_window_t *menu = tui_window_new(0, 0, 5, 36);
        if (!menu) return 1;
        tui_window_title(menu, "中文界面");
        int pick = tui_chinese_menu(menu, items, 3);
        tui_window_free(menu);
        tui_update_panels();
        if (pick < 0) break;
        if (pick == 0) { tui_cn_hw_main(); pages++; }
        else if (pick == 1) { tui_cn_net_main(); pages++; }
        else if (pick == 2) { tui_cn_sysinfo_main(); pages++; }
    }
    char num[12];
    u64_to_str((u64)pages, num);
    screen_console_puts("tui-cn: opened ");
    screen_console_puts(num);
    screen_console_puts(" Chinese apps, shell ok\n");
    return 0;
}

/* ---- registration ------------------------------------------------------ */

void chinese_cmd_register(void) {
    shell_register_command_ex("chinese", cmd_chinese,
        "chinese support status (font/encoding/render/ime)",
        "WP-10-chinese_support");
    shell_register_command_ex("chinesetest", cmd_chinesetest,
        "run the full Chinese support regression suite",
        "WP-10-chinese_support");
    shell_register_command_ex("font", cmd_font,
        "list registered fonts and the active renderer",
        "WP-10-chinese_support");
    shell_register_command_ex("font-test", cmd_font_test,
        "render a mixed CJK/ASCII sample row on the console",
        "WP-10-chinese_support");
    shell_register_command_ex("encoding", cmd_encoding,
        "encoding codec status (gb2312/utf-8/ascii)",
        "WP-10-chinese_support");
    shell_register_command_ex("encoding-test", cmd_encoding_test,
        "quick codec round-trip check (full: chinesetest)",
        "WP-10-chinese_support");
    shell_register_command_ex("ime", cmd_ime,
        "pinyin IME status and usage",
        "WP-10-chinese_support");
    shell_register_command_ex("ime-test", cmd_ime_test,
        "quick IME syllable resolution check",
        "WP-10-chinese_support");
    shell_register_command_ex("tui-cn", cmd_tui_cn,
        "Chinese TUI launcher: hardware config / wireless / system info",
        "WP-10-chinese_support");
}
