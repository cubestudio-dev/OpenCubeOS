/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10d
 * File: kernel/driver_usb_hid.c
 * Purpose: USB HID class driver (boot protocol) - keyboards and mice.
 *
 * Keyboards and mice are the mainstream USB input path; every real
 * keyboard/mouse and QEMU's usb-kbd/usb-mouse/usb-tablet speak the
 * HID boot protocol.  The driver:
 *   - probes HID boot interfaces (class 3, subclass 1, protocol 1 =
 *     keyboard / protocol 2 = mouse)
 *   - switches them to the boot protocol (SET_PROTOCOL class request)
 *     so reports have the fixed layout (kbd: modifier + reserved +
 *     6 key slots; mouse: buttons + dx + dy [+ wheel])
 *   - polls the interrupt-IN endpoint from the USB poll thread
 *   - translates keypresses to the OS keycode set and injects them
 *     through driver_input_keyboard_inject() (exactly what the PS/2 driver
 *     feeds in), so a USB keyboard is immediately usable in the
 *     oc> shell without any configuration
 *   - queues mouse events in a small ring for consumers
 *
 * QEMU devices: usb-kbd (0627:0001), usb-mouse (0627:0001), both
 * full-speed HID boot devices with an 8-byte interrupt-IN report.
 */
#include "driver_usb_hid.h"
#include "driver_usb.h"
#include "driver_input_keyboard.h"
#include "screen_console.h"
#include "lib_string.h"
#include "core_timer.h"
#include "core_sched.h"

#define HID_GET_REPORT   0x01
#define HID_GET_IDLE     0x02
#define HID_SET_IDLE     0x0A
#define HID_SET_PROTOCOL 0x0B
#define HID_GET_PROTOCOL 0x03

#define HID_MOD_LCTRL  0x01
#define HID_MOD_LSHIFT 0x02
#define HID_MOD_LALT   0x04
#define HID_MOD_RCTRL  0x10
#define HID_MOD_RSHIFT 0x20
#define HID_MOD_RALT   0x40

#define MOUSE_RING 64

typedef struct driver_usb_hid_kbd {
    driver_usb_dev_t *dev;
    driver_usb_endpoint_t *ep;
    u8  prev[8];
    int prev_valid;
    int up;
    u64 keys_injected;
} driver_usb_hid_kbd_t;

typedef struct driver_usb_hid_mouse {
    driver_usb_dev_t *dev;
    driver_usb_endpoint_t *ep;
    int up;
    u64 events;
    u64 acc_x, acc_y;
    u8  buttons;
    driver_usb_mouse_event_t ring[MOUSE_RING];
    volatile int rhead, rtail;
} driver_usb_hid_mouse_t;

#define HID_MAX_KBD  4
#define HID_MAX_MOUSE 4
static driver_usb_hid_kbd_t   g_kbd[HID_MAX_KBD];
static driver_usb_hid_mouse_t g_mouse[HID_MAX_MOUSE];

/* ------------------------------------------------------------------
 * keyboard translation
 * ------------------------------------------------------------------ */

