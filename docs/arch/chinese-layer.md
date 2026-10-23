<!-- SPDX-License-Identifier: Apache-2.0 -->
# The Chinese console layer

`chinese/` renders CJK user content while the system's own strings
stay ASCII:

## Pieces

- `chinese_font_data.c` - the 16x16 bitmap glyphs
- `encoding.c` - gb2312 / utf-8 / ascii codecs (round-trip tested)
- `console_cn.c` - CJK console rows on the framebuffer
- `render_cn.c` - glyph raster pipeline
- `pinyin_data.c` + the IME - syllable resolution, interactive line
- `tui_cn_*.c` - CN hardware/net/sysinfo TUI apps

## Why both worlds work

The ASCII contract is about *system* strings (prompts, command
names, help, boot messages) - hardware-terminal compatibility. User
content (filenames, file bodies, editor buffers) flows through the
codecs and renders on the CJK font when the layer is active. With
`CONFIG_CHINESE=n` the console routes to the ASCII equivalents in
`screen_console` - real code, both paths, one contract.

## Tests

`chinesetest` (full regression), `encoding-test` (codec round-trip),
`font-test` (mixed-row render), `ime-test` (syllable resolution).
