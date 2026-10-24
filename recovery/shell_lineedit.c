/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-10-wp08fix1
 * File: shell/shell_lineedit.c
 * Purpose: Full-featured line editor for the kernel shell (oc>).
 *
 * Implements the WP-08 completion spec for the kernel console:
 *   - Up/Down command history with paging (Up = older, Down = newer;
 *     the draft line typed before pressing Up is preserved)
 *   - Left/Right in-line cursor movement, Home/End, Ctrl+A / Ctrl+E
 *   - Backspace, Delete, Ctrl+U (kill to line start), Ctrl+K (kill to
 *     line end), Ctrl+W (kill the previous word)
 *   - Ctrl+C cancels the current line (prints ^C, delivers empty line)
 *   - Tab completion: first word from the shell command table, later
 *     words from the VFS directory contents (fs_vfs_readdir)
 *
 * Rendering uses only \r / re-print / ' ' / \b - the framebuffer text
 * console has no ANSI escape support, and the same trick keeps the
 * serial mirror clean.
 *
 * The session engine (shell_lineedit_t) is reusable; SYS_READLINE keeps
 * its own separate minimal implementation for ring-3 (ush), while the
 * kernel shell drives the global session below.
 */

#include "shell_lineedit.h"
#include "shell.h"
#include "driver_input_keyboard.h"
#include "screen_console.h"
#include "fs_vfs.h"
#include "lib_string.h"
#include "mem_heap.h"
#include "config_gen.h"   /* WP-13 multi-config */
#if OC_CONFIG_CHINESE
#include "../chinese/console_cn.h"
#endif

/* ---- WP-10-chinese_support: UTF-8 helpers ---------------------------
 * The buffer stays BYTE oriented (UTF-8 storage); cursor movement,
 * backspace/delete and width math are CHARACTER aware. For pure-ASCII
 * lines every helper below is byte-identical to the old ±1 behavior,
 * so the ASCII input path cannot change behavior. */

/* Byte i is a UTF-8 continuation byte (10xxxxxx). */
static int le_is_cont(u8 b) { return (b & 0xC0) == 0x80; }

/* Length of the UTF-8 sequence starting at s[0] (1..4, clamped to run
 * of non-continuation bytes). */
static int le_char_len(const char *s, int remaining) {
    u8 b0 = (u8)s[0];
    int n = 1;
    if (b0 < 0x80) return 1;
    else if ((b0 & 0xE0) == 0xC0) n = 2;
    else if ((b0 & 0xF0) == 0xE0) n = 3;
    else if ((b0 & 0xF8) == 0xF0) n = 4;
    else return 1; /* stray byte: treat as one unit */
    if (n > remaining) n = remaining;
    for (int i = 1; i < n; i++) {
        if (!le_is_cont((u8)s[i])) return i;
    }
    return n;
}

/* Start offset of the character BEFORE byte offset p (p > 0). */
static int le_prev_char(const char *buf, int p) {
    if (p <= 0) return 0;
    int q = p - 1;
    while (q > 0 && le_is_cont((u8)buf[q])) q--;
    return q;
}

/* Offset just after the character starting AT byte offset p. */
static int le_next_char(const char *buf, int len, int p) {
    if (p >= len) return len;
    return p + le_char_len(buf + p, len - p);
}

/* Console columns the byte range [0,len) occupies (CJK = 2). */
static int le_display_width(const char *buf, int len) {
    int w = 0, i = 0;
    while (i < len) {
        u32 cp = 0x20;
        u32 consumed = 0;
        if (encoding_utf8_decode((const u8 *)buf + i, (u32)(len - i),
                                 &cp, &consumed) != 0) {
            cp = 0x20;
        }
        w += (int)console_cn_cp_cols(cp);
        i += (consumed > 0) ? (int)consumed : 1;
    }
    return w;
}

/* ---- Forward declarations (session-level helpers used above their
 * definition sites) ---- */
static void le_session_redraw(shell_lineedit_t *s);
static void shell_lineedit_cursor_move_session(shell_lineedit_t *s, int dir);
static int shell_lineedit_session_tab_complete(shell_lineedit_t *s);

/* ------------------------------------------------------------------ *
 * Global (kernel shell) session
 * ------------------------------------------------------------------ */

/* The global (kernel shell) session is heap-allocated: the session
 * struct is ~9 KiB (32x256 history + buffers) and the kernel .bss must
 * stay below the 0x400000 identity-window boundary (see editor.c). */
