/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-10-tui
 * File: tui/tui.h
 * Purpose: Public interface of the TUI framework (ncurses-style) and of
 *          the three TUI applications shipped with WP-10-tui (hardware
 *          configuration, wireless network, system information).
 *
 * Rendering model:
 *   A tui_window_t owns a w*h cell buffer ({char, attr}).  Windows are
 *   painted bottom-up through the panel stack onto the terminal
 *   backend.  Two backends are real code paths:
 *     - tui backend "fb":     the built-in framebuffer console
 *                             (drivers/display/screen_console.c),
 *     - tui backend "serial": an ANSI/VT100 escape stream on COM1.
 *   tui_term_detect() picks one (config key tui.backend = fb|serial,
 *   default fb).  Colors: the 16-entry VGA palette is the base truth;
 *   256-color and truecolor requests are reduced to the nearest
 *   palette entry on the fb backend and sent as real ANSI SGR on the
 *   serial backend.
 *
 * Input model:
 *   tui_getch()/tui_getch_timeout() read the keyboard queue (PS/2, USB
 *   HID and the COM1 bridge all feed it) and map OC_KEY_* special keys
 *   to TUI KEY_* codes.  A real ANSI CSI parser decodes xterm-style
 *   arrow/functional keys and SGR mouse reports arriving over serial.
 *   Touchpad events (I2C-HID) surface through the mouse API when the
 *   device exists; on hardware without one the mouse API reports
 *   "no mouse" honestly.
 */
#ifndef OC_TUI_H
#define OC_TUI_H

#include "../kernel/types.h"

/* ------------------------------------------------------------------ */
/* Terminal detection                                                  */
/* ------------------------------------------------------------------ */

typedef enum {
    TUI_TERM_OPCUBE_FB = 0,   /* built-in framebuffer console          */
    TUI_TERM_ANSI_SERIAL = 1, /* ANSI escape terminal on COM1          */
    TUI_TERM_VT100 = 2,       /* VT100-class serial terminal           */
    TUI_TERM_VT220 = 3,       /* VT220-class serial terminal           */
} tui_term_type_t;

typedef struct {
    tui_term_type_t type;
    const char     *name;     /* human-readable terminal name          */
    int             colors;   /* 16 (fb) / 8..256 (serial reports)     */
    int             truecolor;/* 1 when the backend can do 24-bit RGB  */
    int             has_mouse;/* serial mouse protocol / touchpad      */
    int             cols;
    int             rows;
} tui_term_info_t;

/* ------------------------------------------------------------------ */
/* Attributes and colors                                               */
/* ------------------------------------------------------------------ */

/* attr layout: fg[3:0] | bg[7:4] | flags[15:8] */
#define TUI_ATTR_FG_SHIFT   0
#define TUI_ATTR_BG_SHIFT   4
#define TUI_ATTR_BOLD       (1u << 8)
#define TUI_ATTR_UNDERLINE  (1u << 9)
#define TUI_ATTR_REVERSE    (1u << 10)
#define TUI_ATTR_BLINK      (1u << 11)
#define TUI_ATTR_ITALIC     (1u << 12)
#define TUI_ATTR_INVIS      (1u << 13)

#define TUI_ATTR_FG(a)   (((a) >> TUI_ATTR_FG_SHIFT) & 0xF)
#define TUI_ATTR_BG(a)   (((a) >> TUI_ATTR_BG_SHIFT) & 0xF)
#define TUI_ATTR_FLAGS(a) ((a) & 0xFF00u)
#define TUI_MAKE_ATTR(fg, bg) ((tui_attr_t)((((fg) & 0xF) << TUI_ATTR_FG_SHIFT) | \
                                            (((bg) & 0xF) << TUI_ATTR_BG_SHIFT)))

/* WP-10-chinese_support: a CJK codepoint occupies two cells; the cell
 * RIGHT of a wide glyph carries this marker so cell consumers know to
 * skip it (the glyph bitmap spans both columns). The value is chosen
 * outside every valid Unicode range. */
