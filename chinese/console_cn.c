/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-10-chinese_support
 * File: chinese/console_cn.c
 * Purpose: Console encoding state + wide-character write path.
 *
 * Control-char handling and scrolling replicate the stock putc state
 * machine (screen_console.c) so the two paths stay visually identical;
 * the stock putc itself is not touched.
 */
#include "console_cn.h"
#include "render_cn.h"
#include "chinese_font.h"
#include "../drivers/display/screen_console.h"
#include "../drivers/display/screen_font.h"
#include "../drivers/display/screen_fb.h"
#include "../core/core_sync.h"
#include "../core/lib/lib_string.h"
#include "../services/l1/l1_ext.h"

static encoding_kind_t g_enc = ENC_UTF8;
static spinlock_t g_concn_lock;
static int g_concn_ready;

int console_cn_init(void) {
    if (!g_concn_ready) {
        spin_init(&g_concn_lock);
        g_concn_ready = 1;
    }
    render_chinese_init();
    g_enc = ENC_UTF8;
    return 0;
}

int console_cn_set_encoding(encoding_kind_t enc) {
    if (enc == ENC_MIXED) return -2;
    g_enc = enc;
    return 0;
}

encoding_kind_t console_cn_get_encoding(void) {
    return g_enc;
}

const char* console_cn_encoding_name(encoding_kind_t enc) {
    switch (enc) {
    case ENC_ASCII:  return "ascii";
    case ENC_GB2312: return "gb2312";
    case ENC_UTF8:   return "utf-8";
    default:         return "mixed";
    }
}

u32 console_cn_cp_cols(u32 cp) {
    return render_chinese_cols(cp);
}

/* One wide write: same state machine as screen_console_putc, plus a
 * 16px glyph draw that spans two cells. */
void console_cn_putcw(u32 cp) {
    if (cp < 0x80) {
        screen_console_putc((char)cp);
        cn_note_write_cols(1);
        return;
    }
    /* Mirror to the L1 console hook as UTF-8 so serial tees see the
     * same stream the stock ASCII path mirrors. */
    u8 utf8[4];
    u32 produced = 0;
    if (encoding_utf8_encode(cp, utf8, 4, &produced) == 0) {
        for (u32 i = 0; i < produced; ++i) {
            if (l1_ext_console_hook(utf8[i])) {
                /* Hook suppressed the stream: keep suppressing the rest
                 * of this glyph and skip drawing, exactly like putc. */
                return;
            }
        }
    }
    screen_console_t* con = screen_console_get();
    u32 cols = render_chinese_cols(cp);

    /* fix2: a wide glyph that does not fit in the remaining columns
     * used to be silently dropped (advance without draw). Wrap to the
     * next line first so the glyph is always drawn. */
    if (con->cur_x + cols > con->cols) {
        con->cur_x = 0;
        ++con->cur_y;
        if (con->cur_y >= con->rows) {
            screen_console_scroll_up();
            con->cur_y = con->rows - 1;
        }
    }

    /* Erase cursor at the current cell. */
    if (con->cursor_visible) {
        screen_console_putc_at(con->cur_x, con->cur_y, ' ',
                               con->fg_pixel, con->bg_pixel);
    }
    /* Draw the glyph across `cols` cells (16px for CJK). */
    if (con->cur_x + cols <= con->cols) {
        u32 cw = OC_FONT_CELL_W;
        u32 chh = OC_FONT_CELL_H;
        cn_draw_glyph_px(con->cur_x * cw, con->cur_y * chh, cp,
                         con->fg_pixel, con->bg_pixel);
    }
    con->cur_x += cols;
    if (con->cur_x >= con->cols) {
        con->cur_x = 0;
        ++con->cur_y;
    }
    if (con->cur_y >= con->rows) {
        /* Scroll without the '\n' side effect (putc('\n') would erase
         * the cell under the cursor while scrolling). */
        screen_console_scroll_up();
        con->cur_y = con->rows - 1;
    }
    if (con->cursor_visible) {
        screen_console_putc_at(con->cur_x, con->cur_y, '_',
                               con->fg_pixel, con->bg_pixel);
    }
    cn_note_write_cols(cols);
}

void console_cn_puts_enc(const u8* s, u32 len, encoding_kind_t enc) {
    u32 ip = 0;
    while (ip < len) {
        u32 cp, consumed;
        int rc;
        if (enc == ENC_UTF8) {
            rc = encoding_utf8_decode(s + ip, len - ip, &cp, &consumed);
        } else {
            rc = encoding_gb2312_decode(s + ip, len - ip, &cp, &consumed);
        }
        if (rc != 0) {
            /* Invalid byte: emit it raw so users see the corruption. */
            screen_console_putc((char)s[ip]);
            ip += 1;
            continue;
        }
        console_cn_putcw(cp);
        ip += consumed;
    }
}

void console_cn_puts(const char* s) {
    if (!s) return;
    u32 len = (u32)strlen(s);
    encoding_kind_t enc = g_enc;
    if (enc == ENC_ASCII) {
        for (u32 i = 0; i < len; ++i) screen_console_putc(s[i]);
        return;
    }
    console_cn_puts_enc((const u8*)s, len, enc);
}

int console_cn_cursor_move(int dir) {
    return render_chinese_cursor_move(dir);
}

int console_cn_backspace(void) {
    return render_chinese_backspace();
}

u32 console_cn_display_cols(const char* s, u32 bytes) {
    if (!s) return 0;
    u32 cols = 0, ip = 0;
    const u8* p = (const u8*)s;
    while (ip < bytes) {
        u8 b = p[ip];
        if (b == '\n' || b == '\r' || b == '\b' || b == '\t') {
            ip++;
            continue;   /* control chars do not advance the count here */
        }
        u32 cp, consumed;
        if (encoding_utf8_decode(p + ip, bytes - ip, &cp, &consumed) == 0) {
            cols += render_chinese_cols(cp);
            ip += consumed;
        } else {
            cols += 1;
            ip += 1;
        }
    }
    return cols;
}