static shell_lineedit_t *g_le;

/* Add `cmd` to one session's history ring (skips empty lines and
 * consecutive duplicates; drops the oldest entry when full). */
static void le_session_history_add(shell_lineedit_t *s, const char *cmd) {
    if (!cmd || !cmd[0]) return;
    if (s->hist_count > 0 &&
        strcmp(s->hist[s->hist_count - 1], cmd) == 0) {
        return;
    }
    if (s->hist_count < SHELL_LINEEDIT_HISTORY_MAX) {
        strncpy(s->hist[s->hist_count], cmd, SHELL_LINEEDIT_BUF_MAX - 1);
        s->hist[s->hist_count][SHELL_LINEEDIT_BUF_MAX - 1] = 0;
        s->hist_count++;
    } else {
        for (int i = 0; i + 1 < SHELL_LINEEDIT_HISTORY_MAX; i++) {
            strncpy(s->hist[i], s->hist[i + 1], SHELL_LINEEDIT_BUF_MAX);
        }
        strncpy(s->hist[SHELL_LINEEDIT_HISTORY_MAX - 1], cmd,
                SHELL_LINEEDIT_BUF_MAX - 1);
        s->hist[SHELL_LINEEDIT_HISTORY_MAX - 1][SHELL_LINEEDIT_BUF_MAX - 1] = 0;
    }
}

void shell_lineedit_init(void) {
    if (!g_le) {
        g_le = (shell_lineedit_t *)kmalloc(sizeof(shell_lineedit_t));
    }
    if (!g_le) return;
    shell_lineedit_session_init(g_le, "", 1);
    g_le->completer = shell_lineedit_default_completer;
}

void shell_lineedit_history_add(const char *cmd) {
    if (!g_le) return;
    le_session_history_add(g_le, cmd);
}

const char *shell_lineedit_history_get(int idx) {
    if (!g_le || idx < 0 || idx >= g_le->hist_count) return NULL;
    return g_le->hist[idx];
}

int shell_lineedit_history_count(void) {
    return g_le ? g_le->hist_count : 0;
}

int shell_lineedit_ctrlc(void) {
    if (!g_le) return 0;
    int hit = g_le->ctrlc_hit;
    g_le->ctrlc_hit = 0;
    return hit;
}

/* ------------------------------------------------------------------ *
 * Key pump (blocking) - shared by every session
 * ------------------------------------------------------------------ */

static int le_getch_blocking(void) {
    for (;;) {
        int k = driver_input_keyboard_getch();
        if (k >= 0) return k;
        /* Enable interrupts so keyboard/serial IRQs fire, then halt. */
        __asm__ volatile("sti");
        __asm__ volatile("hlt");
    }
}

/* ------------------------------------------------------------------ *
 * Session engine
 * ------------------------------------------------------------------ */

int shell_lineedit_session_init(shell_lineedit_t *s, const char *prompt,
                                int use_history) {
    if (!s) return -1;
    memset(s->buf, 0, sizeof(s->buf));
    s->len = 0;
    s->cursor = 0;
    s->last_drawn_w = 0;
    shell_lineedit_session_set_prompt(s, prompt ? prompt : "");
    memset(s->hist, 0, sizeof(s->hist));
    s->hist_count = 0;
    s->hist_nav = -1;
    s->hist_draft[0] = 0;
    s->use_history = use_history ? 1 : 0;
    s->ctrlc_hit = 0;
    s->reading = 0;
    s->completer = NULL;
    return 0;
}

void shell_lineedit_session_set_prompt(shell_lineedit_t *s, const char *prompt) {
    if (!s) return;
    if (!prompt) prompt = "";
    strncpy(s->prompt, prompt, SHELL_LINEEDIT_PROMPT_MAX - 1);
    s->prompt[SHELL_LINEEDIT_PROMPT_MAX - 1] = 0;
    s->prompt_len = (int)strlen(s->prompt);
}

/* Redraw the input line: \r, prompt, whole line, clearing spaces for the
 * widest tail, \r, prompt, chars before the cursor, backspaces to the
 * cursor column. WP-10-chinese_support: text goes through the cn write
 * path (UTF-8 aware); for pure-ASCII lines the emitted byte stream is
 * identical to the old implementation (console_cn_putcw routes ASCII to
 * the stock putc one-for-one).
 * WP-10-chinese_support-fix4: the erase run covers not only the cells
 * right of the cursor in the CURRENT line but also the tail of the
 * PREVIOUS draw (last_drawn_w) - otherwise navigating history from a
 * long entry to a short one (or Ctrl+U/Ctrl+K) leaves stale characters
 * floating after the new shorter content. */