#define TUI_WIDE_CONT 0xFFFFFFFFu

/* 16-color VGA palette indexes (standard CGA order). */
enum {
    TUI_COLOR_BLACK = 0, TUI_COLOR_MAROON, TUI_COLOR_GREEN, TUI_COLOR_OLIVE,
    TUI_COLOR_NAVY, TUI_COLOR_PURPLE, TUI_COLOR_TEAL, TUI_COLOR_SILVER,
    TUI_COLOR_GRAY, TUI_COLOR_RED, TUI_COLOR_LIME, TUI_COLOR_YELLOW,
    TUI_COLOR_BLUE, TUI_COLOR_FUCHSIA, TUI_COLOR_AQUA, TUI_COLOR_WHITE,
    TUI_COLOR_COUNT /* 16 */
};

/* Special key codes (shared by tui_getch and the menu/form drivers). */
enum {
    TUI_KEY_NONE    = 0x0000,
    TUI_KEY_CTRL_A  = 0x0001, TUI_KEY_CTRL_B  = 0x0002, TUI_KEY_CTRL_C = 0x0003,
    TUI_KEY_CTRL_D  = 0x0004, TUI_KEY_CTRL_E  = 0x0005, TUI_KEY_CTRL_F = 0x0006,
    TUI_KEY_CTRL_G  = 0x0007, TUI_KEY_CTRL_H  = 0x0008, TUI_KEY_TAB    = 0x0009,
    TUI_KEY_ENTER   = 0x000A, TUI_KEY_CTRL_K  = 0x000B, TUI_KEY_CTRL_L = 0x000C,
    TUI_KEY_CTRL_N  = 0x000E, TUI_KEY_CTRL_O  = 0x000F, TUI_KEY_CTRL_P = 0x0010,
    TUI_KEY_CTRL_R  = 0x0012, TUI_KEY_CTRL_S  = 0x0013, TUI_KEY_CTRL_T = 0x0014,
    TUI_KEY_CTRL_U  = 0x0015, TUI_KEY_CTRL_W  = 0x0017, TUI_KEY_CTRL_X = 0x0018,
    TUI_KEY_CTRL_Y  = 0x0019, TUI_KEY_CTRL_Z  = 0x001A,
    TUI_KEY_ESC     = 0x001B,
    TUI_KEY_BACKSPACE = 0x0100,
    TUI_KEY_UP      = 0x0101, TUI_KEY_DOWN    = 0x0102,
    TUI_KEY_LEFT    = 0x0103, TUI_KEY_RIGHT   = 0x0104,
    TUI_KEY_HOME    = 0x0105, TUI_KEY_END     = 0x0106,
    TUI_KEY_PGUP    = 0x0107, TUI_KEY_PGDN    = 0x0108,
    TUI_KEY_INSERT  = 0x0109, TUI_KEY_DELETE  = 0x010A,
    TUI_KEY_F1      = 0x0110,
    TUI_KEY_F2      = 0x0111, TUI_KEY_F3  = 0x0112, TUI_KEY_F4  = 0x0113,
    TUI_KEY_F5      = 0x0114, TUI_KEY_F6  = 0x0115, TUI_KEY_F7  = 0x0116,
    TUI_KEY_F8      = 0x0117, TUI_KEY_F9  = 0x0118, TUI_KEY_F10 = 0x0119,
    TUI_KEY_F11     = 0x011A, TUI_KEY_F12 = 0x011B,
    TUI_KEY_MOUSE   = 0x0120, /* a mouse event is queued; call tui_mouse_get */
    TUI_KEY_RESIZE  = 0x0121,
};
#define TUI_KEY_F(n) (0x010F + (n))   /* F1..F12 -> 0x0110..0x011B */

