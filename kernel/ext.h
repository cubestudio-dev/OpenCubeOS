/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-01
 * File: kernel/ext.h
 * Purpose: L1 EXTENSION API - the contract L0 exposes to L1 extensions.
 *
 * WP-01 ships FOUR extension points:
 *
 *   1. Framebuffer access          - oc_ext_fb_get_info()
 *   2. Renderer replacement        - oc_ext_set_renderer()
 *   3. Font engine registration    - oc_ext_register_font_engine()
 *   4. Console output hook         - oc_ext_set_console_hook()
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
#include "fb.h"
#include "font.h"
#include "console.h"
#include "ab_update.h"   /* WP-10u: oc_update_status_t + pkg info */

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ *
 * 1. FRAMEBUFFER ACCESS
 *
 * L1 calls oc_ext_fb_get_info() to get a pointer to the framebuffer
 * descriptor (addr, pitch, width, height, bpp, masks). L1 can then
 * write pixels directly to addr.
 *
 * Example (L1 code):
 *     const oc_fb_info_t* fb = oc_ext_fb_get_info();
 *     for (u32 y = 0; y < fb->height; ++y) {
 *         u32* row = (u32*)(fb->addr + (u64)y * fb->pitch);
 *         for (u32 x = 0; x < fb->width; ++x) row[x] = 0xFFFFFFFF;
 *     }
 * ------------------------------------------------------------------ */
const oc_fb_info_t* oc_ext_fb_get_info(void);

/* ------------------------------------------------------------------ *
 * 2. RENDERER REPLACEMENT
 *
 * L1 supplies an oc_renderer_t with its own draw_glyph function and
 * cell dimensions. oc_ext_set_renderer() swaps it in and returns the
 * previous renderer so L1 can restore it on unload.
 *
 * Example (L1 code):
 *     static oc_renderer_t my_renderer = {
 *         .draw_glyph = my_draw_glyph,
 *         .cell_w = 16, .cell_h = 24,
 *         .name = "my-16x24",
 *     };
 *     oc_renderer_t* prev = oc_ext_set_renderer(&my_renderer);
 *     // ... use console with my renderer ...
 *     oc_ext_set_renderer(prev);  // restore
 * ------------------------------------------------------------------ */
oc_renderer_t* oc_ext_set_renderer(oc_renderer_t* new_renderer);

/* ------------------------------------------------------------------ *
 * 3. FONT ENGINE REGISTRATION
 *
 * L1 registers a named font engine. The engine is responsible for
 * producing glyph bitmaps on demand. The default font engine returns
 * pointers into the built-in oc_font8x16[] table.
 *
 * A font engine is described by this struct. L1 fills it in and calls
 * oc_ext_register_font_engine().
 *
 * Multiple engines may be registered; oc_ext_font_engine_by_name()
 * returns the engine registered under the given name (NULL if missing).
 *
 * Example (L1 code):
 *     static oc_font_engine_t my_engine = {
 *         .name = "psf2-unicode",
 *         .lookup = my_psf2_lookup,  // returns 16-byte glyph for cp
 *         .glyph_w = 8, .glyph_h = 16,
 *     };
 *     oc_ext_register_font_engine(&my_engine);
 * ------------------------------------------------------------------ */
typedef struct oc_font_engine {
    const char* name;
    /* Return a pointer to glyph_w * glyph_h / 8 bytes of bitmap data for
     * code point cp. Each row is (glyph_w+7)/8 bytes. */
    const u8* (*lookup)(struct oc_font_engine* self, u32 cp);
    u32 glyph_w;
    u32 glyph_h;
    /* Optional: list of code points the engine claims to support. NULL = all. */
} oc_font_engine_t;

int oc_ext_register_font_engine(oc_font_engine_t* engine);
oc_font_engine_t* oc_ext_font_engine_by_name(const char* name);
oc_font_engine_t* oc_ext_default_font_engine(void);

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
 *     oc_ext_set_console_hook(my_hook, NULL);
 * ------------------------------------------------------------------ */
typedef int (*oc_console_hook_fn)(void* ctx, u8 ch);
void oc_ext_set_console_hook(oc_console_hook_fn hook, void* ctx);

