/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-01
 * File: kernel/ext.h
 * Purpose: L1 EXTENSION API - the contract L0 exposes to L1 extensions.
 *
 * WP-01 ships FOUR extension points:
 *
 *   1. Framebuffer access          - l1_ext_fb_get_info()
 *   2. Renderer replacement        - l1_ext_set_renderer()
 *   3. Font engine registration    - l1_ext_register_font_engine()
 *   4. Console output hook         - l1_ext_set_console_hook()
 *
 * Each function below is part of the L0 ABI: L1 code can call it without
 * linking anything else from L0. The L0 binary exposes these as global
 * symbols; L1 loads as a module / shared object in a later WP.
 *
 * Signatures, usage and examples are documented in docs/EXTENSIONS.md.
 */
#ifndef OC_EXT_H
#define OC_EXT_H

#include "types.h"
#include "screen_fb.h"
#include "screen_font.h"
#include "screen_console.h"
#include "ota_ab.h"   /* WP-10u: ota_update_status_t + pkg info */

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ *
 * 1. FRAMEBUFFER ACCESS
 *
 * L1 calls l1_ext_fb_get_info() to get a pointer to the framebuffer
 * descriptor (addr, pitch, width, height, bpp, masks). L1 can then
 * write pixels directly to addr.
 *
 * Example (L1 code):
 *     const screen_fb_info_t* fb = l1_ext_fb_get_info();
 *     for (u32 y = 0; y < fb->height; ++y) {
 *         u32* row = (u32*)(fb->addr + (u64)y * fb->pitch);
 *         for (u32 x = 0; x < fb->width; ++x) row[x] = 0xFFFFFFFF;
 *     }
 * ------------------------------------------------------------------ */
const screen_fb_info_t* l1_ext_fb_get_info(void);

/* ------------------------------------------------------------------ *
 * 2. RENDERER REPLACEMENT
 *
 * L1 supplies an screen_renderer_t with its own draw_glyph function and
 * cell dimensions. l1_ext_set_renderer() swaps it in and returns the
 * previous renderer so L1 can restore it on unload.
 *
 * Example (L1 code):
 *     static screen_renderer_t my_renderer = {
 *         .draw_glyph = my_draw_glyph,
 *         .cell_w = 16, .cell_h = 24,
 *         .name = "my-16x24",
 *     };
 *     screen_renderer_t* prev = l1_ext_set_renderer(&my_renderer);
 *     // ... use console with my renderer ...
 *     l1_ext_set_renderer(prev);  // restore
 * ------------------------------------------------------------------ */
screen_renderer_t* l1_ext_set_renderer(screen_renderer_t* new_renderer);

/* ------------------------------------------------------------------ *
 * 3. FONT ENGINE REGISTRATION
 *
 * L1 registers a named font engine. The engine is responsible for
 * producing glyph bitmaps on demand. The default font engine returns
 * pointers into the built-in screen_font8x16[] table.
 *
 * A font engine is described by this struct. L1 fills it in and calls
 * l1_ext_register_font_engine().
 *
 * Multiple engines may be registered; l1_ext_font_engine_by_name()
 * returns the engine registered under the given name (NULL if missing).
 *
 * Example (L1 code):
 *     static screen_font_engine_t my_engine = {
 *         .name = "psf2-unicode",
 *         .lookup = my_psf2_lookup,  // returns 16-byte glyph for cp
 *         .glyph_w = 8, .glyph_h = 16,
 *     };
 *     l1_ext_register_font_engine(&my_engine);
 * ------------------------------------------------------------------ */
typedef struct screen_font_engine {
    const char* name;
    /* Return a pointer to glyph_w * glyph_h / 8 bytes of bitmap data for
     * code point cp. Each row is (glyph_w+7)/8 bytes. */
    const u8* (*lookup)(struct screen_font_engine* self, u32 cp);
    u32 glyph_w;
    u32 glyph_h;
    /* Optional: list of code points the engine claims to support. NULL = all. */
} screen_font_engine_t;

int l1_ext_register_font_engine(screen_font_engine_t* engine);
screen_font_engine_t* l1_ext_font_engine_by_name(const char* name);
screen_font_engine_t* l1_ext_default_font_engine(void);

/* ------------------------------------------------------------------ *
 * 4. CONSOLE OUTPUT HOOK
 *
 * L1 can install a hook that receives every character the console
 * emits. This is how L1 can tee console output to a serial line, a
 * ring buffer, a network socket, etc.
 *
 * The hook is called BEFORE the character is drawn on screen, so L1
 * can even suppress the char by returning non-zero (but then the L1
 * hook is responsible for any output the user should see).
 *
 * Return value: 0 = let L0 draw the char normally; non-zero = L1 handled it.
 *
 * Example (L1 code):
 *     static int my_hook(void* ctx, u8 ch) {
 *         uart_putc(ch);
 *         return 0;  // let L0 still draw it on the framebuffer
 *     }
 *     l1_ext_set_console_hook(my_hook, NULL);
 * ------------------------------------------------------------------ */
typedef int (*screen_console_hook_fn)(void* ctx, u8 ch);
void l1_ext_set_console_hook(screen_console_hook_fn hook, void* ctx);