/* Mouse events (xterm SGR / touchpad mapped onto one shape). */
enum {
    TUI_BUTTON_RELEASED = 0, TUI_BUTTON_PRESSED = 1, TUI_BUTTON_CLICKED = 2,
    TUI_BUTTON_WHEEL_UP = 3, TUI_BUTTON_WHEEL_DOWN = 4,
};
typedef struct {
    int button;     /* 0..2 primary/secondary/tertiary, 3 wheel       */
    int action;     /* TUI_BUTTON_*                                    */
    int x, y;       /* cell coordinates                                */
    int modifiers;  /* 1=shift 2=alt 4=ctrl                            */
} tui_mouse_event_t;

/* ------------------------------------------------------------------ */
/* Windows                                                             */
/* ------------------------------------------------------------------ */

/* packed per-cell attribute: fg[3:0] | bg[7:4] | SGR flags in bits 8-15 */
typedef u32 tui_attr_t;

typedef struct { u32 ch; tui_attr_t attr; } tui_cell_t;

typedef struct tui_window {
    int x, y, w, h;             /* position and size in cells         */
    int curs_x, curs_y;         /* cursor inside the window           */
    tui_attr_t attr;            /* current write attribute            */
    tui_attr_t bkgd;            /* background fill {ch, attr}         */
    int bordered;               /* draw a border on refresh           */
    int scroll_enabled;         /* scrollok                           */
    int top_margin;             /* first row that scrolls             */
    int visible;
    char title[40];
    tui_cell_t *cells;          /* w * h                              */
    struct tui_window *parent;  /* subwin/derwin owner                */
    int id;
} tui_window_t;

/* Border characters (single / double / ASCII; custom = pass your own). */
typedef struct {
    u32 ls, rs, ts, bs, tl, tr, bl, br;
} tui_border_t;
extern const tui_border_t TUI_BORDER_SINGLE;
extern const tui_border_t TUI_BORDER_DOUBLE;
extern const tui_border_t TUI_BORDER_ASCII;

/* ------------------------------------------------------------------ */
/* Menu                                                                */
/* ------------------------------------------------------------------ */

typedef struct tui_menu_item {
    const char *name;
    const char *desc;
    void       *userptr;
} tui_menu_item_t;

typedef struct tui_menu {
    tui_window_t        *win;
    tui_menu_item_t     *items;
    int                  count;
    int                  current;
    int                  top_row;    /* scroll window                     */
    tui_attr_t           fore;       /* normal item attr                  */
    tui_attr_t           back;
} tui_menu_t;

/* ------------------------------------------------------------------ */
/* Form                                                                */
/* ------------------------------------------------------------------ */

#define TUI_FIELD_MAX 64

typedef struct tui_field {
    int  y, x;                  /* position inside the form window    */
    int  width;                 /* editable width                     */
    char buffer[TUI_FIELD_MAX + 1];
    int  len;                   /* used length                        */
    int  cursor;                /* edit cursor inside buffer          */
    int  editable;
    const char *label;
} tui_field_t;

typedef struct tui_form {
    tui_window_t *win;
    tui_field_t  *fields;
    int           count;
    int           current;
} tui_form_t;

/* ------------------------------------------------------------------ */
/* TUI framework API (interface set 7.1)                               */
/* ------------------------------------------------------------------ */

int  tui_init(void);                       /* 0 on success            */
int  tui_shutdown(void);
int  tui_term_detect(tui_term_info_t *out);
void tui_term_size(int *cols, int *rows);

tui_window_t *tui_window_new(int y, int x, int h, int w);
void tui_window_free(tui_window_t *win);
int  tui_window_move(tui_window_t *win, int y, int x);
int  tui_window_resize(tui_window_t *win, int h, int w);
int  tui_window_refresh(tui_window_t *win);
int  tui_window_clear(tui_window_t *win);
int  tui_window_border(tui_window_t *win, const tui_border_t *b);
int  tui_window_title(tui_window_t *win, const char *title);
int  tui_window_scrollok(tui_window_t *win, int on);
int  tui_window_scroll(tui_window_t *win, int lines);
tui_window_t *tui_window_subwin(tui_window_t *parent, int y, int x, int h, int w);
int  tui_window_touch(tui_window_t *win);  /* mark dirty for redraw   */

