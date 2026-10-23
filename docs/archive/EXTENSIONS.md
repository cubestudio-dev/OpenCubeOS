<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 cubestudio-dev <cubestudio@qq.com> -->

# Open Cube OS — WP-01 Extension API

WP-01 ships **four** L0→L1 extension points. Each is a global C function declared in `l1/l1_ext.h`. L1 code calls these without linking anything else from L0.

All function signatures are stable for the WP-01 release. Future WPs may add new functions but will not change existing ones incompatibly.

---

## 1. Framebuffer access

```c
const screen_fb_info_t* l1_ext_fb_get_info(void);
```

Returns a pointer to the L0 framebuffer descriptor. L1 can write pixels directly to `fb->addr`.

### `screen_fb_info_t` (from `drivers/display/screen_fb.h`)

```c
typedef struct screen_fb_info {
    u8*  addr;        /* framebuffer base, identity-mapped */
    u32  pitch;       /* bytes per scanline (may be > width * 4) */
    u32  width;       /* pixels per scanline */
    u32  height;      /* scanlines */
    u8   bpp;         /* bits per pixel (always 32 in WP-01) */
    u8   red_pos,   red_size;     /* RGB mask info from the bootloader */
    u8   green_pos, green_size;
    u8   blue_pos,  blue_size;
} screen_fb_info_t;
```

### Usage

```c
#include "l1_ext.h"

void paint_red(void) {
    const screen_fb_info_t* fb = l1_ext_fb_get_info();
    u32 red = (0xFF << fb->red_pos);
    for (u32 y = 0; y < fb->height; ++y) {
        u32* row = (u32*)(fb->addr + (u64)y * fb->pitch);
        for (u32 x = 0; x < fb->width; ++x) row[x] = red;
    }
}
```

### Helpers L1 can use

- `u32 screen_fb_rgb(u8 r, u8 g, u8 b)` — pack an RGB triplet into a framebuffer-native pixel value, using the current mask info.
- `void screen_fb_put_pixel(u32 x, u32 y, u32 pixel)` — clipped pixel write.
- `void screen_fb_fill_rect(u32 x, u32 y, u32 w, u32 h, u32 pixel)` — filled rectangle.
- `void screen_fb_blit(u32 x, u32 y, u32 w, u32 h, const u32* src, u32 src_pitch_bytes)` — blit a source bitmap.
- `void screen_fb_clear(u32 pixel)` — clear the whole framebuffer.

---

## 2. Renderer replacement

```c
screen_renderer_t* l1_ext_set_renderer(screen_renderer_t* new_renderer);
```

Swaps in a new renderer and returns the previous one (so L1 can restore it on unload).

### `screen_renderer_t` (from `drivers/display/screen_font.h`)

```c
typedef struct screen_renderer {
    void (*draw_glyph)(u32 x, u32 y, u32 cp, u32 fg_pixel, u32 bg_pixel);
    u32   cell_w;     /* pixels per cell, horizontally */
    u32   cell_h;     /* pixels per cell, vertically */
    const char* name;
} screen_renderer_t;
```

`draw_glyph` must paint a single code point `cp` at pixel coordinate `(x, y)`. The cell is `cell_w × cell_h` pixels. L0 calls `draw_glyph` once per character.

### Usage

```c
#include "l1_ext.h"

static void my_draw_glyph(u32 x, u32 y, u32 cp, u32 fg, u32 bg) {
    /* ... your rendering code ... */
}

static screen_renderer_t my_renderer = {
    .draw_glyph = my_draw_glyph,
    .cell_w = 16, .cell_h = 24,
    .name = "my-16x24",
};

void install_my_renderer(void) {
    screen_renderer_t* prev = l1_ext_set_renderer(&my_renderer);
    /* ... use console with my renderer ... */
    l1_ext_set_renderer(prev);   /* restore */
}
```

