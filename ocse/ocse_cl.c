/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP06b-ocsecl
 * File: ocse/ocse_cl.c
 * Purpose: OCSECL - the OCSE command line. A plain interactive shell
 * for installing the system: prompt, tokenizer, the WP-04-fix1 line
 * editor (arrows/Home/End/Del, Ctrl+A/E/U/K/W/C, history, Tab
 * completion) and a fixed command table (the 28 installer commands
 * plus the WP-06a diagnostics). Line input comes from the keyboard
 * queue (PS/2 and the COM1 serial RX path both feed it); every echo
 * and all command output goes through ocse_out (console + serial).
 */
#include "ocse_cl.h"
#include "ocse_gui.h"
#include "ocse_i18n.h"
#include "ocse_detect.h"
#include "ocse_disk.h"
#include "ocse_part.h"
#include "screen_console.h"
#include "screen_fb.h"
#include "driver_input_keyboard.h"
#include "core_sched.h"
#include "core_power.h"
#include "lib_string.h"
#include <stdint.h>

extern int g_ocse_hotkey_req;

/* Provided by ocse_main.c (serial logging path). */
extern void ocse_serial_puts_raw(const char* s);

#define CL_BUF     256
#define CL_HIST    32
#define CL_MAXARGS 20

/* ==================================================================
 * Line editor (ported from the WP-04-fix1 ush editor: prompt-aware
 * redraw that clears to the previous rendered length, cursor-aware
 * insert/delete, history with draft save). Key codes 0x80..0x89 are
 * produced by the same keyboard driver ush reads.
 * ================================================================== */
static const char* g_le_prompt = "";
static int g_le_prevlen = 0;

static void le_write(const char* s) { ocse_out(s); }

static void le_redraw(const char* line, int llen, int cur) {
    char cr = '\r';
    char sp[2] = { ' ', 0 };
    (void)cur;
    int plen = (int)strlen(g_le_prompt);
    char one[2] = { 0, 0 };
    one[0] = cr; le_write(one);
    le_write(g_le_prompt);
    one[0] = ' ';
    for (int i = 0; i < llen; i++) {
        char c[2] = { line[i], 0 };
        le_write(c);
    }
    for (int i = llen; i < g_le_prevlen; i++)
        le_write(sp);                 /* wipe the old render's tail */
    one[0] = cr; le_write(one);
    le_write(g_le_prompt);
    for (int i = 0; i < cur; i++) {
        char c[2] = { line[i], 0 };
        le_write(c);
    }
    g_le_prevlen = llen;
    (void)plen;
}

static int le_insert(char* line, int llen, int cur, char c, int cap) {
    if (llen + 1 >= cap) return cur;
    if (cur == llen) {
        line[llen] = c;
        char one[2] = { c, 0 };
        le_write(one);
    } else {
        for (int i = llen; i > cur; i--) line[i] = line[i - 1];
        line[cur] = c;
        le_redraw(line, llen + 1, cur + 1);
    }
    line[llen + 1] = 0;
    return cur + 1;
}

static int le_backspace(char* line, int llen, int cur) {
    if (cur <= 0) return cur;
    for (int i = cur - 1; i < llen; i++) line[i] = line[i + 1];
    if (cur == llen) le_write("\b \b");
    else le_redraw(line, llen - 1, cur - 1);
    return cur - 1;
}

static int le_delete(char* line, int llen, int cur) {
    if (cur >= llen) return cur;
    for (int i = cur; i < llen; i++) line[i] = line[i + 1];
    le_redraw(line, llen - 1, cur);
    return cur;
}

/* ---- history ---- */
static char g_hist[CL_HIST][CL_BUF];
static int  g_hist_n = 0;

static void hist_add(const char* line) {
    if (!line[0]) return;
    if (g_hist_n > 0 && strcmp(g_hist[g_hist_n - 1], line) == 0)
        return;                                 /* no adjacent dupes */
    if (g_hist_n == CL_HIST) {
        memmove(g_hist[0], g_hist[1], (u64)(CL_HIST - 1) * CL_BUF);
        g_hist_n--;
    }
    strncpy(g_hist[g_hist_n], line, CL_BUF - 1);
    g_hist[g_hist_n][CL_BUF - 1] = 0;
    g_hist_n++;
}

/* ==================================================================
 * Command table
 * ================================================================== */
