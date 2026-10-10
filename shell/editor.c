/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-10-wp08fix1
 * File: shell/editor.c
 * Purpose: nano-style full-screen-ish text editor for the kernel shell.
 *
 * The whole file is held as an array of lines. On entry the file is
 * printed once with line numbers; afterwards only the CURRENT line is
 * redrawn in place (\r + reprint) because the framebuffer text console
 * has no ANSI escapes.
 *
 * Key bindings (ASCII only):
 *   Up/Down            select the current line
 *   Left/Right         move inside the line
 *   Home / End         line start / end
 *   Ctrl+A / Ctrl+E    line start / end
 *   Delete             remove the char at the cursor
 *   Backspace          remove the char before the cursor (merges lines)
 *   Enter              split the current line at the cursor (nano)
 *   Ctrl+K             delete the current line
 *   Ctrl+C             report cursor position (nano binding)
 *   Ctrl+G             key guide
 *   Ctrl+O             save (write the buffer back to the file)
 *   Ctrl+X             exit (asks before discarding unsaved changes)
 *
 * File I/O goes through fs_vfs_open/read/write - the editor works on
 * REAL files on any mounted filesystem.
 */

#include "editor.h"
#include "fs_vfs.h"
#include "screen_console.h"
#include "driver_input_keyboard.h"
#include "lib_string.h"
#include "mem_heap.h"

/* ---- Session state ----
 * WP-10-wp08fix1 FIX: the line array used to be a 128 KiB static (and the
 * boot loader buffer 4 KiB more). The kernel image must stay below the
 * 0x400000 identity-window boundary (the user address space splits the
 * 0x400000-0x600000 window into an empty page table, so kernel .bss above
 * 0x400000 would #PF the first time a syscall touched it). The buffers
 * are kmalloc'd per editor_open() / kfree'd per editor_close() now. */
static char (*ed_lines)[EDITOR_LINE_CAP];
static int   ed_nlines = 0;
static int   ed_cl     = 0;      /* current line  */
static int   ed_cc     = 0;      /* cursor column */
static int   ed_dirty  = 0;
static int   ed_active = 0;
static char  ed_file[EDITOR_PATH_LEN];

/* BUG-0242 FIX (A15-16): the load loop used to drop everything past the
 * session limits SILENTLY (lines beyond EDITOR_MAX_LINES, characters
 * beyond EDITOR_LINE_CAP) and treated a fs_vfs_read error like EOF, so
 * ^O then destroyed the unread part of the file (O_TRUNC save of a
 * partial view). These flags record what the load actually delivered;
 * a partial session warns on open and REFUSES to save. */
static int   ed_partial        = 0;  /* buffer holds only part of the file */
static int   ed_dropped_lines  = 0;  /* lines lost to the 512-line limit   */
static int   ed_dropped_chars  = 0;  /* chars lost to the 255-char width   */

/* ---- Internal helpers ---- */

static void ed_prefix(int lineno) {
    /* Fixed width 3 + ": " (e.g. "  7: "). */
    if (lineno < 10) screen_console_puts("  ");
    else if (lineno < 100) screen_console_putc(' ');
    {
        char num[8];
        u64_to_str((u64)lineno, num);
        screen_console_puts(num);
    }
    screen_console_putc(':');
    screen_console_putc(' ');
}

/* Redraw the current line in place. */
static void ed_redraw_line(void) {
    screen_console_putc('\r');
    ed_prefix(ed_cl + 1);
    screen_console_puts(ed_lines[ed_cl]);
    screen_console_putc(' ');          /* clear one trailing cell */
    screen_console_putc('\r');
    ed_prefix(ed_cl + 1);
    if (ed_cc > 0) {
        char saved = ed_lines[ed_cl][ed_cc];
        ed_lines[ed_cl][ed_cc] = 0;
        screen_console_puts(ed_lines[ed_cl]);
        ed_lines[ed_cl][ed_cc] = saved;
    }
}

static void ed_state(void) {
    char num[8];
    screen_console_puts("[nano ");
    screen_console_puts(ed_file);
    if (ed_partial) screen_console_puts(" PARTIAL");   /* BUG-0242 */
    screen_console_puts(" line ");
    u64_to_str((u64)(ed_cl + 1), num);
    screen_console_puts(num);
    screen_console_putc('/');
    u64_to_str((u64)ed_nlines, num);
    screen_console_puts(num);
    screen_console_puts(ed_dirty ? "  modified] ^O save  ^X exit  ^K del-line  ^C pos  ^G help\n"
                                 : "  saved   ] ^O save  ^X exit  ^K del-line  ^C pos  ^G help\n");
}