The default L0 renderer (`screen_default_renderer`) uses the built-in 8×16 bitmap font.

---

## 3. Font engine registration

```c
int l1_ext_register_font_engine(screen_font_engine_t* engine);
screen_font_engine_t* l1_ext_font_engine_by_name(const char* name);
screen_font_engine_t* l1_ext_default_font_engine(void);
```

L1 registers a named font engine. Multiple engines may be registered; lookup by name.

### `screen_font_engine_t` (from `l1/l1_ext.h`)

```c
typedef struct screen_font_engine {
    const char* name;
    const u8* (*lookup)(struct screen_font_engine* self, u32 cp);
    u32 glyph_w;
    u32 glyph_h;
} screen_font_engine_t;
```

`lookup` returns a pointer to `(glyph_w * glyph_h + 7) / 8` bytes of bitmap data for code point `cp`. Each row is `(glyph_w + 7) / 8` bytes. Bit 7 (0x80) of each byte is the leftmost pixel.

### Usage

```c
#include "l1_ext.h"

static const u8 my_glyphs[256][16] = { /* ... */ };

static const u8* my_lookup(screen_font_engine_t* self, u32 cp) {
    (void)self;
    return my_glyphs[cp & 0xFF];
}

static screen_font_engine_t my_engine = {
    .name = "my-psf",
    .lookup = my_lookup,
    .glyph_w = 8, .glyph_h = 16,
};

void register_my_font(void) {
    l1_ext_register_font_engine(&my_engine);
}
```

The default L0 font engine (`l1_ext_default_font_engine()`) is named `"default-8x16-vga"` and serves glyphs from the built-in `oc_font8x16[256][16]` table.

---

## 4. Console output hook

```c
typedef int (*screen_console_hook_fn)(void* ctx, u8 ch);
void l1_ext_set_console_hook(screen_console_hook_fn hook, void* ctx);
```

L1 installs a hook that receives every character the console emits. The hook is called **before** L0 draws the character on screen.

**Return value:** `0` = let L0 draw the char normally; non-zero = L1 handled it (L0 skips the framebuffer draw).

### Usage

```c
#include "l1_ext.h"

static int my_hook(void* ctx, u8 ch) {
    /* Tee every console char to a serial line, a log buffer, etc. */
    uart_putc(ch);
    return 0;  /* let L0 still draw it on the framebuffer */
}

void install_serial_tee(void) {
    l1_ext_set_console_hook(my_hook, NULL);
}
```

To disable: `l1_ext_set_console_hook(NULL, NULL);`

The L0 kernel itself uses this hook in `kmain.c` to tee console output to COM1 (port 0x3F8), so `qemu-system-x86_64 ... -serial stdio` shows the boot log.

---

## Self-test

`int l1_ext_self_test(void);`

Exercises all four extension points from inside L0 and returns the number that passed (`0..4`). Called by `kmain` near the end of the boot log. Returns `4` on a healthy system.

Source: `l1/l1_selftest.c`. Read it to see exactly which calls each test makes.

---

## ABI stability

The four extension functions and their structs are frozen for the WP-01 release. Specifically:

- `l1_ext_fb_get_info` — signature frozen; `screen_fb_info_t` layout frozen.
- `l1_ext_set_renderer` — signature frozen; `screen_renderer_t` layout frozen.
- `l1_ext_register_font_engine` / `l1_ext_font_engine_by_name` / `l1_ext_default_font_engine` — signatures frozen; `screen_font_engine_t` layout frozen.
- `l1_ext_set_console_hook` — signature frozen; `screen_console_hook_fn` typedef frozen.

Future WPs may add new fields to the structs **at the end**, may add new functions, but will not remove or repurpose existing ones.

## Loading model

WP-01 does not yet have a dynamic module loader — L1 is expected to be linked into the same binary as L0 for now. A proper loadable-module interface is planned for a later WP. The extension API is designed to survive that transition unchanged.