/* Called by the L0 console for every character (do not call from L1). */
void oc_ext_console_hook(u8 ch);

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
 *     if (oc_ext_config_read("update_url", url, sizeof(url)) == 0)
 *         use(url);
 *     oc_ext_config_write("auto_check", "yes");
 * ------------------------------------------------------------------ */
int  oc_ext_config_read(const char *key, char *val_out, int outlen);
int  oc_ext_config_write(const char *key, const char *value);
int  oc_ext_config_get_all(char *buf, int buflen);

/* ------------------------------------------------------------------ *
 * 6. UPDATE CHECK (WP-09-fix5)
 *
 * L1 can run the online update check over HTTP or HTTPS.  The URL and
 * the auto-check flag live in /etc/opencube.conf (see above).  The
 * result codes match oc_check_update() in kernel/update.h:
 *   0 = up to date, 1 = new version available, <0 = transport/parse error.
 *
 * oc_update_info_t is defined in kernel/update.h; declare it here so L1
 * only needs this header.
 * ------------------------------------------------------------------ */
typedef struct {
    char version[32];
    char time[32];
    char changes[256];  /* mirrors oc_update_info_t (kernel/update.h) */
} oc_ext_update_info_t;

int  oc_ext_check_update(oc_ext_update_info_t *out);
int  oc_ext_check_update_async(void);

/* ------------------------------------------------------------------ *
 * 7. IN-SYSTEM UPDATE + A/B SLOTS (WP-10u)
 *
 * L1 can drive the Windows-Update-style system update: check the
 * manifest (with package fields), download/verify/install the package
 * into an A/B slot, switch the boot slot and roll back.  The A/B disk
 * layout, the boot-flag protocol and the package format are documented
 * in kernel/ab_update.h and docs/EXTENSIONS_WP10u.md.
 *
 * oc_update_status_t and oc_update_pkg_info_t are defined in
 * kernel/ab_update.h / kernel/update.h (included above via ab_update.h).
 *
 * Example (L1 code):
 *     oc_ext_update_pkg_info_t pkg;
 *     if (oc_ext_update_check_pkg(&pkg) == 1) {          // new version
 *         oc_ext_update_download(pkg.package_url, "/data/p.tar.gz");
 *         oc_ext_update_verify("/data/p.tar.gz", pkg.package_sha256);
 *         oc_ext_update_install("/data/p.tar.gz", "B");
 *         oc_ext_update_set_boot("B");                   // reboot next
 *     }
 *     oc_ext_update_rollback();                          // back to A
 * ------------------------------------------------------------------ */
int  oc_ext_update_check_pkg(oc_ext_update_pkg_info_t *out);
int  oc_ext_update_download(const char *url, const char *path);
int  oc_ext_update_verify(const char *path, const char *sha256_hex);
int  oc_ext_update_install(const char *pkg_path, const char *slot);
int  oc_ext_update_rollback(void);
int  oc_ext_update_set_boot(const char *slot);
int  oc_ext_update_get_status(oc_update_status_t *out);

/* ------------------------------------------------------------------ *
 * WP-10d-fix2: power management + structured help (7 new interfaces)
 *
 * Example (L1 code):
 *     oc_ext_power_shutdown();                     // power off the box
 *     oc_ext_shell_register_command_ex("mycmd", my_fn,
 *                                      "my command", "WP-L1demo");
 *     oc_ext_shell_list_commands_a_z();            // A-Z view
 *     oc_ext_shell_list_commands_by_wp();          // per-WP view
 * ------------------------------------------------------------------ */
int  oc_ext_power_shutdown(void);   /* -1 unsupported (then halt yourself) */
int  oc_ext_power_suspend(void);    /* -1 unsupported (ACPI S3 pending)    */
void oc_ext_power_halt(void);       /* never returns                       */
void oc_ext_power_reboot(void);     /* never returns                       */
int  oc_ext_shell_register_command_ex(const char *name, int (*fn)(const char *),
                                      const char *help, const char *wp);
void oc_ext_shell_list_commands_a_z(void);
void oc_ext_shell_list_commands_by_wp(void);

/* ------------------------------------------------------------------ *
 * Self-test: invokes every extension point from inside L0 (the kernel
 * calls this at the end of kmain as part of the WP-01 acceptance test).
 * Returns the number of points that passed (0..4).
 * ------------------------------------------------------------------ */
int oc_ext_self_test(void);

#ifdef __cplusplus
}
#endif

#endif /* OC_EXT_H */