static void ed_print_all(void) {
    screen_console_puts("-- nano: ");
    screen_console_puts(ed_file);
    screen_console_puts(" -- ^O save ^X exit ^K del line ^G help --\n");
    for (int i = 0; i < ed_nlines; i++) {
        ed_prefix(i + 1);
        screen_console_puts(ed_lines[i]);
        screen_console_putc('\n');
    }
}

/* BUG-0242 FIX (A15-16): detail lines shared by the open warning and the
 * save refusal (all counters relate to the load of ed_file). */
static void ed_partial_details(void) {
    char num[16];
    if (ed_dropped_lines > 0) {
        screen_console_puts("  ");
        u64_to_str((u64)ed_dropped_lines, num);
        screen_console_puts(num);
        screen_console_puts(" line(s) over the 512-line session limit were dropped\n");
    }
    if (ed_dropped_chars > 0) {
        screen_console_puts("  ");
        u64_to_str((u64)ed_dropped_chars, num);
        screen_console_puts(num);
        screen_console_puts(" character(s) over the 255-char line width were dropped\n");
    }
    screen_console_puts("  saving now would DESTROY the dropped content\n");
    screen_console_puts("  quit without saving (^X n / :q!) and split the file first\n");
}

/* Printed once at the top of the interactive loops. */
static void ed_partial_warning(void) {
    if (!ed_partial) return;
    screen_console_puts("editor: WARNING: ");
    screen_console_puts(ed_file);
    screen_console_puts(" is only PARTIALLY loaded:\n");
    ed_partial_details();
}

static int ed_getch_blocking(void) {
    for (;;) {
        int k = driver_input_keyboard_getch();
        if (k >= 0) return k;
        __asm__ volatile("sti");
        __asm__ volatile("hlt");
    }
}

/* ---- Spec interfaces ---- */

int editor_open(const char *file) {
    if (!file || !file[0]) return -1;
    if (ed_active) return -2;

    ed_lines = (char (*)[EDITOR_LINE_CAP])kmalloc(
        (usize)EDITOR_MAX_LINES * EDITOR_LINE_CAP);
    if (!ed_lines) return -3;
    memset(ed_lines, 0, (usize)EDITOR_MAX_LINES * EDITOR_LINE_CAP);

    ed_nlines = 0;
    ed_dirty  = 0;
    ed_cl     = 0;
    ed_cc     = 0;
    /* BUG-0242 FIX (A15-16): reset the partial-load bookkeeping. */
    ed_partial       = 0;
    ed_dropped_lines = 0;
    ed_dropped_chars = 0;
    strncpy(ed_file, file, EDITOR_PATH_LEN - 1);
    ed_file[EDITOR_PATH_LEN - 1] = 0;

    int fd = fs_vfs_open(ed_file, VFS_O_RDONLY);
    if (fd >= 0) {
        char *rbuf = (char *)kmalloc(4096);
        if (!rbuf) {
            fs_vfs_close(fd);
            kfree(ed_lines);
            ed_lines = NULL;
            ed_active = 0;
            return -3;
        }
        char cur[EDITOR_LINE_CAP];
        int  cl = 0;
        cur[0] = 0;
        for (;;) {
            /* BUG-0242 FIX (A15-16): n < 0 is a READ ERROR, not EOF. The
             * old `if (n <= 0) break;` opened the editor on a partial
             * view as if the file simply ended (and ^O then truncated
             * it). A read error now aborts the open with -4 (the value
             * editor.h has always documented) and leaves the file
             * untouched. */
            int n = fs_vfs_read(fd, rbuf, 4096);
            if (n < 0) {
                kfree(rbuf);
                fs_vfs_close(fd);
                kfree(ed_lines);
                ed_lines = NULL;
                ed_nlines = 0;
                ed_active = 0;
                ed_partial = 0;
                ed_dropped_lines = 0;
                ed_dropped_chars = 0;
                return -4;
            }
            if (n == 0) break;
            for (int i = 0; i < n; i++) {
                if (rbuf[i] == '\n') {
                    if (ed_nlines < EDITOR_MAX_LINES) {
                        strncpy(ed_lines[ed_nlines++], cur, EDITOR_LINE_CAP - 1);
                        ed_lines[ed_nlines - 1][EDITOR_LINE_CAP - 1] = 0;
                    } else {
                        ed_dropped_lines++;   /* BUG-0242: counted, not silent */
                    }
                    cl = 0;
                    cur[0] = 0;
                } else if (cl + 1 < EDITOR_LINE_CAP) {
                    cur[cl++] = rbuf[i];
                    cur[cl] = 0;
                } else {
                    ed_dropped_chars++;       /* BUG-0242: counted, not silent */
                }
            }
        }
        kfree(rbuf);
        fs_vfs_close(fd);
        if (cl > 0 || ed_nlines == 0) {
            if (ed_nlines < EDITOR_MAX_LINES) {
                strncpy(ed_lines[ed_nlines++], cur, EDITOR_LINE_CAP - 1);
                ed_lines[ed_nlines - 1][EDITOR_LINE_CAP - 1] = 0;
            } else {
                ed_dropped_lines++;           /* BUG-0242: trailing partial line */
            }
        }
        /* BUG-0242 FIX (A15-16): a session that lost anything at load
         * time is partial - it warns on open and refuses to save. */
        ed_partial = (ed_dropped_lines > 0 || ed_dropped_chars > 0) ? 1 : 0;
    } else {
        /* New file: start with one empty line. */
        ed_lines[0][0] = 0;
        ed_nlines = 1;
    }
    ed_active = 1;
    return 0;
}