/* HID usage ID -> keycode (or 0xFFFF = not printable) */
static u16 hid_kbd_translate(u8 usage, u8 mods) {
    u8 shift = (mods & (HID_MOD_LSHIFT | HID_MOD_RSHIFT)) ? 1 : 0;
    u8 ctrl  = (mods & (HID_MOD_LCTRL | HID_MOD_RCTRL)) ? 1 : 0;

    /* letters */
    if (usage >= 0x04 && usage <= 0x1d) {
        char c = (char)('a' + (usage - 0x04));
        if (shift) c = (char)(c - 'a' + 'A');
        if (ctrl)  return (u16)(c & 0x1f);
        return (u16)c;
    }
    /* digits row (shifted = symbols) */
    static const char digs[10] = { '1','2','3','4','5','6','7','8','9','0' };
    static const char dshf[10] = { '!','@','#','$','%','^','&','*','(',')' };
    if (usage >= 0x1e && usage <= 0x27) {
        int i = usage - 0x1e;
        char c = shift ? dshf[i] : digs[i];
        return (u16)c;
    }
    switch (usage) {
        case 0x28: return OC_KEY_ENTER;      /* Enter */
        case 0x29: return OC_KEY_ESC;        /* Escape */
        case 0x2a: return OC_KEY_BACKSPACE;  /* Backspace */
        case 0x2b: return OC_KEY_TAB;        /* Tab */
        case 0x2c: return ' ';
        case 0x2d: return shift ? '_' : '-';
        case 0x2e: return shift ? '+' : '=';
        case 0x2f: return shift ? '{' : '[';
        case 0x30: return shift ? '}' : ']';
        case 0x31: return shift ? '|' : '\\';
        case 0x33: return shift ? ':' : ';';
        case 0x34: return shift ? '"' : '\'';
        case 0x35: return shift ? '~' : '`';
        case 0x36: return shift ? '<' : ',';
        case 0x37: return shift ? '>' : '.';
        case 0x38: return shift ? '?' : '/';
        /* navigation / editing */
        case 0x4f: return OC_KEY_RIGHT;
        case 0x50: return OC_KEY_LEFT;
        case 0x51: return OC_KEY_DOWN;
        case 0x52: return OC_KEY_UP;
        case 0x4a: return OC_KEY_HOME;
        case 0x4d: return OC_KEY_END;
        case 0x4b: return OC_KEY_PGUP;
        case 0x4e: return OC_KEY_PGDN;
        case 0x49: return OC_KEY_INS;
        case 0x4c: return OC_KEY_DEL;
        /* F1..F12 */
        case 0x3a: case 0x3b: case 0x3c: case 0x3d:
        case 0x3e: case 0x3f: case 0x40: case 0x41:
        case 0x42: case 0x43: case 0x44: case 0x45:
            return (u16)(OC_KEY_F1 + (usage - 0x3a));
        /* keypad */
        case 0x54: return '/';
        case 0x55: return '*';
        case 0x56: return '-';
        case 0x57: return '+';
        case 0x58: return OC_KEY_ENTER;
        case 0x59: case 0x5a: case 0x5b: case 0x5c: case 0x5d:
        case 0x5e: case 0x5f: case 0x60: case 0x61:
            return (u16)('1' + (usage - 0x59));
        case 0x62: return '0';
        case 0x63: return '.';
    }
    return 0xffff;   /* not printable / not mapped */
}

static void hid_kbd_handle(driver_usb_hid_kbd_t *k, const u8 *rep, int len) {
    if (len < 8) return;
    u8 mods = rep[0];
    /* inject newly-pressed keys (diff against the previous report) */
    for (int i = 2; i < 8; i++) {
        u8 usage = rep[i];
        if (!usage) continue;
        int was = 0;
        for (int j = 2; j < 8; j++)
            if (k->prev[j] == usage) was = 1;
        if (was) continue;
        u16 kc = hid_kbd_translate(usage, mods);
        if (kc != 0xffff) {
            driver_input_keyboard_inject(kc);
            k->keys_injected++;
        }
    }
    memcpy(k->prev, rep, 8);
    k->prev_valid = 1;
}

/* ------------------------------------------------------------------
 * mouse handling
 * ------------------------------------------------------------------ */

static void hid_mouse_push(driver_usb_hid_mouse_t *m, int dx, int dy,
                           u8 buttons, int wheel) {
    driver_usb_mouse_event_t *e = &m->ring[m->rhead % MOUSE_RING];
    e->dx = dx; e->dy = dy;
    e->buttons = buttons;
    e->wheel = (u8)wheel;
    m->rhead++;
    if ((int)(m->rhead - m->rtail) > MOUSE_RING)
        m->rtail = m->rhead - MOUSE_RING;
    m->events++;
    m->acc_x += dx;
    m->acc_y += dy;
    m->buttons = buttons;
}