static void le_session_redraw(shell_lineedit_t *s) {
    int full_w = le_display_width(s->buf, s->len);
    int pre_w = le_display_width(s->buf, s->cursor);
    int tail = full_w - pre_w;   /* cells still occupied right of cursor */
    int stale = s->last_drawn_w - pre_w; /* tail of the previous draw */
    if (stale > tail) tail = stale;
    if (tail < 0) tail = 0;
    s->last_drawn_w = full_w;
    screen_console_putc('\r');
    screen_console_puts(s->prompt);
    if (s->len > 0) console_cn_puts(s->buf);
    for (int i = 0; i < tail + 1; i++) screen_console_putc(' '); /* erase */
    for (int i = 0; i < tail + 1; i++) screen_console_putc('\b');
    screen_console_putc('\r');
    screen_console_puts(s->prompt);
    if (s->cursor > 0) {
        /* Print the prefix up to the cursor. */
        char saved = s->buf[s->cursor];
        s->buf[s->cursor] = 0;
        console_cn_puts(s->buf);
        s->buf[s->cursor] = saved;
        /* The console cursor sits at prompt+pre_w; no backtracking needed
         * (the previous implementation relied on the same invariant). */
    }
}

/* Move the session cursor. dir: <0 left (steps), >0 right, HOME, END.
 * WP-10-chinese_support: steps are CHARACTERS (UTF-8 aware); for ASCII
 * lines this is byte-identical to the old ±1 stepping. */
void shell_lineedit_cursor_move(int dir) {
    shell_lineedit_cursor_move_session(g_le, dir);
}

static void shell_lineedit_cursor_move_session(shell_lineedit_t *s, int dir) {
    if (!s || !s->reading) return;
    switch (dir) {
        case SHELL_LINEEDIT_HOME:
            s->cursor = 0;
            break;
        case SHELL_LINEEDIT_END:
            s->cursor = s->len;
            break;
        case SHELL_LINEEDIT_LEFT:
            if (s->cursor > 0) s->cursor = le_prev_char(s->buf, s->cursor);
            break;
        case SHELL_LINEEDIT_RIGHT:
            if (s->cursor < s->len) {
                s->cursor = le_next_char(s->buf, s->len, s->cursor);
            }
            break;
        default:
            if (dir < 0) {
                for (int i = 0; i < -dir && s->cursor > 0; i++) {
                    s->cursor = le_prev_char(s->buf, s->cursor);
                }
            } else {
                for (int i = 0; i < dir && s->cursor < s->len; i++) {
                    s->cursor = le_next_char(s->buf, s->len, s->cursor);
                }
            }
            break;
    }
    le_session_redraw(s);
}

/* Run one Tab-completion round for a session. */
static int shell_lineedit_session_tab_complete(shell_lineedit_t *s) {
    if (!s || !s->completer) return 0;
    char out[64];
    out[0] = 0;
    int rc = s->completer(s->buf, s->cursor, out, (int)sizeof(out));
    if (rc <= 0 || out[0] == 0) return 0;
    /* Insert the completion text at the cursor. */
    int ol = (int)strlen(out);
    for (int i = 0; i < ol; i++) {
        if (s->len + 1 >= SHELL_LINEEDIT_BUF_MAX) break;
        for (int j = s->len; j > s->cursor; j--) s->buf[j] = s->buf[j - 1];
        s->buf[s->cursor++] = out[i];
        s->len++;
    }
    s->buf[s->len] = 0;
    le_session_redraw(s);
    return 1;
}

int shell_lineedit_tab_complete(void) {
    return shell_lineedit_session_tab_complete(g_le);
}

/* Kill (delete) the previous word - shared by Ctrl+W.
 * WP-10-chinese_support: word stepping is character based so a CJK
 * run is killed as one word, not byte-fragmented. */