int editor_save(void) {
    if (!ed_active) return -1;
    /* BUG-0242 FIX (A15-16): a partially loaded buffer must never be
     * written back over the file it came from (O_TRUNC would destroy
     * everything the load dropped). Refuse loudly instead. */
    if (ed_partial) {
        screen_console_puts("editor: save REFUSED - ");
        screen_console_puts(ed_file);
        screen_console_puts(" is only PARTIALLY loaded:\n");
        ed_partial_details();
        return -5;
    }
    /* O_TRUNC clears the existing content on open (P2-15 semantics). */
    int fd = fs_vfs_open(ed_file, VFS_O_WRONLY | VFS_O_CREAT | VFS_O_TRUNC);
    if (fd < 0) return -2;
    for (int i = 0; i < ed_nlines; i++) {
        int len = (int)strlen(ed_lines[i]);
        if (len > 0) {
            if (fs_vfs_write(fd, ed_lines[i], len) < 0) {
                fs_vfs_close(fd);
                return -3;
            }
        }
        if (fs_vfs_write(fd, "\n", 1) < 0) {
            fs_vfs_close(fd);
            return -3;
        }
    }
    fs_vfs_close(fd);
    ed_dirty = 0;
    return 0;
}

int editor_close(void) {
    if (!ed_active) return -1;
    if (ed_lines) {
        kfree(ed_lines);
        ed_lines = NULL;
    }
    ed_active = 0;
    ed_nlines = 0;
    ed_file[0] = 0;
    ed_partial = 0;
    ed_dropped_lines = 0;
    ed_dropped_chars = 0;
    return 0;
}

int editor_is_active(void) {
    return ed_active ? 1 : 0;
}

const char *editor_file(void) {
    return ed_active ? ed_file : "";
}

/* ---- Interactive loop ---- */

