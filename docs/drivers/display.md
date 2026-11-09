<!-- SPDX-License-Identifier: Apache-2.0 -->
# Display and the framebuffer

## The chain

multiboot2 framebuffer tag (geometry + pitch + masks) ->
`screen_fb.c` (the framebuffer primitives) -> `screen_font.c` +
`screen_font_data.c` (the 8x16 engine) -> `screen_console.c` (the
console grid) -> the shell line editor and the TUI.

## The L1 surface

`l1_ext_fb_get_info()` (geometry for module renderers) and the
font-engine protocol `l1_ext_register_font_engine` (the Chinese
16x16 font registers through it) + the console hook
(`l1_ext_set_console_hook`) that lets a module intercept output.

## Commands and tests

`tui-hw` (render diagnostics), `font`/`font-test` (engines), the TUI
suite `tui-test`. In QEMU the default `-vga std` gives 800x600x32 -
the boot log prints the exact geometry and masks it found.
