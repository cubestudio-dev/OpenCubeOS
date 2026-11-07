<!-- SPDX-License-Identifier: Apache-2.0 -->
# Chinese console layer

Open Cube OS keeps every system-visible string ASCII - and still
renders Chinese user content, through a dedicated layer under
`chinese/`.

## What it is

| Piece | File | What it does |
|-------|------|--------------|
| 16x16 CJK font | `chinese/chinese_font_data.c` | bitmap glyphs for the console |
| GB encoding | `chinese/encoding.c` | gb2312 / utf-8 / ascii codecs |
| CN console | `chinese/console_cn.c` | renders CJK rows on the framebuffer |
| Renderer | `chinese/render_cn.c` | glyph raster pipeline |
| Pinyin IME | `chinese/pinyin_data.c` + ime | syllable resolution and input |
| CN TUI apps | `tui_cn_*.c` | hardware / net / sysinfo in CN |

## Commands

```
oc> chinese          (status: font / encoding / render / ime)
oc> chinesetest      (full Chinese-support regression suite)
oc> font             (list registered fonts + active renderer)
oc> font-test        (render a mixed CJK/ASCII sample row)
oc> encoding         (codec status: gb2312 / utf-8 / ascii)
oc> encoding-test    (quick codec round-trip check)
oc> ime              (pinyin IME status)
oc> ime -i           (interactive pinyin input on the console line)
oc> lang             (language console switch)
```

## The design rule

- **System strings are ASCII**: command names, prompts, boot messages,
  help text - the firmware/hardware compatibility contract (see
  `CONTRIBUTING.md` ground rule 1).
- **User content may be anything**: filenames, file contents, editor
  buffers, network payloads - and the Chinese layer renders them on
  the console and in the TUI.

So a terminal without CJK fonts still gets a fully usable system;
with the layer on, mixed-language content displays correctly.

## Build configuration

```
make CONFIG_CHINESE=n     (also forces CONFIG_TUI=n)
```

The off-path is real code: `console_cn` routes to the ASCII
equivalents in `screen_console`, and the configurator's generated
header maps the disabled calls to those equivalents. A
`CONFIG_CHINESE=n CONFIG_TUI=n` build boots with 216 commands
(233 - 17 Chinese/TUI commands) - the full config boots 233.