int editor_run(void) {
    if (!ed_active) return 1;

    ed_print_all();
    ed_state();
    ed_partial_warning();   /* BUG-0242 FIX (A15-16) */
    ed_redraw_line();

    for (;;) {
        int k = ed_getch_blocking();

        if (k == OC_KEY_UP) {
            if (ed_cl > 0) {
                ed_cl--;
                int ll = (int)strlen(ed_lines[ed_cl]);
                if (ed_cc > ll) ed_cc = ll;
                ed_redraw_line();
            }
        } else if (k == OC_KEY_DOWN) {
            if (ed_cl + 1 < ed_nlines) {
                ed_cl++;
                int ll = (int)strlen(ed_lines[ed_cl]);
                if (ed_cc > ll) ed_cc = ll;
                ed_redraw_line();
            }
        } else if (k == OC_KEY_LEFT) {
            if (ed_cc > 0) { ed_cc--; ed_redraw_line(); }
        } else if (k == OC_KEY_RIGHT) {
            if (ed_cc < (int)strlen(ed_lines[ed_cl])) {
                ed_cc++; ed_redraw_line();
            }
        } else if (k == OC_KEY_HOME || k == 0x01) {   /* Home / ^A */
            ed_cc = 0;
            ed_redraw_line();
        } else if (k == OC_KEY_END || k == 0x05) {    /* End / ^E */
            ed_cc = (int)strlen(ed_lines[ed_cl]);
            ed_redraw_line();
        } else if (k == OC_KEY_DEL) {
            char *l = ed_lines[ed_cl];
            int ll = (int)strlen(l);
            if (ed_cc < ll) {
                for (int i = ed_cc; i < ll; i++) l[i] = l[i + 1];
                ed_dirty = 1;
                ed_redraw_line();
            }
        } else if (k == OC_KEY_BACKSPACE || k == 0x7F) {
            char *l = ed_lines[ed_cl];
            if (ed_cc > 0) {
                int ll = (int)strlen(l);
                for (int i = ed_cc - 1; i < ll; i++) l[i] = l[i + 1];
                ed_cc--;
                ed_dirty = 1;
                ed_redraw_line();
            } else if (ed_cl > 0) {
                /* Merge with the previous line. */
                char *prev = ed_lines[ed_cl - 1];
                int plen = (int)strlen(prev);
                int ll = (int)strlen(l);
                if (plen + ll < EDITOR_LINE_CAP - 1) {
                    strcat(prev, l);
                    ed_cc = plen;
                    ed_cl--;
                    for (int i = ed_cl + 1; i + 1 < ed_nlines; i++) {
                        strncpy(ed_lines[i], ed_lines[i + 1], EDITOR_LINE_CAP);
                    }
                    ed_nlines--;
                    ed_dirty = 1;
                    ed_print_all();
                    ed_state();
                    ed_redraw_line();
                }
            }
        } else if (k == OC_KEY_ENTER || k == '\n' || k == '\r') {
            /* ENTER: split the current line at the cursor (nano). */
            if (ed_nlines < EDITOR_MAX_LINES) {
                char *l = ed_lines[ed_cl];
                char tail[EDITOR_LINE_CAP];
                strncpy(tail, l + ed_cc, EDITOR_LINE_CAP - 1);
                tail[EDITOR_LINE_CAP - 1] = 0;
                l[ed_cc] = 0;
                for (int i = ed_nlines; i > ed_cl + 1; i--) {
                    strncpy(ed_lines[i], ed_lines[i - 1], EDITOR_LINE_CAP);
                }
                strncpy(ed_lines[ed_cl + 1], tail, EDITOR_LINE_CAP - 1);
                ed_lines[ed_cl + 1][EDITOR_LINE_CAP - 1] = 0;
                ed_nlines++;
                ed_cl++;
                ed_cc = 0;
                ed_dirty = 1;
                ed_print_all();
                ed_state();
                ed_redraw_line();
            }
        } else if (k == 0x0B) {   /* ^K: delete the current line */
            for (int i = ed_cl; i + 1 < ed_nlines; i++) {
                strncpy(ed_lines[i], ed_lines[i + 1], EDITOR_LINE_CAP);
            }
            ed_nlines--;
            if (ed_nlines == 0) {
                ed_lines[0][0] = 0;
                ed_nlines = 1;
            }
            if (ed_cl >= ed_nlines) ed_cl = ed_nlines - 1;
            int ll = (int)strlen(ed_lines[ed_cl]);
            if (ed_cc > ll) ed_cc = ll;
            ed_dirty = 1;
            ed_print_all();
            ed_state();
            ed_redraw_line();
        } else if (k == 0x03) {   /* ^C: report position (nano binding) */
            char num[8];
            screen_console_puts("\n[line ");
            u64_to_str((u64)(ed_cl + 1), num);
            screen_console_puts(num);
            screen_console_puts(", col ");
            u64_to_str((u64)(ed_cc + 1), num);
            screen_console_puts(num);
            screen_console_puts("]\n");
            ed_state();
            ed_redraw_line();
        } else if (k == 0x07) {   /* ^G: help */
            screen_console_puts("\narrows move | type to insert | BKSP/DEL delete\n");
            screen_console_puts("ENTER split line | ^K del line | ^O save | ^X exit | ^C pos\n");
            ed_state();
            ed_redraw_line();
        } else if (k == 0x0F) {   /* ^O: save */
            if (editor_save() == 0) {
                char num[8];
                screen_console_puts("\n[wrote ");
                u64_to_str((u64)ed_nlines, num);
                screen_console_puts(num);
                screen_console_puts(" lines]\n");
            } else {
                screen_console_puts("\n[save failed]\n");
            }
            ed_state();
            ed_redraw_line();
        } else if (k == 0x18) {   /* ^X: exit */
            if (ed_dirty) {
                screen_console_puts("\nSave modified buffer? (y/n): ");
                int c = ed_getch_blocking();
                screen_console_putc((char)c);
                screen_console_putc('\n');
                if (c == 'y' || c == 'Y') {
                    if (editor_save() != 0) {
                        screen_console_puts("[save failed - stay in editor]\n");
                        ed_state();
                        ed_redraw_line();
                        continue;
                    }
                    editor_close();
                    return 0;    /* saved before exit */
                }
                editor_close();
                return 1;        /* quit without saving */
            }
            editor_close();
            return 0;
        } else if (k >= 0x20 && k < 0x7F) {   /* insert a character */
            char *l = ed_lines[ed_cl];
            int ll = (int)strlen(l);
            if (ll + 1 < EDITOR_LINE_CAP) {
                ed_dirty = 1;
                if (ed_cc == ll) {
                    l[ll] = (char)k;
                    l[ll + 1] = 0;
                    screen_console_putc((char)k);
                } else {
                    for (int i = ll; i > ed_cc; i--) l[i] = l[i - 1];
                    l[ed_cc] = (char)k;
                    l[ll + 1] = 0;
                    ed_cc++;
                    ed_redraw_line();
                    continue;   /* redraw already positioned the cursor */
                }
                ed_cc++;
            }
        }
        /* Everything else: ignore. */
    }
}