int  tui_print(const char *s);             /* at the window cursor    */
int  tui_print_at(int y, int x, const char *s);
int  tui_printw(const char *fmt, ...);     /* printf-style, at cursor */
int  tui_addch(u32 ch);
int  tui_set_attr(tui_attr_t attr);
int  tui_attron(tui_attr_t attr);
int  tui_attroff(tui_attr_t attr);
int  tui_set_color(int fg, int bg);
int  tui_move(int y, int x);
void tui_getyx(int *y, int *x);
void tui_getbegyx(tui_window_t *win, int *y, int *x);
void tui_getmaxyx(tui_window_t *win, int *y, int *x);
int  tui_curs_set(int visibility);         /* 0 hidden 1 normal       */

int  tui_getch(void);                      /* blocking                */
int  tui_getch_timeout(int ms);            /* -1 on timeout           */
void tui_nodelay(int on);                  /* non-blocking mode       */
int  tui_mouse_enable(int on);
int  tui_mouse_get(tui_mouse_event_t *out);
/* fix2: tui_core emits the SGR enable/disable on the serial backend */
void tui_core_serial_mouse(int on);

int  tui_color_supported(void);            /* nonzero when colors ok  */
int  tui_color_pairs_max(void);
int  tui_init_pair(int pair, int fg, int bg);
int  tui_pair_attr(int pair);              /* COLOR_PAIR equivalent   */
int  tui_color_set_256(int fg256, int bg256);  /* nearest palette map */
int  tui_color_set_rgb(int fg256, int bg256);  /* truecolor-aware map */

/* Panel system (7.1, panels) */
typedef struct tui_panel tui_panel_t;
tui_panel_t *tui_panel_new(tui_window_t *win);
void tui_panel_free(tui_panel_t *panel);
int  tui_panel_top(tui_panel_t *panel);
int  tui_panel_bottom(tui_panel_t *panel);
int  tui_panel_show(tui_panel_t *panel);
int  tui_panel_hide(tui_panel_t *panel);
int  tui_panel_move(tui_panel_t *panel, int y, int x);
int  tui_panel_hidden(tui_panel_t *panel);
tui_window_t *tui_panel_window(tui_panel_t *panel);
/* fix2: drop every panel (their windows die with the session reset) */
void tui_panels_reset(void);
void tui_update_panels(void);              /* repaint whole stack     */

/* Menu system (7.1, menus) */
tui_menu_t *tui_menu_new(tui_window_t *win, tui_menu_item_t *items, int count);
void tui_menu_free(tui_menu_t *menu);
int  tui_menu_post(tui_menu_t *menu);      /* draw + first paint      */
int  tui_menu_unpost(tui_menu_t *menu);
int  tui_menu_driver(tui_menu_t *menu, int key);  /* nav; returns:
                                   0 keep going, 1 item chosen,
                                   -1 user asked to leave (Esc)      */
int  tui_menu_current(tui_menu_t *menu);
int  tui_menu_set_current(tui_menu_t *menu, int idx);
void tui_menu_set_colors(tui_menu_t *menu, tui_attr_t fore, tui_attr_t back);

/* Form system (7.1, forms) */
tui_form_t *tui_form_new(tui_window_t *win, tui_field_t *fields, int count);
void tui_form_free(tui_form_t *form);
int  tui_form_post(tui_form_t *form);
int  tui_form_unpost(tui_form_t *form);
int  tui_form_driver(tui_form_t *form, int key);  /* 0 nav/edit ok,
                                   1 form submitted (Enter on last),
                                   -1 user asked to leave (Esc)      */
const char *tui_form_field_buffer(tui_form_t *form, int idx);
int  tui_form_set_field(tui_form_t *form, int idx, const char *value);