static void hid_mouse_handle(driver_usb_hid_mouse_t *m, const u8 *rep,
                             int len) {
    if (len < 3) return;
    int dx = (signed char)rep[1];
    int dy = (signed char)rep[2];
    int wheel = (len >= 4) ? (signed char)rep[3] : 0;
    hid_mouse_push(m, dx, dy, rep[0], wheel);
}

/* ------------------------------------------------------------------
 * class-driver probe hooks
 * ------------------------------------------------------------------ */

static int hid_kbd_probe(driver_usb_dev_t *dev) {
    for (int i = 0; i < HID_MAX_KBD; i++) {
        if (g_kbd[i].up && g_kbd[i].dev == dev) return 0;  /* ours */
    }
    driver_usb_interface_t *ifp = driver_usb_find_if(dev, USB_CLASS_HID, 1, 1, 0);
    if (!ifp) return -1;
    driver_usb_endpoint_t *ep = driver_usb_find_ep(dev, ifp->number,
                                     USB_EP_ATTR_INTERRUPT, 1);
    if (!ep) return -1;
    for (int i = 0; i < HID_MAX_KBD; i++) {
        driver_usb_hid_kbd_t *k = &g_kbd[i];
        if (k->up) continue;
        memset(k, 0, sizeof(*k));
        k->dev = dev;
        k->ep = ep;
        /* switch to the boot protocol + silence the idle timer */
        driver_usb_control(dev, 0x21, HID_SET_PROTOCOL, 0, ifp->number,
                    NULL, 0);
        driver_usb_control(dev, 0x21, HID_SET_IDLE, 0, ifp->number, NULL, 0);
        k->up = 1;
        screen_console_puts("usb-hid: keyboard attached (boot protocol)\n");
        return 0;
    }
    return -1;
}

static void hid_kbd_disconnect(driver_usb_dev_t *dev) {
    for (int i = 0; i < HID_MAX_KBD; i++) {
        if (g_kbd[i].up && g_kbd[i].dev == dev) {
            memset(&g_kbd[i], 0, sizeof(g_kbd[i]));
            screen_console_puts("usb-hid: keyboard removed\n");
        }
    }
}

static int hid_mouse_probe(driver_usb_dev_t *dev) {
    for (int i = 0; i < HID_MAX_MOUSE; i++) {
        if (g_mouse[i].up && g_mouse[i].dev == dev) return 0;
    }
    driver_usb_interface_t *ifp = driver_usb_find_if(dev, USB_CLASS_HID, 1, 2, 0);
    if (!ifp) return -1;
    driver_usb_endpoint_t *ep = driver_usb_find_ep(dev, ifp->number,
                                     USB_EP_ATTR_INTERRUPT, 1);
    if (!ep) return -1;
    for (int i = 0; i < HID_MAX_MOUSE; i++) {
        driver_usb_hid_mouse_t *m = &g_mouse[i];
        if (m->up) continue;
        memset(m, 0, sizeof(*m));
        m->dev = dev;
        m->ep = ep;
        driver_usb_control(dev, 0x21, HID_SET_PROTOCOL, 0, ifp->number,
                    NULL, 0);
        driver_usb_control(dev, 0x21, HID_SET_IDLE, 0, ifp->number, NULL, 0);
        m->up = 1;
        screen_console_puts("usb-hid: mouse attached (boot protocol)\n");
        return 0;
    }
    return -1;
}

static void hid_mouse_disconnect(driver_usb_dev_t *dev) {
    for (int i = 0; i < HID_MAX_MOUSE; i++) {
        if (g_mouse[i].up && g_mouse[i].dev == dev) {
            memset(&g_mouse[i], 0, sizeof(g_mouse[i]));
            screen_console_puts("usb-hid: mouse removed\n");
        }
    }
}