static void le_kill_prev_word(shell_lineedit_t *s) {
    int p = s->cursor;
    while (p > 0 && s->buf[le_prev_char(s->buf, p)] == ' ') {
        p = le_prev_char(s->buf, p);
    }
    while (p > 0 && s->buf[le_prev_char(s->buf, p)] != ' ') {
        p = le_prev_char(s->buf, p);
    }
    if (p == s->cursor) return;
    int removed = s->cursor - p;
    for (int i = p; i <= s->len - removed; i++) s->buf[i] = s->buf[i + removed];
    s->len -= removed;
    s->cursor = p;
    s->buf[s->len] = 0;
    le_session_redraw(s);
}

int shell_lineedit_session_readline(shell_lineedit_t *s, char *buf, int cap) {
    if (!s || !buf || cap <= 0) return 0;
    s->reading = 1;
    s->len = 0;
    s->cursor = 0;
    s->buf[0] = 0;
    s->ctrlc_hit = 0;
    s->hist_nav = -1;
    s->hist_draft[0] = 0;
    s->last_drawn_w = 0;  /* fix4: fresh line - nothing drawn yet   */

    screen_console_puts(s->prompt);

    int done = 0;
    int cancelled = 0;
    while (!done) {
        int k = le_getch_blocking();
        switch (k) {
            case OC_KEY_ENTER:
            case '\n':
                screen_console_putc('\n');
                done = 1;
                break;
            case OC_KEY_CTRL_C:   /* 0x03 (OC_KEY_CTRL_C == 0x03) */
                screen_console_puts("^C\n");
                s->len = 0;
                s->cursor = 0;
                s->buf[0] = 0;
                s->ctrlc_hit = 1;
                cancelled = 1;
                done = 1;
                break;
            case OC_KEY_BACKSPACE:
            case 0x7F:
                if (s->cursor > 0) {
                    /* WP-10-chinese_support: remove ONE character (a full
                     * UTF-8 sequence), not one byte. */
                    int p = le_prev_char(s->buf, s->cursor);
                    int removed = s->cursor - p;
                    for (int i = p; i <= s->len - removed; i++) {
                        s->buf[i] = s->buf[i + removed];
                    }
                    s->cursor = p;
                    s->len -= removed;
                    s->buf[s->len] = 0;
                    le_session_redraw(s);
                }
                break;
            case OC_KEY_DEL:
                if (s->cursor < s->len) {
                    /* Remove the character AT the cursor. */
                    int n = le_next_char(s->buf, s->len, s->cursor) - s->cursor;
                    for (int i = s->cursor; i <= s->len - n; i++) {
                        s->buf[i] = s->buf[i + n];
                    }
                    s->len -= n;
                    s->buf[s->len] = 0;
                    le_session_redraw(s);
                }
                break;
            case OC_KEY_UP:
                if (s->use_history && s->hist_count > 0) {
                    if (s->hist_nav == -1) {
                        /* Save the draft, jump to the newest entry. */
                        strncpy(s->hist_draft, s->buf, SHELL_LINEEDIT_BUF_MAX - 1);
                        s->hist_draft[SHELL_LINEEDIT_BUF_MAX - 1] = 0;
                        s->hist_nav = s->hist_count - 1;
                    } else if (s->hist_nav > 0) {
                        s->hist_nav--;      /* page to older entries */
                    } else {
                        break;              /* already the oldest */
                    }
                    strncpy(s->buf, s->hist[s->hist_nav], SHELL_LINEEDIT_BUF_MAX - 1);
                    s->buf[SHELL_LINEEDIT_BUF_MAX - 1] = 0;
                    s->len = (int)strlen(s->buf);
                    s->cursor = s->len;
                    le_session_redraw(s);
                }
                break;
            case OC_KEY_DOWN:
                if (s->use_history && s->hist_nav != -1) {
                    if (s->hist_nav < s->hist_count - 1) {
                        s->hist_nav++;
                        strncpy(s->buf, s->hist[s->hist_nav],
                                SHELL_LINEEDIT_BUF_MAX - 1);
                    } else {
                        /* Back to the draft line. */
                        s->hist_nav = -1;
                        strncpy(s->buf, s->hist_draft, SHELL_LINEEDIT_BUF_MAX - 1);
                    }
                    s->buf[SHELL_LINEEDIT_BUF_MAX - 1] = 0;
                    s->len = (int)strlen(s->buf);
                    s->cursor = s->len;
                    le_session_redraw(s);
                }
                break;
            case OC_KEY_LEFT:
                if (s->cursor > 0) {
                    s->cursor = le_prev_char(s->buf, s->cursor);
                    le_session_redraw(s);
                }
                break;
            case OC_KEY_RIGHT:
                if (s->cursor < s->len) {
                    s->cursor = le_next_char(s->buf, s->len, s->cursor);
                    le_session_redraw(s);
                }
                break;
            case OC_KEY_HOME:
                s->cursor = 0;
                le_session_redraw(s);
                break;
            case OC_KEY_END:
                s->cursor = s->len;
                le_session_redraw(s);
                break;
            case 0x01:   /* Ctrl+A: line start */
                s->cursor = 0;
                le_session_redraw(s);
                break;
            case 0x05:   /* Ctrl+E: line end */
                s->cursor = s->len;
                le_session_redraw(s);
                break;
            case 0x15:   /* Ctrl+U: kill to line start */
                if (s->cursor > 0) {
                    int rest = s->len - s->cursor;
                    for (int i = 0; i < rest; i++) {
                        s->buf[i] = s->buf[s->cursor + i];
                    }
                    s->len = rest;
                    s->buf[s->len] = 0;
                    s->cursor = 0;
                    le_session_redraw(s);
                }
                break;
            case 0x0B:   /* Ctrl+K: kill to line end */
                if (s->cursor < s->len) {
                    s->len = s->cursor;
                    s->buf[s->len] = 0;
                    le_session_redraw(s);
                }
                break;
            case 0x17:   /* Ctrl+W: kill previous word */
                le_kill_prev_word(s);
                break;
            case OC_KEY_TAB:   /* '\t' == 0x09 == OC_KEY_TAB */
                shell_lineedit_session_tab_complete(s);
                break;
            case OC_KEY_ESC:
                /* Bare ESC: ignore (escape sequences are already decoded
                 * into OC_KEY_* codes by the keyboard driver). */
                break;
            default:
                if (k >= 0x20 && k < 0x7F && s->len + 1 < SHELL_LINEEDIT_BUF_MAX) {
                    /* Insert at the cursor position. */
                    for (int i = s->len; i > s->cursor; i--) {
                        s->buf[i] = s->buf[i - 1];
                    }
                    s->buf[s->cursor++] = (char)k;
                    s->len++;
                    s->buf[s->len] = 0;
                    if (s->cursor == s->len) {
                        /* Appending: just echo (avoids a full redraw). */
                        screen_console_putc((char)k);
                    } else {
                        le_session_redraw(s);
                    }
                } else if (k >= 0x100 && k < 0x200 &&
                           s->len + 1 < SHELL_LINEEDIT_BUF_MAX) {
                    /* WP-10-chinese_support: one UTF-8 byte from the
                     * serial bridge (stored biased by +0x100 so it never
                     * collides with OC_KEY_* codes). Bytes accumulate in
                     * the buffer; the display is redrawn per byte which
                     * renders partial sequences as boxes until the
                     * sequence completes - a full sequence repaints the
                     * real glyph. */
                    for (int i = s->len; i > s->cursor; i--) {
                        s->buf[i] = s->buf[i - 1];
                    }
                    s->buf[s->cursor++] = (char)(k - 0x100);
                    s->len++;
                    s->buf[s->len] = 0;
                    le_session_redraw(s);
                }
                /* Other control/special keys: ignore. */
                break;
        }
    }
    s->reading = 0;

    (void)cancelled;
    /* Add to this session's history on ENTER (skips empty + consecutive
     * duplicates). */
    if (!cancelled && s->use_history) {
        le_session_history_add(s, s->buf);
    }

    int n = s->len;
    if (n > cap - 1) n = cap - 1;
    memcpy(buf, s->buf, (usize)n);
    buf[n] = 0;
    return n;
}