typedef int (*cl_fn)(int argc, char** argv);

typedef struct {
    const char* name;
    const char* help;       /* English - system-visible string */
    cl_fn fn;
} cl_cmd_t;

static const cl_cmd_t* cl_find(const char* name);

static void cl_unknown(const char* cmd) {
    ocse_out("ocsecl: '");
    ocse_out(cmd);
    ocse_outln("' - unknown command (try 'help')");
}

/* ---- WP-06a diagnostics kept as-is ---- */
static void print_checks(void) {
    for (int i = 0; i < OCSE_CHECKS; i++) {
        ocse_out("  ");
        ocse_out(ocse_tr(OCSE_T_CHECK_CPU + i));
        ocse_out(": ");
        ocse_out(ocse_tr(g_ocse_checks[i].pass ? OCSE_T_PASS : OCSE_T_FAIL));
        ocse_out("  [");
        ocse_out(g_ocse_checks[i].detail);
        ocse_outln("]");
    }
}

static void print_disks(void) {
    char n[16];
    if (g_ocse_ndisks == 0) {
        ocse_outln(ocse_tr(OCSE_T_NO_DISK));
        return;
    }
    for (int d = 0; d < g_ocse_ndisks; d++) {
        ocse_disk_t* dk = &g_ocse_disks[d];
        ocse_out("  ");
        ocse_out(dk->name);
        ocse_out(dk->table_kind == 2 ? " [GPT] " :
                 dk->table_kind == 1 ? " [MBR] " : " [no table] ");
        u64_to_str(dk->mb, n); ocse_out(n); ocse_out(" MiB, ");
        u64_to_str((u64)dk->nparts, n); ocse_out(n); ocse_out(" part(s):");
        ocse_outln("");
        for (int p = 0; p < dk->nparts; p++) {
            ocse_part_t* pt = &dk->parts[p];
            ocse_out("    #");
            u64_to_str((u64)p, n); ocse_out(n);
            ocse_out(" ");
            ocse_out(ocse_fs_name(pt->fs));
            ocse_out(" lba=");
            u64_to_str(pt->start_lba, n); ocse_out(n);
            ocse_out(" size=");
            u64_to_str(pt->mb, n); ocse_out(n); ocse_out("MiB");
            ocse_outln("");
        }
        if (dk->space_low) {
            ocse_out("    (!) ");
            ocse_outln(ocse_tr(OCSE_T_SPACE_LOW));
        }
    }
}

static int cmd_hwcheck(int argc, char** argv) {
    (void)argc; (void)argv;
    ocse_detect_run();
    print_checks();
    return 0;
}
static int cmd_disks(int argc, char** argv) {
    (void)argc; (void)argv;
    ocse_disk_scan();
    print_disks();
    return 0;
}
static int cmd_lang(int argc, char** argv) {
    if (argc == 1) {
        ocse_outln(ocse_tr(OCSE_T_LANG_NOW));
        return 0;
    }
    if (argc == 2 && (strcmp(argv[1], "en") == 0 ||
                      strcmp(argv[1], "zh") == 0)) {
        ocse_i18n_set(argv[1][0] == 'e' ? OCSE_LANG_EN : OCSE_LANG_ZH);
        ocse_outln(ocse_tr(OCSE_T_LANG_NOW));
        return 0;
    }
    ocse_outln("usage: lang [en|zh]");
    return 1;
}
static int cmd_clear(int argc, char** argv) {
    (void)argc; (void)argv;
    screen_console_reset(screen_fb_rgb(0xE0, 0xE0, 0xE0),
                         screen_fb_rgb(0x10, 0x10, 0x14));
    return 0;
}
static int cmd_reboot(int argc, char** argv) {
    (void)argc; (void)argv;
    ocse_outln(ocse_tr(OCSE_T_REBOOTING));
    ocse_serial_puts_raw("[ocsecl] reboot requested\r\n");
    core_power_reboot();                        /* flushes blk, never returns */
    return 0;
}
static int cmd_shutdown(int argc, char** argv) {
    (void)argc; (void)argv;
    ocse_outln(ocse_tr(OCSE_T_POWEROFF));
    ocse_serial_puts_raw("[ocsecl] shutdown requested\r\n");
    if (core_power_shutdown() != 0) {
        ocse_outln(ocse_tr(OCSE_T_POWEROFF_NOACPI));
        core_power_halt();
    }
    return 0;
}