/* ================================================================== *
 * BUG-0135 FIX: a real two-mode vi.
 *
 * The audit run showed `vi` was registered as a plain alias of the nano
 * engine ("same engine as nano"), so "ithree-fox" and ":wq" were both
 * taken as literal text — the claimed vi editor did not exist. Below is
 * a real modal vi built on the same line-buffer session state:
 *
 *   NORMAL (entry mode)
 *     h j k l / arrows   move (j/k clamp the column like vi)
 *     0 / $              line start / end
 *     G / g g            last line / first line
 *     x                  delete the char under the cursor
 *     d d                delete the current line
 *     i a A I o O        enter INSERT (before/at/after, line ops)
 *     :                  ex command line (w, q, wq, q!)
 *
 *   INSERT
 *     printable chars    insert at the cursor
 *     BKSP / DEL         delete before / at the cursor (BKSP merges lines)
 *     Enter              split the line at the cursor
 *     arrows / Home/End  move
 *     ESC                back to NORMAL
 *
 * The console has no ANSI escapes, so any structural change redraws the
 * whole buffer (same policy the nano engine already uses).
 * ================================================================== */

static void ed_vi_print_all(void) {
    screen_console_puts("-- vi: ");
    screen_console_puts(ed_file);
    screen_console_puts(" -- ESC leaves insert, :w saves, :q quits --\n");
    for (int i = 0; i < ed_nlines; i++) {
        ed_prefix(i + 1);
        screen_console_puts(ed_lines[i]);
        screen_console_putc('\n');
    }
}

/* vi status line (redrawn after every normal-mode command). */
static void ed_vi_state(const char *mode) {
    char num[8];
    screen_console_puts("-- ");
    screen_console_puts(mode);
    screen_console_puts(" -- ");
    screen_console_puts(ed_file);
    if (ed_partial) screen_console_puts(" PARTIAL");   /* BUG-0242 */
    screen_console_puts(" line ");
    u64_to_str((u64)(ed_cl + 1), num);
    screen_console_puts(num);
    screen_console_putc('/');
    u64_to_str((u64)ed_nlines, num);
    screen_console_puts(num);
    screen_console_puts(" col ");
    u64_to_str((u64)(ed_cc + 1), num);
    screen_console_puts(num);
    screen_console_puts(ed_dirty ? "  [+] :w :q :wq :q!\n" : "  :w :q :wq :q!\n");
}

/* INSERT-mode editing (shares nano's exact buffer semantics). */