/* ------------------------------------------------------------------ *
 * Global session readline
 * ------------------------------------------------------------------ */

int shell_lineedit_readline(char *buf, int cap) {
    if (!g_le) shell_lineedit_init();
    /* WP-10-chinese_support-fix4: the prompt belongs to the editor
     * session, not to the caller. The interactive loop used to print
     * "oc> " itself while the session held an EMPTY prompt, so every
     * redraw ("\r" + prompt + line) restarted at column 0 without the
     * prompt and overwrote it - visible as history/cursor keys
     * replacing "oc> " on screen. Sync the prompt from $PS1 (default
     * "oc> ") before every read; session_readline prints it once. */
    const char *ps1 = shell_getenv("PS1");
    shell_lineedit_session_set_prompt(g_le, (ps1 && ps1[0]) ? ps1 : "oc> ");
    return shell_lineedit_session_readline(g_le, buf, cap);
}

void shell_lineedit_set_completer(shell_lineedit_completer_fn fn) {
    if (!g_le) shell_lineedit_init();
    if (g_le) g_le->completer = fn;
}

/* ------------------------------------------------------------------ *
 * Default completer: command names + VFS paths
 * ------------------------------------------------------------------ */

/* Split "word start" inside `line` at `cursor`.
 * Returns the word start offset (the completion replaces from here). */