/* ---- WP-06b partition commands ---- */
static int cmd_lsdisk(int argc, char** argv) {
    (void)argc; (void)argv;
    return ocse_part_lsdisk();
}
static int cmd_lspart(int argc, char** argv) {
    (void)argc;
    if (!argv[1]) { ocse_outln("usage: lspart <disk>"); return 1; }
    return ocse_part_lspart(argv[1]);
}
static int cmd_parted(int argc, char** argv) {
    (void)argc;
    if (!argv[1]) { ocse_outln("usage: parted <disk>"); return 1; }
    return ocse_part_parted(argv[1]);
}
static int cmd_mkpart(int argc, char** argv) {
    (void)argc;
    if (!argv[1] || !argv[2] || !argv[3]) {
        ocse_outln("usage: mkpart <disk> <startMiB> <endMiB>");
        return 1;
    }
    u64 s = 0, e = 0;
    int i = 0;
    while (argv[2][i] >= '0' && argv[2][i] <= '9' && i < 12) {
        s = s * 10 + (u64)(argv[2][i] - '0'); i++;
    }
    if (argv[2][i] || i == 0) { ocse_outln("mkpart: bad start"); return 1; }
    i = 0;
    while (argv[3][i] >= '0' && argv[3][i] <= '9' && i < 12) {
        e = e * 10 + (u64)(argv[3][i] - '0'); i++;
    }
    if (argv[3][i] || i == 0) { ocse_outln("mkpart: bad end"); return 1; }
    return ocse_part_mkpart(argv[1], s, e);
}
static int cmd_rmpart(int argc, char** argv) {
    (void)argc;
    if (!argv[1] || !argv[2]) {
        ocse_outln("usage: rmpart <disk> <n>");
        return 1;
    }
    int n = 0, i = 0;
    while (argv[2][i] >= '0' && argv[2][i] <= '9' && i < 4) {
        n = n * 10 + (argv[2][i] - '0'); i++;
    }
    if (argv[2][i] || i == 0 || n < 1) {
        ocse_outln("rmpart: bad partition number");
        return 1;
    }
    return ocse_part_rmpart(argv[1], n);
}
static int cmd_setflag(int argc, char** argv) {
    (void)argc;
    if (!argv[1] || !argv[2] || !argv[3]) {
        ocse_outln("usage: setflag <disk> <n> boot [on|off]");
        return 1;
    }
    int n = 0, i = 0;
    while (argv[2][i] >= '0' && argv[2][i] <= '9' && i < 4) {
        n = n * 10 + (argv[2][i] - '0'); i++;
    }
    if (argv[2][i] || i == 0 || n < 1) {
        ocse_outln("setflag: bad partition number");
        return 1;
    }
    int on = 1;
    if (argc >= 5) {
        if (strcmp(argv[4], "on") == 0 || strcmp(argv[4], "1") == 0)
            on = 1;
        else if (strcmp(argv[4], "off") == 0 || strcmp(argv[4], "0") == 0)
            on = 0;
        else {
            ocse_outln("setflag: value must be on|off");
            return 1;
        }
    }
    return ocse_part_setflag(argv[1], n, argv[3], on);
}

/* ---- help ---- */
static int cmd_help(int argc, char** argv);

/* ---- the table (order = help listing order) ----
 * halt/gui have dedicated handlers; both switch the world (halt
 * never returns, gui leaves the loop), so they are real entries
 * with their own fn slots. */
static int cmd_halt(int argc, char** argv) {
    (void)argc; (void)argv;
    ocse_outln(ocse_tr(OCSE_T_HALT));
    core_power_halt();                        /* never returns */
    return 0;
}
static int cmd_gui(int argc, char** argv) {
    (void)argc; (void)argv;
    return 0x100;                               /* "go to GUI" */
}

