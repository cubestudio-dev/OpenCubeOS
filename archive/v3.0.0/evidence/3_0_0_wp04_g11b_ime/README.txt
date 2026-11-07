Open Cube OS 3.0.0-WP04 G11b - IME revival (ime / ime-test) evidence
=====================================================================

What this batch delivers
------------------------
1. The pinyin IME engine is revived verbatim from v2.2.0:
   - services/display/pinyin_data.c  (generated tables, byte-identical
     copy of v2.2.0 chinese/pinyin_data.c; 396 syllables / 3755 chars)
   - services/display/ime_engine.[ch] (the tui_chinese.c IME section;
     tui_chinese_ime_syllables/chars/test keep their v2.2.0 names so
     the G11c TUI revival links unchanged; ime_match/ime_candidates
     change storage class file-static -> exported for the TUI pad)
   - disp_cmds_cn.c gains ime_console_compose + disp_cmd_ime +
     disp_cmd_ime_test, verbatim from chinese/chinese_cmd.c
   - DISP ops 11/12 (L1SVC_DISP_IME / L1SVC_DISP_IME_TEST) re-occupy
     their v2.2.0 numbers (WP-03 retired them without renumbering)
   - L1 apps: src/L1/ime/, src/L1/ime-test/ (thin l1svc_main shells)
2. "ime -i" (interactive compose line) routes DIRECTLY in disp_l1.c,
   bypassing the core_kcap capture - the '\r' redraw stream must hit
   the console/serial live (same rule as the G11a sshd op).  Plain
   "ime" (status only) stays on the captured path.

Identity-window incident (found and fixed in this batch)
--------------------------------------------------------
make userprogs regenerated 265 programs (+ime/+ime-test) and the final
link hit the linker ASSERT:
    ld: kernel image grew past the 0x400000 identity-window boundary
The boundary is the P3-1 security invariant (user address spaces
leave 0x400000-0x600000 empty; kernel data above 0x400000 is
unreachable from syscall/IRQ context on a user CR3) - it cannot be
raised.  Fix per the ASSERT's own advice: the six raw GB2312 font
tables (~292 KiB .rodata) moved out of the kernel image:

   - tools/gen_font_gz.py wraps chinese_font_glyphs + chinese_font_
     symbols + chinese_font_zone_index + chinese_font_symbol_index +
     chinese_font_uto_gb + chinese_font_idx_to_unicode into ONE gzip
     container inside chinese_font_data.c
     raw 298138 B -> gzip 179649 B (60.3%), saves 118489 B of .rodata
     raw sha256   6ac7bb3cd8536ae0f97ed15db0f0fa7191c1282ee40c250ee1fc9b67382bae89
     gzip sha256  1681ff917fa3fa918504d3b9ab379c5c6e2413d8540475589e79114ea1e35c18
   - chinese_font.c inflates the container LAZILY into one kmalloc'd
     buffer on the first glyph lookup (boot step 3 registers the
     renderer but renders pure ASCII; the heap is always up by the
     time any CJK lookup happens).  A _Static_assert pins the
     generator's section layout at build time.
   - encoding.c's GB2312 encode path walks the zone/symbol index
     tables through new accessors (chinese_font_zone_index_table /
     chinese_font_symbol_index_table).
   - Behavioral parity proof: `chinese` prints byte-identical status
     (data sizes are computed from the counts, not the storage);
     chinesetest is 77/77 PASS on BOTH firmwares; the decompressed
     tables hash to the recorded raw sha256.

Result: build/opencube.elf 2216016 -> 2101376 bytes; the link clears
the boundary with headroom for the G11c TUI block.

Test battery (this folder)
--------------------------
bios_ime_battery.txt    BIOS battery: ime (status, v2.2.0-verbatim),
                        ime-test PASS, chinese (status verbatim),
                        chinesetest 77/77 PASS, font, font-test,
                        encoding, encoding-test PASS, stats tick flow
uefi_ime_smoke.txt      OVMF UEFI: ime (syllables 396), ime-test PASS,
                        chinesetest 77/77 PASS, encoding-test PASS
ime_interactive_compose.txt
                        live '\r' redraw transcript of "ime -i" driven
                        by QEMU monitor sendkey: [n] strip, [ni] 你1 倪2
                        匿3 妮4 candidates, digit 1 commits 你, Enter
                        ends: "ime: composed 1 character(s): 你"

Build state: 0 errors 0 warnings; 265 programs embedded (515264 B);
ISO 12376064 B dual El Torito (BIOS + UEFI).

Remaining C-level commands after G11b: tui x9 (G11c).