static void ed_vi_insert_printable(int k) {
    char *l = ed_lines[ed_cl];
    int ll = (int)strlen(l);
    if (ll + 1 < EDITOR_LINE_CAP) {
        ed_dirty = 1;
        for (int i = ll; i > ed_cc; i--) l[i] = l[i - 1];
        l[ed_cc] = (char)k;
        l[ll + 1] = 0;
        ed_cc++;
        ed_redraw_line();
    }
}

static void ed_vi_split_line(void) {
    if (ed_nlines >= EDITOR_MAX_LINES) return;
    char *l = ed_lines[ed_cl];
    char tail[EDITOR_LINE_CAP];
    strncpy(tail, l + ed_cc, EDITOR_LINE_CAP - 1);
    tail[EDITOR_LINE_CAP - 1] = 0;
    l[ed_cc] = 0;
    for (int i = ed_nlines; i > ed_cl + 1; i--) {
        strncpy(ed_lines[i], ed_lines[i - 1], EDITOR_LINE_CAP);
    }
    strncpy(ed_lines[ed_cl + 1], tail, EDITOR_LINE_CAP - 1);
    ed_lines[ed_cl + 1][EDITOR_LINE_CAP - 1] = 0;
    ed_nlines++;
    ed_cl++;
    ed_cc = 0;
    ed_dirty = 1;
    ed_print_all();
    ed_vi_state("-- INSERT --");
    ed_redraw_line();
}

static void ed_vi_backspace(void) {
    char *l = ed_lines[ed_cl];
    if (ed_cc > 0) {
        int ll = (int)strlen(l);
        for (int i = ed_cc - 1; i < ll; i++) l[i] = l[i + 1];
        ed_cc--;
        ed_dirty = 1;
        ed_redraw_line();
    } else if (ed_cl > 0) {
        /* merge with the previous line */
        char *prev = ed_lines[ed_cl - 1];
        int plen = (int)strlen(prev);
        int ll = (int)strlen(l);
        if (plen + ll < EDITOR_LINE_CAP - 1) {
            strcat(prev, l);
            ed_cc = plen;
            ed_cl--;
            for (int i = ed_cl + 1; i + 1 < ed_nlines; i++) {
                strncpy(ed_lines[i], ed_lines[i + 1], EDITOR_LINE_CAP);
            }
            ed_nlines--;
            ed_dirty = 1;
            ed_print_all();
            ed_vi_state("-- INSERT --");
            ed_redraw_line();
        }
    }
}

static void ed_vi_delete_at(void) {
    char *l = ed_lines[ed_cl];
    int ll = (int)strlen(l);
    if (ed_cc < ll) {
        for (int i = ed_cc; i < ll; i++) l[i] = l[i + 1];
        ed_dirty = 1;
        ed_redraw_line();
    }
}

/* NORMAL-mode `x`: delete the char under the cursor. */
static void ed_vi_x(void) {
    ed_vi_delete_at();
}

/* NORMAL-mode `dd`: remove the current line. */
static void ed_vi_dd(void) {
    for (int i = ed_cl; i + 1 < ed_nlines; i++) {
        strncpy(ed_lines[i], ed_lines[i + 1], EDITOR_LINE_CAP);
    }
    ed_nlines--;
    if (ed_nlines == 0) {
        ed_lines[0][0] = 0;
        ed_nlines = 1;
    }
    if (ed_cl >= ed_nlines) ed_cl = ed_nlines - 1;
    int ll = (int)strlen(ed_lines[ed_cl]);
    if (ed_cc > ll) ed_cc = ll;
    ed_dirty = 1;
    ed_print_all();
    ed_vi_state("NORMAL");
    ed_redraw_line();
}

/* Read a `:` ex command line. The ':' was already echoed. Returns the
 * command length (>=0), or -1 when the operator pressed ESC. */
static int ed_vi_readcmd(char *out, int cap) {
    int n = 0;
    out[0] = 0;
    for (;;) {
        int k = ed_getch_blocking();
        if (k == OC_KEY_ESC) {
            screen_console_puts("\n");
            return -1;
        }
        if (k == OC_KEY_ENTER || k == '\n' || k == '\r') {
            screen_console_putc('\n');
            out[n] = 0;
            return n;
        }
        if ((k == OC_KEY_BACKSPACE || k == 0x7F) && n > 0) {
            n--;
            out[n] = 0;
            screen_console_puts("\b \b");
            continue;
        }
        if (k >= 0x20 && k < 0x7F && n + 1 < cap) {
            out[n++] = (char)k;
            out[n] = 0;
            screen_console_putc((char)k);
        }
    }
}

