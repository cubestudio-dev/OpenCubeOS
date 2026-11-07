#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 -*-
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""
gen_pinyin_phrase.py: regenerate src/L0/services/display/
pinyin_phrase_data.c from tools/ime_phrases.txt.

3.0.0-WP06g2: the IME phrase library.  Each word is tagged with its
tone-free full pinyin string (e.g. 中国 -> "zhongguo"); the generator
emits a pinyin-string-sorted table the session engine binary-searches
for the longest whole-string match (phrase-first candidate ordering -
"long words first").

CI fix (same batch): the tone-free pinyin now composes from the
KERNEL single-char table (pinyin_data.c - the very syllables the
single-char engine matches), so this generator is fully
self-contained.  The original version imported pypinyin and the
clean CI environment (which installs no pip packages) failed with
ModuleNotFoundError mid-ISO; composing from pinyin_data.c removes
the dependency AND makes the phrase readings agree with the
single-char engine by construction.  Polyphonic words - where the
word's dictionary reading differs from the composed single-char
reading (e.g. 银行 yinhang, not yinxing) - carry an explicit
"word pinyin" pair in ime_phrases.txt; the generator validates
every explicit reading decomposes into legal syllables, exactly
one per character (a misspelling cannot pass).

Every CHARACTER must exist in the level-1 char map (pinyin_data.c,
GB2312 zones 16-55): the generator cross-checks each word's
characters and fails on a miss (a word the single-char engine
cannot type end-to-end would be a broken promise).

Usage: python3 tools/gen_pinyin_phrase.py
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "tools", "ime_phrases.txt")
OUT_C = os.path.join(ROOT, "src", "L0", "services", "display",
                     "pinyin_phrase_data.c")
OUT_H = os.path.join(ROOT, "src", "L0", "services", "display",
                     "pinyin_phrase.h")

# The level-1 char set + the syllable inventory: both rebuilt from
# the emitted pinyin_data.c (the KERNEL table pinyin_char_map is the
# truth - the single-char engine matches exactly these syllables).
PINYIN_DATA = os.path.join(ROOT, "src", "L0", "services", "display",
                           "pinyin_data.c")


def fail(msg):
    print(f"gen_pinyin_phrase: FAIL: {msg}", file=sys.stderr)
    sys.exit(1)


def parse_c_array(text, name):
    """Extract the hex values of a C array by its declarator name.
    The declaration regex (not a bare find) avoids the header comment,
    which mentions every table name and would otherwise shadow it."""
    m = re.search(re.escape(name) + r"\s*\[[^\]]*\]\s*=\s*\{", text)
    if not m:
        fail(f"pinyin_data.c: array '{name}' not found")
    j = m.end() - 1
    k = text.find("}", j)
    if k < 0:
        fail(f"pinyin_data.c: array '{name}' not terminated")
    return [int(t, 16) for t in
            re.findall(r"0[xX][0-9a-fA-F]+", text[j + 1:k])]


def load_kernel_tables():
    """Parse pinyin_data.c: syllable pool + per-syllable offsets +
    the {unicode, syllable_idx} char map.  Returns
    (char2syllable, syllable_set)."""
    with open(PINYIN_DATA, "r", encoding="utf-8") as f:
        text = f.read()
    pool = parse_c_array(text, "pinyin_syllable_pool")
    offs = parse_c_array(text, "pinyin_syllable_off")
    cmap = parse_c_array(text, "pinyin_char_map")
    if len(cmap) % 2 != 0:
        fail("pinyin_data.c: odd pair count")
    if not pool or not offs or not cmap:
        fail("pinyin_data.c: empty table(s)")
    # offs[] carries count+1 entries: syllable i spans off[i]..off[i+1]
    syllables = []
    for i in range(len(offs) - 1):
        s = bytes(pool[offs[i]:offs[i + 1]]).decode("ascii")
        if not s.isalpha() or not s.islower():
            fail(f"pinyin_data.c: bad syllable '{s}'")
        syllables.append(s)
    char2syl = {}
    for i in range(0, len(cmap), 2):
        cp, si = cmap[i], cmap[i + 1]
        if si >= len(syllables):
            fail("pinyin_data.c: syllable index out of range")
        if cp in char2syl and char2syl[cp] != syllables[si]:
            fail(f"pinyin_data.c: char U+{cp:04X} has conflicting "
                 "syllables")
        char2syl[cp] = syllables[si]
    if not char2syl:
        fail("pinyin_data.c: no char values parsed")
    return char2syl, set(syllables)


