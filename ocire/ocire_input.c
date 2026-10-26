/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP-01-ocire-core
 * File: ocire/ocire_input.c
 * Purpose: OCIRE input sources.
 *
 * Serial:   characters from COM1; CR and LF are both accepted as the
 *           line terminator and both normalise to '\n'.
 * PS/2:     scancode set 1 (translation on), US layout, with Shift and
 *           CapsLock tracking and E0-prefixed extended keys ignored
 *           (arrows / keypad duplicates are not needed by the shell).
 *           The polling read is IRQ-free: OCIRE never enables the PIC,
 *           so the keyboard status port is polled directly.
 */

#include "ocire_input.h"
#include "ocire_io.h"
#include "ocire_console.h"

/* ---- PS/2 keyboard (scancode set 1, US layout) ---------------------- */

#define KBD_DATA   0x60
#define KBD_STATUS 0x64

static int g_shift;
static int g_caps;
static int g_e0;

/* Unshifted map for scancodes 0x02..0x39 (set 1). */
static const char map_lo[0x38] = {
    /* 0x00 */ 0,   '1','2','3','4','5','6','7','8','9','0','-','=', '\b',
    /* 0x0E */ 0,   /* 0x0F (Tab) */ 0,
    /* 0x10 */ 'q','w','e','r','t','y','u','i','o','p','[',']','\n',
    /* 0x1D */ 0,   /* LCtrl */
    /* 0x1E */ 'a','s','d','f','g','h','j','k','l',';','\'','`',
    /* 0x2A */ 0,   /* LShift */
    /* 0x2B */ '\\',
    /* 0x2C */ 'z','x','c','v','b','n','m',',','.','/',
    /* 0x36 */ 0,   /* RShift */
    /* 0x37 */ '*'  /* keypad * */
};

/* Shifted map for the same range. */
static const char map_hi[0x38] = {
    /* 0x00 */ 0,   '!','@','#','$','%','^','&','*','(',')','_','+', '\b',
    /* 0x0E */ 0,   0,
    /* 0x10 */ 'Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    /* 0x1D */ 0,
    /* 0x1E */ 'A','S','D','F','G','H','J','K','L',':','"','~',
    /* 0x2A */ 0,
    /* 0x2B */ '|',
    /* 0x2C */ 'Z','X','C','V','B','N','M','<','>','?',
    /* 0x36 */ 0,
    /* 0x37 */ '*'
};

static char kbd_poll(void) {
    if (!(oc_inb(KBD_STATUS) & 0x01)) return 0;
    u8 sc = oc_inb(KBD_DATA);
    if (sc == 0xE0) { g_e0 = 1; return 0; }     /* extended prefix */
    if (g_e0) { g_e0 = 0; return 0; }            /* drop extended keys */

    switch (sc) {
    case 0x2A: case 0x36: g_shift = 1; return 0; /* press  */
    case 0xAA: case 0xB6: g_shift = 0; return 0; /* release */
    case 0x3A: g_caps = !g_caps; return 0;       /* caps toggle */
    default: break;
    }
    if (sc & 0x80) return 0;                     /* any other release */
    if (sc < 0x02 || sc > 0x37) return 0;

    char c = g_shift ? map_hi[sc] : map_lo[sc];
    if (!c) return 0;
    if (g_caps) {
        /* CapsLock inverts the case for letters only. */
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        else if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    }
    return c;
}

/* ---- public API ------------------------------------------------------ */

void ocire_input_init(void) {
    g_shift = 0;
    g_caps = 0;
    g_e0 = 0;
    /* Drain any stale bytes the firmware or GRUB left in the output
     * buffer so the first shell prompt is clean. */
    while (oc_inb(KBD_STATUS) & 0x01) (void)oc_inb(KBD_DATA);
    while (ocire_serial_rx_ready()) (void)ocire_serial_rx();
}

int ocire_getchar(char *out) {
    if (ocire_serial_rx_ready()) {
        char c = ocire_serial_rx();
        if (c == '\r') c = '\n';
        *out = c;
        return 1;
    }
    char c = kbd_poll();
    if (c) { *out = c; return 1; }
    return 0;
}