/* Execute one ex command. Returns 1 when the editor must exit, 0 to
 * stay inside the vi loop. */
static int ed_vi_excmd(const char *cmd) {
    if (strcmp(cmd, "w") == 0 || strcmp(cmd, "w!") == 0) {
        if (editor_save() == 0) {
            char num[8];
            screen_console_puts("\"");
            screen_console_puts(ed_file);
            screen_console_puts("\" ");
            u64_to_str((u64)ed_nlines, num);
            screen_console_puts(num);
            screen_console_puts("L written\n");
        } else {
            screen_console_puts("vi: write failed\n");
        }
        return 0;
    }
    if (strcmp(cmd, "q") == 0) {
        if (ed_dirty) {
            screen_console_puts("vi: no write since last change (:q! to force)\n");
            return 0;
        }
        editor_close();
        return 1;
    }
    if (strcmp(cmd, "q!") == 0) {
        editor_close();
        return 1;
    }
    if (strcmp(cmd, "wq") == 0 || strcmp(cmd, "x") == 0) {
        if (editor_save() != 0) {
            screen_console_puts("vi: write failed\n");
            return 0;
        }
        editor_close();
        return 1;
    }
    screen_console_puts("vi: not an editor command: ");
    screen_console_puts(cmd[0] ? cmd : "(empty)");
    screen_console_putc('\n');
    return 0;
}

