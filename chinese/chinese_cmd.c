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
 *   ime          - pinyin IME status ("ime -i": interactive console line)
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
#include "../kernel/core/core_sched.h"
#include "../drivers/display/screen_console.h"
#include "../drivers/display/screen_font.h"
#include "../drivers/input/driver_input_keyboard.h"
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
    if (!out || cap < 8) return -1;
    u32 cps[12];               /* committed codepoints (display)   */
    int ncp = 0;
    u8 composed[128];          /* committed UTF-8 (return value)   */
    int comp_len = 0;
    composed[0] = 0;
    char pinyin[8];            /* typed letters (max 6)            */
    int py_len = 0;
    pinyin[0] = 0;
    int done = 0, cancelled = 0;

    screen_console_t *con = screen_console_get();
    int pad = (con && con->cols > 8) ? (int)con->cols - 8 : 72;
    if (pad > 72) pad = 72;

    while (!done) {
        /* current candidates for the pending letters */
        u32 cands[9];
        int ncand = (py_len > 0)
                    ? tui_chinese_ime_test(pinyin, cands, 9) : 0;
        if (ncand < 0) ncand = 0;

        /* redraw the single line in place */
        screen_console_putc('\r');
        for (int i = 0; i < pad; i++) screen_console_putc(' ');
        screen_console_putc('\r');
        screen_console_puts("IME> ");
        for (int i = 0; i < ncp; i++) console_cn_putcw(cps[i]);
        if (py_len > 0) {
            screen_console_putc('[');
            screen_console_puts(pinyin);
            screen_console_putc(']');
            if (ncand > 0) {
                screen_console_putc(' ');
                for (int i = 0; i < ncand && i < 4; i++) {
                    console_cn_putcw(cands[i]);
                    char dig[3];
                    dig[0] = (char)('1' + i);
                    dig[1] = ' ';
                    dig[2] = 0;
                    screen_console_puts(dig);
                }
            }
        }

        /* next key: poll with a cooperative breather (the keyboard
         * queue returns -1 when empty, never 0) */
        int k;
        for (;;) {
            k = driver_input_keyboard_getch();
            if (k > 0) break;
            core_sched_yield();
        }

        if (k == '\r' || k == '\n') {
            done = 1;
        } else if (k == 0x1B || k == OC_KEY_ESC_KEY ||
                   k == OC_KEY_CTRL_C) {
            cancelled = 1;
            done = 1;
        } else if (k == OC_KEY_BACKSPACE || k == 0x08) {
            if (py_len > 0) {
                pinyin[--py_len] = 0;
            } else if (ncp > 0) {
                ncp--;
                /* drop the last UTF-8 character of the composition */
                int p = comp_len - 1;
                while (p > 0 && ((u8)composed[p] & 0xC0) == 0x80) p--;
                comp_len = p;
                composed[comp_len] = 0;
            }
        } else if (k >= 'a' && k <= 'z' && py_len < 6) {
            pinyin[py_len++] = (char)k;
            pinyin[py_len] = 0;
        } else if (k >= '1' && k <= '9' && py_len > 0) {
            int pick = k - '1';
            if (pick < ncand) {
                u8 u8buf[4];
                u32 produced = 0;
                if (encoding_utf8_encode(cands[pick], u8buf, 4,
                                         &produced) == 0 &&
                    comp_len + (int)produced < (int)sizeof(composed) - 1 &&
                    ncp < (int)(sizeof(cps) / sizeof(cps[0]))) {
                    for (u32 b = 0; b < produced; b++) {
                        composed[comp_len++] = (char)u8buf[b];
                    }
                    composed[comp_len] = 0;
                    cps[ncp++] = cands[pick];
                }
                py_len = 0;
                pinyin[0] = 0;
                if (ncp >= (int)(sizeof(cps) / sizeof(cps[0]))) done = 1;
            }
        } else if (k == ' ') {
            if (py_len > 0) {
                if (ncand > 0) {
                    u8 u8buf[4];
                    u32 produced = 0;
                    if (encoding_utf8_encode(cands[0], u8buf, 4,
                                             &produced) == 0 &&
                        comp_len + (int)produced <
                            (int)sizeof(composed) - 1 &&
                        ncp < (int)(sizeof(cps) / sizeof(cps[0]))) {
                        for (u32 b = 0; b < produced; b++) {
                            composed[comp_len++] = (char)u8buf[b];
                        }
                        composed[comp_len] = 0;
                        cps[ncp++] = cands[0];
                    }
                    if (ncp >= (int)(sizeof(cps) / sizeof(cps[0])))
                        done = 1;
                }
                py_len = 0;
                pinyin[0] = 0;
            } else if (comp_len + 1 < (int)sizeof(composed) - 1 &&
                       ncp < (int)(sizeof(cps) / sizeof(cps[0]))) {
                composed[comp_len++] = ' ';
                composed[comp_len] = 0;
                cps[ncp++] = (u32)' ';
            }
        } else if (k >= 0x20 && k < 0x7F && py_len == 0) {
            /* ASCII direct input when no pinyin is being composed */
            if (comp_len + 1 < (int)sizeof(composed) - 1 &&
                ncp < (int)(sizeof(cps) / sizeof(cps[0]))) {
                composed[comp_len++] = (char)k;
                composed[comp_len] = 0;
                cps[ncp++] = (u32)k;
            }
        }
    }

    screen_console_putc('\r');
    for (int i = 0; i < pad; i++) screen_console_putc(' ');
    screen_console_putc('\r');
    screen_console_putc('\n');
    char num[12];
    if (cancelled) {
        screen_console_puts("ime: cancelled, ");
        u64_to_str((u64)ncp, num);
        screen_console_puts(num);
        screen_console_puts(" character(s) discarded\n");
        return -1;
    }
    screen_console_puts("ime: composed ");
    u64_to_str((u64)ncp, num);
    screen_console_puts(num);
    screen_console_puts(" character(s)");
    if (ncp > 0) {
        screen_console_puts(": ");
        for (int i = 0; i < ncp; i++) console_cn_putcw(cps[i]);
    }
    screen_console_putc('\n');
    int n = comp_len;
    if (n > cap - 1) n = cap - 1;
    for (int i = 0; i < n; i++) out[i] = (u8)composed[i];
    out[n] = 0;
    return n;
}

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
    screen_console_puts("  usage     : run \"ime -i\" for the interactive line; then type pinyin\n");
    screen_console_puts("              letters; digits 1-9 pick that candidate; space = first;\n");
    screen_console_puts("              Backspace drops a letter; digits/symbols land verbatim\n");
    screen_console_puts("              while no pinyin is pending; Enter ends; Esc / Ctrl+C leave\n");
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
    tui_menu_item_t items[4];
    /* fix2 localization: these descriptions render INSIDE the Chinese
     * launcher's status bar, so they are Chinese now. Shell command
     * names and help output stay English by design (EXTENSIONS
     * WP-10-chinese_support §7: system strings stay ASCII). */
    items[0].name = "硬件配置";
    items[0].desc = "处理器/内存/存储/网络/声卡/USB/PCI 等十一页";
    items[0].userptr = 0;
    items[1].name = "无线网络";
    items[1].desc = "扫描并连接 Wi-Fi，蓝牙设备管理";
    items[1].userptr = 0;
    items[2].name = "系统信息";
    items[2].desc = "概览/进程/日志/监控/电源/更新/时间";
    items[2].userptr = 0;
    items[3].name = "中文输入";
    items[3].desc = "拼音输入板：字母组句，数字 1-9 选字，空格首选";
    items[3].userptr = 0;
    int pages = 0;
    for (;;) {
        int cols, rows;
        tui_term_size(&cols, &rows);
        tui_window_t *menu = tui_window_new(0, 0, 6, 44);
        if (!menu) return 1;
        tui_window_title(menu, "中文界面");
        int pick = tui_chinese_menu(menu, items, 4);
        tui_window_free(menu);
        tui_update_panels();
        if (pick < 0) break;
        if (pick == 0) { tui_cn_hw_main(); pages++; }
        else if (pick == 1) { tui_cn_net_main(); pages++; }
        else if (pick == 2) { tui_cn_sysinfo_main(); pages++; }
        else if (pick == 3) { tui_cn_input_app(); pages++; }
    }
    tui_shutdown();   /* symmetric with the English commands: restore
                         the shell screen and release every window */
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
        "pinyin IME status; \"ime -i\" opens the interactive console line",
        "WP-10-chinese_support");
    shell_register_command_ex("ime-test", cmd_ime_test,
        "quick IME syllable resolution check",
        "WP-10-chinese_support");
    shell_register_command_ex("tui-cn", cmd_tui_cn,
        "Chinese TUI launcher: hardware config / wireless / system info",
        "WP-10-chinese_support");
}