def validate_explicit(word, py, syl_set):
    """An explicit 'word pinyin' pair must decompose into exactly
    len(word) legal syllables (one per character).  Dynamic
    programming over the syllable set (seg[i] = the set of character
    counts reachable at py[:i] - a position may be reachable with
    DIFFERENT counts, e.g. xian = xi+an = 2 or xian = 1); a
    misspelled reading (a segment that is not a legal syllable)
    cannot pass.  No syllable exceeds 6 letters, so segments longer
    than 7 never match."""
    n = len(word)
    seg = [set() for _ in range(len(py) + 1)]
    seg[0].add(0)
    for i in range(len(py)):
        for j in range(i + 1, min(i + 7, len(py)) + 1):
            if py[i:j] in syl_set:
                for k in seg[i]:
                    seg[j].add(k + 1)
    if n not in seg[len(py)]:
        fail(f"'{word} {py}': the reading does not decompose into "
             f"{n} legal syllable(s)")


def c_escape(s):
    out = []
    for ch in s:
        if ch == '"':
            out.append('\\"')
        elif ch == "\\":
            out.append("\\\\")
        elif ch == "\n":
            out.append("\\n")
        elif ch == "\t":
            out.append("\\t")
        else:
            out.append(ch)
    return "".join(out)


def utf8_bytes(s):
    return ",".join(f"0x{b:02x}" for b in s.encode("utf-8"))