/* System status line shared by all three applications. */
void tui_status_line(const char *fmt, ...);

/* ------------------------------------------------------------------ */
/* Hardware configuration application (interface set 7.2)              */
/* ------------------------------------------------------------------ */

int  tui_hw_init(void);                    /* 0 ok, <0 prerequisites  */
int  tui_hw_cpu(void);
int  tui_hw_mem(void);
int  tui_hw_storage(void);
int  tui_hw_network(void);
int  tui_hw_sound(void);
int  tui_hw_usb(void);
int  tui_hw_pci(void);
int  tui_hw_input(void);
int  tui_hw_camera(void);
int  tui_hw_wireless(void);
int  tui_hw_timer(void);
int  tui_hw_save(void);                    /* persist to /etc/opencube.conf */
int  tui_hw_load(void);

/* ------------------------------------------------------------------ */
/* Wireless network application (interface set 7.3)                    */
/* ------------------------------------------------------------------ */

int  tui_wifi_init(void);
int  tui_wifi_scan(void);                  /* sync scan; results kept */
int  tui_wifi_list(void);                  /* TUI list of last scan   */
int  tui_wifi_connect(const char *ssid, const char *password);
int  tui_wifi_disconnect(void);
int  tui_wifi_status(void);                /* status TUI panel        */
int  tui_bt_init(void);
int  tui_bt_scan(void);
int  tui_bt_pair(int dev_index);
int  tui_bt_unpair(int dev_index);
int  tui_bt_services(int dev_index);

/* ------------------------------------------------------------------ */
/* System information application (interface set 7.4)                  */
/* ------------------------------------------------------------------ */

int  tui_sysinfo_init(void);
int  tui_sysinfo_overview(void);
int  tui_sysinfo_processes(void);
int  tui_sysinfo_logs(void);
int  tui_sysinfo_monitor(void);
int  tui_sysinfo_power(void);
int  tui_sysinfo_update(void);
int  tui_sysinfo_time(void);
int  tui_sysinfo_help(void);

/* Main entry points registered as shell commands. */
void tui_shell_cmds_register(void);        /* tui, tui-test, tui-hw,
                                              tui-net, tui-sysinfo    */

/* ---- cross-module (internal) surface shared by the tui sources ---- */
int  tui_started(void);                    /* nonzero after tui_init  */
tui_attr_t tui_default_attr(void);
u32  tui_palette_pixel(int idx);
void tui_backend_putc_at(int x, int y, u32 ch, tui_attr_t a);
void tui_backend_flush(void);
void tui_backend_cursor(int visible);
int  tui_cursor_visible(void);

tui_window_t *tui_window_get(int id);
int  tui_window_count(void);
void tui_window_compact(void);             /* compact after free      */
void tui_windows_reset(void);              /* free every window (init/shutdown) */
void tui_text_reset(void);                 /* drop cached window pointers (focus/status) */
void tui_input_reset(void);                /* reset the CSI decoder + mouse state (init/shutdown) */
tui_window_t *tui_window_focus(void);
tui_window_t *tui_window_focus_set(tui_window_t *win);

int  tui_hw_main(void);                    /* menu-driven HW app      */
int  tui_hw_run_page(const char *page);    /* direct page (tui-hw X)  */
int  tui_hw_apply_sound_volume(u32 vol);
int  tui_net_main(void);                   /* WiFi + Bluetooth app    */
int  tui_sysinfo_main(void);               /* system information app  */
int  tui_input_map_key(int c);             /* OC_KEY_* -> TUI KEY_*   */
int  tui_input_feed_raw(int c);            /* CSI decoder test hook   */
int  tui_mouse_poll_touchpad(void);

int  tui_core_apply_theme(int fg, int bg); /* fix4 theme hook (7.5)   */

#include "tui_ext.h"                       /* fix4: L1 extension sets */

#endif /* OC_TUI_H */