static int le_word_start(const char *line, int cursor) {
    int ws = cursor;
    while (ws > 0 && line[ws - 1] != ' ') ws--;
    return ws;
}

int shell_lineedit_default_completer(const char *line, int cursor,
                                     char *out, int out_cap) {
    if (!line || !out || out_cap <= 0) return 0;
    out[0] = 0;

    int ws = le_word_start(line, cursor);
    int wlen = cursor - ws;
    if (wlen < 0 || wlen >= 64) return 0;
    char word[64];
    memcpy(word, line + ws, (usize)wlen);
    word[wlen] = 0;

    int first_word = 1;
    for (int i = 0; i < ws; i++) {
        if (line[i] != ' ') { first_word = 0; break; }
    }

    if (first_word) {
        /* Complete a shell command name. shell_complete_command_prefix()
         * (shell.c) matches the registered command table and returns the
         * longest common prefix of all candidates; its return value is
         * the candidate count (0 = no match). */
        char match[64];
        int found = shell_complete_command_prefix(word, match, (int)sizeof(match));
        if (found <= 0) return 0;
        int blen = (int)strlen(match);
        if (blen <= wlen) return 0;   /* nothing to add */
        int add = blen - wlen;
        if (add + 2 > out_cap) add = out_cap - 2;
        if (add <= 0) return 0;
        memcpy(out, match + wlen, (usize)add);
        if (found == 1) {
            /* Single candidate: terminate the word with a space. */
            out[add++] = ' ';
        }
        out[add] = 0;
        return add;
    }

    /* Complete a path from the VFS. Split the word into dir + prefix. */
    char dir[128];
    char prefix[64];
    int dlen = 0;
    int plen = 0;
    int last_slash = -1;
    for (int i = 0; i < wlen; i++) {
        if (word[i] == '/') last_slash = i;
    }
    if (last_slash >= 0) {
        for (int i = 0; i <= last_slash && dlen < (int)sizeof(dir) - 1; i++) {
            dir[dlen++] = word[i];
        }
        dir[dlen] = 0;
        for (int i = last_slash + 1; i < wlen && plen < (int)sizeof(prefix) - 1; i++) {
            prefix[plen++] = word[i];
        }
        prefix[plen] = 0;
    } else {
        strcpy(dir, "/");
        for (int i = 0; i < wlen && plen < (int)sizeof(prefix) - 1; i++) {
            prefix[plen++] = word[i];
        }
        prefix[plen] = 0;
    }

    /* Scan the directory entries for the longest common prefix. */
    char fmatch[VFS_NAME_LEN];
    int fblen = 0;
    int ffound = 0;
    fmatch[0] = 0;
    for (int idx = 0; idx < 128; idx++) {
        fs_vfs_dirent_t e;
        memset(&e, 0, sizeof(e));
        if (fs_vfs_readdir(dir, idx, &e) < 0) break;
        if (e.name[0] == 0) break;
        int matches = 1;
        for (int j = 0; j < plen; j++) {
            if (e.name[j] != prefix[j]) { matches = 0; break; }
        }
        if (!matches) continue;
        if (ffound == 0) {
            strncpy(fmatch, e.name, VFS_NAME_LEN - 1);
            fmatch[VFS_NAME_LEN - 1] = 0;
            fblen = (int)strlen(fmatch);
        } else {
            int j = 0;
            while (j < fblen && fmatch[j] && e.name[j] &&
                   fmatch[j] == e.name[j]) {
                j++;
            }
            fblen = j;
            fmatch[fblen] = 0;
        }
        ffound++;
    }
    if (ffound == 0 || fblen <= plen) return 0;
    /* Emit the part of the match that extends the typed prefix. */
    int add = fblen - plen;
    if (add >= out_cap) add = out_cap - 1;
    memcpy(out, fmatch + plen, (usize)add);
    out[add] = 0;
    return add;
}