def main():
    char2syl, syl_set = load_kernel_tables()
    words = []
    seen = set()
    with open(SRC, "r", encoding="utf-8") as f:
        for ln, line in enumerate(f, 1):
            w = line.strip()
            if not w or w.startswith("#"):
                continue
            parts = w.split()
            if len(parts) > 2:
                fail(f"line {ln}: '{w}' - one word, or 'word pinyin'")
            word = parts[0]
            explicit = parts[1] if len(parts) == 2 else None
            if len(word) < 2 or len(word) > 4:
                fail(f"line {ln}: '{word}' - only 2..4 character words")
            for ch in word:
                if ord(ch) not in char2syl:
                    fail(f"line {ln}: '{word}' - '{ch}' (U+{ord(ch):04X}) "
                         "is not a level-1 char the single-char engine "
                         "can type")
            if word in seen:
                fail(f"line {ln}: duplicate word '{word}'")
            seen.add(word)
            if explicit is not None:
                # a polyphonic word's dictionary reading
                py = explicit
                if not py or not py.isalpha() or not py.islower():
                    fail(f"line {ln}: '{word}' - bad pinyin '{py}'")
                validate_explicit(word, py, syl_set)
            else:
                # composed from the kernel single-char table
                py = "".join(char2syl[ord(ch)] for ch in word)
            if not py or not py.isalpha() or not py.islower():
                fail(f"line {ln}: '{word}' - bad pinyin '{py}'")
            words.append((py, word))
    # sort by pinyin string for binary search
    words.sort(key=lambda t: t[0])
    if not words:
        fail("no words parsed")

    pool = []
    pool_len = 0
    rows = []
    for py, w in words:
        wb = w.encode("utf-8")
        # NUL-terminate EVERY pool string: the offsets are byte
        # offsets into one flat char array, and phrase_py() returns a
        # C string - without the separators the pool is a single
        # glued run and every lookup walks past its own entry (the
        # live-caught nihao -> 你号 miss: '你好' sat in the table but
        # no boundary ever ended the comparison).
        py_off = pool_len
        pool.append(py)
        pool.append("\x00")
        pool_len += len(py) + 1
        u_off = pool_len
        pool.append(w)
        pool.append("\x00")
        pool_len += len(wb) + 1
        rows.append(f'    {{ {py_off}, {u_off}, {len(wb)}, {len(w)} }},')
    table = "\n".join(rows)
    pool_text = ",".join(f"0x{b:02x}" for b in
                         "".join(pool).encode("utf-8"))

    text_h = (
        "/* SPDX-License-Identifier: Apache-2.0 */\n"
        "/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */\n"
        "/* Auto-generated by tools/gen_pinyin_phrase.py - DO NOT EDIT.\n"
        " * The IME phrase-library interface: struct + externs. */\n"
        "#ifndef OC_PINYIN_PHRASE_H\n"
        "#define OC_PINYIN_PHRASE_H\n"
        '#include "types.h"\n'
        "\n"
        "/* Pooled layout (WP06g2 kernel-window diet): the 0x400000\n"
        " * identity-window link budget is tight, so the table pools\n"
        " * both strings and stores 10-byte rows instead of fixed\n"
        " * py[24]/utf8[12] arrays (~14 KiB saved at 610 words). */\n"
        "struct pinyin_phrase {\n"
        "    u16 py_off;      /* offset into pinyin_phrase_pool */\n"
        "    u16 utf8_off;    /* offset into pinyin_phrase_pool */\n"
        "    u8  bytes;       /* UTF-8 length */\n"
        "    u8  chars;       /* character count */\n"
        "};\n"
        "\n"
        "extern const struct pinyin_phrase PINYIN_PHRASES[];\n"
        "extern const u32 PINYIN_PHRASE_COUNT;\n"
        "extern const char pinyin_phrase_pool[];\n"
        "\n"
        "static inline const char *phrase_py(const struct pinyin_phrase *p) {\n"
        "    return pinyin_phrase_pool + p->py_off;\n"
        "}\n"
        "static inline const char *phrase_utf8(const struct pinyin_phrase *p) {\n"
        "    return pinyin_phrase_pool + p->utf8_off;\n"
        "}\n"
        "\n"
        "#endif /* OC_PINYIN_PHRASE_H */\n"
    )
    text_c = (
        "/* SPDX-License-Identifier: Apache-2.0 */\n"
        "/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */\n"
        "/* Auto-generated by tools/gen_pinyin_phrase.py - DO NOT EDIT.\n"
        " * Source list: tools/ime_phrases.txt (" + str(len(words)) + " words).\n"
        " * Tone-free full-pinyin -> UTF-8 word table (POOLED strings),\n"
        " * sorted by the pinyin string: whole-string matches give the\n"
        " * phrase-first candidates.  Every character is level-1 GB2312\n"
        " * (the single-char engine can type each one). */\n"
        '#include "pinyin_phrase.h"\n'
        "\n"
        f"const u32 PINYIN_PHRASE_COUNT = {len(words)};\n"
        "\n"
        "const char pinyin_phrase_pool[] = {\n"
        + pool_text + "\n"
        "};\n"
        "\n"
        "const struct pinyin_phrase PINYIN_PHRASES[] = {\n"
        + table + "\n"
        "};\n"
    )

    for path, text in ((OUT_H, text_h), (OUT_C, text_c)):
        old = None
        try:
            with open(path, "r", encoding="utf-8") as f:
                old = f.read()
        except OSError:
            pass
        if old == text:
            print(f"gen_pinyin_phrase: {path} unchanged (mtime kept)")
            continue
        with open(path, "w", encoding="utf-8") as f:
            f.write(text)
        print(f"gen_pinyin_phrase: {len(words)} words -> {path}")


if __name__ == "__main__":
    main()