static const cl_cmd_t g_cmds[] = {
    { "help",     "show all commands or one command's help", cmd_help },
    { "lsdisk",   "list install-target disks",              cmd_lsdisk },
    { "lspart",   "list partitions of <disk>",              cmd_lspart },
    { "parted",   "show the raw partition table of <disk>", cmd_parted },
    { "mkpart",   "create a partition: mkpart <disk> <startMiB> <endMiB>", cmd_mkpart },
    { "rmpart",   "delete partition #n: rmpart <disk> <n>",  cmd_rmpart },
    { "setflag",  "set a flag: setflag <disk> <n> boot [on|off]", cmd_setflag },
    { "reboot",   "flush disks and reboot the machine",     cmd_reboot },
    { "shutdown", "flush disks and power off",              cmd_shutdown },
    { "hwcheck",  "re-run the four hardware checks",         cmd_hwcheck },
    { "disks",    "raw disk inventory (WP-06a view)",        cmd_disks },
    { "lang",     "show or set the language: lang [en|zh]", cmd_lang },
    { "clear",    "clear the screen",                        cmd_clear },
    { "gui",      "switch back to the graphical panel",      cmd_gui },
    { "halt",     "stop the CPU (no ACPI path)",             cmd_halt },
    { NULL, NULL, NULL }
};

static const cl_cmd_t* cl_find(const char* name) {
    for (const cl_cmd_t* c = &g_cmds[0]; c->name; c++)
        if (strcmp(c->name, name) == 0) return c;
    return NULL;
}

static int cmd_help(int argc, char** argv) {
    if (argc >= 2) {
        const cl_cmd_t* c = cl_find(argv[1]);
        if (!c) { cl_unknown(argv[1]); return 1; }
        ocse_out(c->name);
        ocse_out(" - ");
        ocse_outln(c->help);
        return 0;
    }
    ocse_outln(ocse_tr(OCSE_T_CLI_BANNER));
    for (const cl_cmd_t* c = &g_cmds[0]; c->name; c++) {
        ocse_out("  ");
        ocse_out(c->name);
        int pad = 14 - (int)strlen(c->name);
        while (pad-- > 0) ocse_out(" ");
        ocse_out(" ");
        ocse_outln(c->help);
    }
    return 0;
}

/* ==================================================================
 * Tokenizer + dispatch
 * ================================================================== */
static int split_args(char* line, char** argv, int max) {
    int argc = 0;
    char* p = line;
    while (*p && argc < max) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) *p++ = 0;
    }
    return argc;
}

/* Returns 0 normally; 0x100 = switch to GUI. */
static int run_line(char* line) {
    char* argv[CL_MAXARGS];
    int argc = split_args(line, argv, CL_MAXARGS);
    if (argc == 0) return 0;
    const cl_cmd_t* c = cl_find(argv[0]);
    if (!c) { cl_unknown(argv[0]); return 1; }
    return c->fn(argc, argv);
}

/* ==================================================================
 * Tab completion: commands (first word) or cwd entries (later words)
 * ================================================================== */
static void complete_word(char* line, int* llen, int* cur) {
    int wstart = *cur;
    while (wstart > 0 && line[wstart - 1] != ' ') wstart--;
    char* word = line + wstart;
    int wlen = *cur - wstart;
    if (wlen <= 0) return;
    int found = 0;
    char match[CL_BUF];
    match[0] = 0;
    if (wstart == 0) {
        /* command names */
        for (const cl_cmd_t* c = &g_cmds[0]; c->name; c++) {
            if (strncmp(c->name, word, (u64)wlen) == 0) {
                if (found == 0) { strncpy(match, c->name, CL_BUF - 1); }
                else {
                    int j = 0;
                    while (j < (int)strlen(match) && match[j] == c->name[j]) j++;
                    match[j] = 0;
                }
                found++;
            }
        }
    }
    /* file-name completion arrives with the file command batch. */
    if (found == 1 || (found > 1 && match[0])) {
        int ml = (int)strlen(match);
        if (ml > wlen) {
            for (int i = wlen; i < ml && *llen < CL_BUF - 2; i++) {
                line[(*llen)++] = match[i];
                char one[2] = { match[i], 0 };
                le_write(one);
            }
            *cur = *llen;
            if (found == 1 && *llen < CL_BUF - 2) {
                line[(*llen)++] = ' ';
                le_write(" ");
                *cur = *llen;
            }
        } else if (found > 1) {
            le_write("\n");
            for (const cl_cmd_t* c = &g_cmds[0]; c->name; c++)
                if (strncmp(c->name, word, (u64)wlen) == 0) {
                    ocse_out("  ");
                    ocse_outln(c->name);
                }
            le_write(g_le_prompt);
            le_write(line);
            *cur = *llen;
        }
    }
    line[*llen] = 0;
}

/* ==================================================================
 * The loop
 * ================================================================== */