int editor_run_vi(void) {
    if (!ed_active) return 1;

    ed_vi_print_all();       /* BUG-0135: vi header, not the nano one */
    ed_vi_state("NORMAL");
    ed_partial_warning();    /* BUG-0242 FIX (A15-16) */
    ed_redraw_line();

    int insert = 0;          /* 0 = NORMAL, 1 = INSERT */
    int pending = 0;         /* first key of a `dd` / `gg` sequence */

    for (;;) {
        int k = ed_getch_blocking();

        /* ---------- INSERT mode ---------- */
        if (insert) {
            if (k == OC_KEY_ESC) {
                insert = 0;
                if (ed_cc > 0) ed_cc--;      /* cursor onto last char (vi) */
                ed_vi_state("NORMAL");
                ed_redraw_line();
            } else if (k == OC_KEY_UP) {
                if (ed_cl > 0) {
                    ed_cl--;
                    int ll = (int)strlen(ed_lines[ed_cl]);
                    if (ed_cc > ll) ed_cc = ll;
                    ed_redraw_line();
                }
            } else if (k == OC_KEY_DOWN) {
                if (ed_cl + 1 < ed_nlines) {
                    ed_cl++;
                    int ll = (int)strlen(ed_lines[ed_cl]);
                    if (ed_cc > ll) ed_cc = ll;
                    ed_redraw_line();
                }
            } else if (k == OC_KEY_LEFT) {
                if (ed_cc > 0) { ed_cc--; ed_redraw_line(); }
            } else if (k == OC_KEY_RIGHT) {
                if (ed_cc < (int)strlen(ed_lines[ed_cl])) {
                    ed_cc++; ed_redraw_line();
                }
            } else if (k == OC_KEY_HOME) {
                ed_cc = 0; ed_redraw_line();
            } else if (k == OC_KEY_END) {
                ed_cc = (int)strlen(ed_lines[ed_cl]); ed_redraw_line();
            } else if (k == OC_KEY_BACKSPACE || k == 0x7F) {
                ed_vi_backspace();
            } else if (k == OC_KEY_DEL) {
                ed_vi_delete_at();
            } else if (k == OC_KEY_ENTER || k == '\n' || k == '\r') {
                ed_vi_split_line();
            } else if (k >= 0x20 && k < 0x7F) {
                ed_vi_insert_printable(k);
            }
            /* everything else ignored while inserting */
            continue;
        }

        /* ---------- NORMAL mode ---------- */
        if (pending != 0) {
            /* completing dd / gg */
            int first = pending;
            pending = 0;
            if (first == 'd' && k == 'd') { ed_vi_dd(); continue; }
            if (first == 'g' && k == 'g') {
                ed_cl = 0;
                int ll = (int)strlen(ed_lines[ed_cl]);
                if (ed_cc > ll) ed_cc = ll;
                ed_vi_state("NORMAL");
                ed_redraw_line();
                continue;
            }
            /* the pending key was a plain 'g' (unsupported alone): fall
             * through and handle the new key normally */
            if (first == 'g') { ed_vi_state("NORMAL"); ed_redraw_line(); continue; }
            if (first == 'd') { ed_vi_state("NORMAL"); ed_redraw_line(); continue; }
        }

        if (k == 'h' || k == OC_KEY_LEFT) {
            if (ed_cc > 0) { ed_cc--; ed_redraw_line(); }
        } else if (k == 'l' || k == OC_KEY_RIGHT) {
            if (ed_cc < (int)strlen(ed_lines[ed_cl])) { ed_cc++; ed_redraw_line(); }
        } else if (k == 'j' || k == OC_KEY_DOWN) {
            if (ed_cl + 1 < ed_nlines) {
                ed_cl++;
                int ll = (int)strlen(ed_lines[ed_cl]);
                if (ed_cc > ll) ed_cc = ll;
                ed_vi_state("NORMAL");
                ed_redraw_line();
            }
        } else if (k == 'k' || k == OC_KEY_UP) {
            if (ed_cl > 0) {
                ed_cl--;
                int ll = (int)strlen(ed_lines[ed_cl]);
                if (ed_cc > ll) ed_cc = ll;
                ed_vi_state("NORMAL");
                ed_redraw_line();
            }
        } else if (k == '0') {
            ed_cc = 0; ed_redraw_line();
        } else if (k == '$') {
            ed_cc = (int)strlen(ed_lines[ed_cl]); ed_redraw_line();
        } else if (k == 'G') {
            ed_cl = ed_nlines - 1;
            int ll = (int)strlen(ed_lines[ed_cl]);
            if (ed_cc > ll) ed_cc = ll;
            ed_vi_state("NORMAL");
            ed_redraw_line();
        } else if (k == 'g') {
            pending = 'g';
        } else if (k == 'd') {
            pending = 'd';
        } else if (k == 'x') {
            ed_vi_x();
        } else if (k == 'i') {
            insert = 1; ed_vi_state("-- INSERT --");
        } else if (k == 'a') {
            if (ed_cc < (int)strlen(ed_lines[ed_cl])) ed_cc++;
            insert = 1; ed_vi_state("-- INSERT --");
        } else if (k == 'A') {
            ed_cc = (int)strlen(ed_lines[ed_cl]);
            insert = 1; ed_vi_state("-- INSERT --");
        } else if (k == 'I') {
            ed_cc = 0;
            insert = 1; ed_vi_state("-- INSERT --");
        } else if (k == 'o') {
            /* open a line BELOW the cursor and insert there */
            if (ed_nlines < EDITOR_MAX_LINES) {
                for (int i = ed_nlines; i > ed_cl + 1; i--) {
                    strncpy(ed_lines[i], ed_lines[i - 1], EDITOR_LINE_CAP);
                }
                ed_lines[ed_cl + 1][0] = 0;
                ed_nlines++;
                ed_cl++;
                ed_cc = 0;
                ed_dirty = 1;
                ed_print_all();
                insert = 1; ed_vi_state("-- INSERT --");
                ed_redraw_line();
            }
        } else if (k == 'O') {
            /* open a line ABOVE the cursor and insert there */
            if (ed_nlines < EDITOR_MAX_LINES) {
                for (int i = ed_nlines; i > ed_cl; i--) {
                    strncpy(ed_lines[i], ed_lines[i - 1], EDITOR_LINE_CAP);
                }
                ed_lines[ed_cl][0] = 0;
                ed_nlines++;
                ed_cc = 0;
                ed_dirty = 1;
                ed_print_all();
                insert = 1; ed_vi_state("-- INSERT --");
                ed_redraw_line();
            }
        } else if (k == ':') {
            screen_console_puts("\n:");
            char cmd[32];
            int n = ed_vi_readcmd(cmd, (int)sizeof(cmd));
            if (n >= 0) {
                if (ed_vi_excmd(cmd)) return 0;   /* saved/quit cleanly */
                /* not exited: redraw buffer + status */
                ed_vi_print_all();
                ed_vi_state(ed_dirty ? "NORMAL*" : "NORMAL");
                ed_redraw_line();
            } else {
                ed_vi_state("NORMAL");
                ed_redraw_line();
            }
        } else if (k == OC_KEY_ESC) {
            /* already normal: just refresh the status line */
            ed_vi_state("NORMAL");
            ed_redraw_line();
        }
        /* everything else: ignore */
    }
}