/* Called by the L0 console for every character (do not call from L1).
 * BUG-0091 FIX (A16-4): returns the hook's verdict (0 = nothing
 * handled, L0 draws; non-zero = hook handled/suppressed the char),
 * matching the documented contract above. */
int l1_ext_console_hook(u8 ch);

/* ------------------------------------------------------------------ *
 * 5. SYSTEM CONFIGURATION (WP-09-fix5)
 *
 * L1 reads/writes the system configuration file /etc/opencube.conf - the
 * first user-editable config file of Open Cube OS.  ASCII "key=value"
 * lines, '#' comments; see kernel/config.h and docs/CONFIG.md for the
 * documented missing/empty/invalid-value semantics.
 *
 * Example (L1 code):
 *     char url[256];
 *     if (l1_ext_config_read("update_url", url, sizeof(url)) == 0)
 *         use(url);
 *     l1_ext_config_write("auto_check", "yes");
 * ------------------------------------------------------------------ */
int  l1_ext_config_read(const char *key, char *val_out, int outlen);
int  l1_ext_config_write(const char *key, const char *value);
int  l1_ext_config_get_all(char *buf, int buflen);

/* ------------------------------------------------------------------ *
 * 6. UPDATE CHECK (WP-09-fix5)
 *
 * L1 can run the online update check over HTTP or HTTPS.  The URL and
 * the auto-check flag live in /etc/opencube.conf (see above).  The
 * result codes match ota_update_check() in kernel/update.h:
 *   0 = up to date, 1 = new version available, <0 = transport/parse error.
 *
 * ota_update_info_t is defined in kernel/update.h; declare it here so L1
 * only needs this header.
 * ------------------------------------------------------------------ */
typedef struct {
    char version[32];
    char time[32];
    char changes[256];  /* mirrors ota_update_info_t (kernel/update.h) */
} l1_ext_update_info_t;

int  l1_ext_check_update(l1_ext_update_info_t *out);
int  l1_ext_check_update_async(void);

/* ------------------------------------------------------------------ *
 * 7. IN-SYSTEM UPDATE + A/B SLOTS (WP-10u)
 *
 * L1 can drive the Windows-Update-style system update: check the
 * manifest (with package fields), download/verify/install the package
 * into an A/B slot, switch the boot slot and roll back.  The A/B disk
 * layout, the boot-flag protocol and the package format are documented
 * in kernel/ota_ab_update.h and docs/EXTENSIONS_WP10u.md.
 *
 * ota_update_status_t and ota_update_pkg_info_t are defined in
 * kernel/ota_ab_update.h / kernel/update.h (included above via ota_ab_update.h).
 *
 * Example (L1 code):
 *     l1_ext_update_pkg_info_t pkg;
 *     if (l1_ext_update_check_pkg(&pkg) == 1) {          // new version
 *         l1_ext_update_download(pkg.package_url, "/data/p.tar.gz");
 *         l1_ext_update_verify("/data/p.tar.gz", pkg.package_sha256);
 *         l1_ext_update_install("/data/p.tar.gz", "B");
 *         l1_ext_update_set_boot("B");                   // reboot next
 *     }
 *     l1_ext_update_rollback();                          // back to A
 * ------------------------------------------------------------------ */
int  l1_ext_update_check_pkg(l1_ext_update_pkg_info_t *out);
int  l1_ext_update_download(const char *url, const char *path);
int  l1_ext_update_verify(const char *path, const char *crypto_sha256_hex);
int  l1_ext_update_install(const char *pkg_path, const char *slot);
int  l1_ext_update_rollback(void);
int  l1_ext_update_set_boot(const char *slot);
int  l1_ext_update_get_status(ota_update_status_t *out);

/* ------------------------------------------------------------------ *
 * WP-10d-fix2: power management + structured help (7 new interfaces)
 *
 * Example (L1 code):
 *     l1_ext_power_shutdown();                     // power off the box
 *     l1_ext_shell_register_command_ex("mycmd", my_fn,
 *                                      "my command", "WP-L1demo");
 *     l1_ext_shell_list_commands_a_z();            // A-Z view
 *     l1_ext_shell_list_commands_by_wp();          // per-WP view
 * ------------------------------------------------------------------ */
int  l1_ext_power_shutdown(void);   /* -1 unsupported (then halt yourself) */
int  l1_ext_power_suspend(void);    /* -1 unsupported (ACPI S3 pending)    */
void l1_ext_power_halt(void);       /* never returns                       */
void l1_ext_power_reboot(void);     /* never returns                       */
int  l1_ext_shell_register_command_ex(const char *name, int (*fn)(const char *),
                                      const char *help, const char *wp);
void l1_ext_shell_list_commands_a_z(void);
void l1_ext_shell_list_commands_by_wp(void);

/* ------------------------------------------------------------------ *
 * Self-test: invokes every extension point from inside L0 (the kernel
 * calls this at the end of kmain as part of the WP-01 acceptance test).
 * Returns the number of points that passed (0..4).
 * ------------------------------------------------------------------ */
int l1_ext_self_test(void);

#ifdef __cplusplus
}
#endif

#endif /* OC_EXT_H */