static int read_line(char* line) {
    /* prompt-aware editor loop; returns length */
    int llen = 0, cur = 0;
    int hist_nav = -1;
    char draft[CL_BUF];
    line[0] = 0;
    for (;;) {
        if (g_ocse_hotkey_req)
            return -1;                          /* caller switches */
        if (!driver_input_keyboard_has_key()) {
            core_sched_yield();
            continue;
        }
        int k = driver_input_keyboard_getch();
        if (k == '\r' || k == '\n') {
            le_write("\n");
            return llen;
        }
        if (k == 0x03) {                        /* Ctrl+C */
            le_write("^C\n");
            return -2;
        }
        if (k == 0x80) {                        /* Up */
            if (g_hist_n > 0) {
                if (hist_nav == -1) {
                    strncpy(draft, line, CL_BUF - 1);
                    hist_nav = g_hist_n - 1;
                } else if (hist_nav > 0) hist_nav--;
                else continue;
                strncpy(line, g_hist[hist_nav], CL_BUF - 1);
                line[CL_BUF - 1] = 0;
                llen = cur = (int)strlen(line);
                le_redraw(line, llen, cur);
            }
            continue;
        }
        if (k == 0x81) {                        /* Down */
            if (hist_nav != -1) {
                if (hist_nav < g_hist_n - 1) {
                    hist_nav++;
                    strncpy(line, g_hist[hist_nav], CL_BUF - 1);
                } else {
                    hist_nav = -1;
                    strncpy(line, draft, CL_BUF - 1);
                }
                line[CL_BUF - 1] = 0;
                llen = cur = (int)strlen(line);
                le_redraw(line, llen, cur);
            }
            continue;
        }
        if (k == 0x82) { if (cur > 0) { cur--; le_redraw(line, llen, cur); } continue; }
        if (k == 0x83) { if (cur < llen) { cur++; le_redraw(line, llen, cur); } continue; }
        if (k == 0x84 || k == 0x01) { cur = 0; le_redraw(line, llen, cur); continue; }
        if (k == 0x85 || k == 0x05) { cur = llen; le_redraw(line, llen, cur); continue; }
        if (k == 0x89) { cur = le_delete(line, llen, cur); llen = (int)strlen(line); continue; }
        if (k == 0x15) {                        /* Ctrl+U */
            if (cur > 0) {
                int rest = llen - cur;
                for (int i = 0; i < rest; i++) line[i] = line[cur + i];
                llen = rest; line[llen] = 0; cur = 0;
                le_redraw(line, llen, cur);
            }
            continue;
        }
        if (k == 0x0B) {                        /* Ctrl+K */
            if (cur < llen) {
                llen = cur; line[llen] = 0;
                le_redraw(line, llen, cur);
            }
            continue;
        }
        if (k == 0x17) {                        /* Ctrl+W */
            if (cur > 0) {
                int p2 = cur;
                while (p2 > 0 && line[p2 - 1] == ' ') p2--;
                while (p2 > 0 && line[p2 - 1] != ' ') p2--;
                int removed = cur - p2;
                int rest = llen - cur;
                for (int i = 0; i < rest; i++) line[p2 + i] = line[cur + i];
                llen -= removed; line[llen] = 0; cur = p2;
                le_redraw(line, llen, cur);
            }
            continue;
        }
        if (k == 0x08 || k == 0x7F) {           /* Backspace */
            cur = le_backspace(line, llen, cur);
            llen = (int)strlen(line);
            continue;
        }
        if (k == '\t') {                        /* Tab */
            if (cur == llen) complete_word(line, &llen, &cur);
            continue;
        }
        if (k >= 0x20 && k < 0x7F) {
            cur = le_insert(line, llen, cur, (char)k, CL_BUF);
            llen = (int)strlen(line);
        }
    }
}

int ocse_cl_loop(void) {
    char line[CL_BUF];
    ocse_out("ocsecl> ");
    for (;;) {
        g_le_prompt = "ocsecl> ";
        g_le_prevlen = 0;
        int rc = read_line(line);
        if (rc == -1)
            return OCSE_ACT_GOTO_GUI;           /* hotkey F1 */
        if (rc >= 0 && line[0]) {
            hist_add(line);
            int act = run_line(line);
            if (act == 0x100)
                return OCSE_ACT_GOTO_GUI;
        }
        ocse_out("ocsecl> ");
    }
}
