#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 */
# Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
# Open Cube OS - 3.0.0-WP-runtime-hardening-and-experience-rebase
# File: tools/gen_ocbl_font.py
# Purpose: regenerate ocbl/efi/ocbl_font_data.c from the kernel's
#          screen_font8x16 (ocos/kernel/core_console_font_data.c):
#          the ASCII 32..126 slice for the OCBL raw-flavor fb console.
import re
src = open('ocos/kernel/core_console_font_data.c').read()
m = re.search(r'screen_font8x16\[256\]\[16\] = \{(.*?)\n\};', src, re.S)
body = m.group(1)
rows = re.findall(r'/\* (0x[0-9A-Fa-f]{2}) \*/\s*\{([^}]*)\}', body)
glyphs = {int(i, 16): [int(x, 16) for x in v.replace(' ', '').split(',') if x] for i, v in rows}
assert len(glyphs) == 256
out = []
out.append('/* SPDX-License-Identifier: Apache-2.0 */')
out.append('/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */')
out.append('/* Open Cube OS - 3.0.0-WP-runtime-hardening-and-experience-rebase')
out.append(' * File: ocbl/efi/ocbl_font_data.c')
out.append(' * Purpose: the 8x16 ASCII glyph table for the raw-flavor frame-')
out.append(' *          buffer console (chars 32..126, extracted from the')
out.append(' *          kernel screen_font8x16 by tools/gen_ocbl_font.py -')
out.append(' *          regenerate with: python3 tools/gen_ocbl_font.py).')
out.append(' *          Bit 7 of each row byte is the leftmost pixel.')
out.append(' */')
out.append('')
out.append('#include "ocbl_font.h"')
out.append('')
out.append('const u8 ocbl_font8x16[95][16] = {')
for c in range(32, 127):
    out.append('    /* %3d %s */ { %s },' % (c, repr(chr(c)), ','.join('0x%02X' % b for b in glyphs[c])))
out.append('};')
open('ocbl/efi/ocbl_font_data.c', 'w').write('\n'.join(out) + '\n')
print('ocbl_font_data.c: 95 glyphs regenerated')