/* ------------------------------------------------------------------
 * polling + API
 * ------------------------------------------------------------------ */

void driver_usb_hid_poll(void) {
    u8 rep[8];
    for (int i = 0; i < HID_MAX_KBD; i++) {
        driver_usb_hid_kbd_t *k = &g_kbd[i];
        if (!k->up || !k->dev->present) continue;
        int rc = driver_usb_interrupt_transfer(k->dev, k->ep->addr, rep, 8);
        if (rc > 0) hid_kbd_handle(k, rep, rc);
        else if (rc == OC_USB_ESTALL) {
            driver_usb_control(k->dev, 0x02, USB_REQ_CLEAR_FEAT, 0,
                        k->ep->addr, NULL, 0);
            driver_usb_tog_reset(k->dev, k->ep->addr);
        }
    }
    for (int i = 0; i < HID_MAX_MOUSE; i++) {
        driver_usb_hid_mouse_t *m = &g_mouse[i];
        if (!m->up || !m->dev->present) continue;
        int rc = driver_usb_interrupt_transfer(m->dev, m->ep->addr, rep, 8);
        if (rc >= 3) hid_mouse_handle(m, rep, rc);
        else if (rc == OC_USB_ESTALL) {
            driver_usb_control(m->dev, 0x02, USB_REQ_CLEAR_FEAT, 0,
                        m->ep->addr, NULL, 0);
            driver_usb_tog_reset(m->dev, m->ep->addr);
        }
    }
}

int driver_usb_hid_num_keyboards(void) {
    int n = 0;
    for (int i = 0; i < HID_MAX_KBD; i++)
        if (g_kbd[i].up) n++;
    return n;
}

int driver_usb_hid_num_mice(void) {
    int n = 0;
    for (int i = 0; i < HID_MAX_MOUSE; i++)
        if (g_mouse[i].up) n++;
    return n;
}

int driver_usb_mouse_read_event(driver_usb_mouse_event_t *out) {
    if (!out) return 0;
    for (int i = 0; i < HID_MAX_MOUSE; i++) {
        driver_usb_hid_mouse_t *m = &g_mouse[i];
        if (!m->up) continue;
        if (m->rtail < m->rhead) {
            *out = m->ring[m->rtail % MOUSE_RING];
            m->rtail++;
            return 1;
        }
    }
    return 0;
}

int driver_usb_mouse_num_events(void) {
    int n = 0;
    for (int i = 0; i < HID_MAX_MOUSE; i++)
        if (g_mouse[i].up) n += (int)(g_mouse[i].rhead - g_mouse[i].rtail);
    return n;
}

void driver_usb_mouse_print_state(void) {
    char line[96]; char n[24];
    int any = 0;
    for (int i = 0; i < HID_MAX_MOUSE; i++) {
        driver_usb_hid_mouse_t *m = &g_mouse[i];
        if (!m->up) continue;
        any = 1;
        strcpy(line, "  mouse: events=");
        u64_to_str(m->events, n); strcat(line, n);
        strcat(line, " dx_sum=");
        u64_to_str(m->acc_x, n); strcat(line, n);
        strcat(line, " dy_sum=");
        u64_to_str(m->acc_y, n); strcat(line, n);
        strcat(line, " buttons=");
        u64_to_str(m->buttons, n); strcat(line, n);
        strcat(line, " queued=");
        u64_to_str((u64)(m->rhead - m->rtail), n);
        strcat(line, n);
        screen_console_puts(line);
        screen_console_puts("\n");
    }
    if (!any) screen_console_puts("  mouse: none\n");
}

int driver_usb_hid_init(void) {
    driver_usb_register_driver("hid-kbd", USB_CLASS_HID,
                        hid_kbd_probe, hid_kbd_disconnect);
    return driver_usb_register_driver("hid-mouse", USB_CLASS_HID,
                               hid_mouse_probe, hid_mouse_disconnect);
}
