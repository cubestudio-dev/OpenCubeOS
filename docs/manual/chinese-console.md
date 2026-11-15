<!-- SPDX-License-Identifier: Apache-2.0 -->
# The Chinese console

## Switching

```
ush> lang zh        everything user-facing switches, live:
                    shell help, all 8 wizard apps, service surfaces
ush> lang en        back to English
ush> lang           show the current language
```

The locale is probed per call (not cached at first render), so a
`lang zh` inside a long-running ush affects the very next help line
- including inside the shell that owns the switch.

## What is translated

| Surface | Mechanism |
|---------|-----------|
| ush built-ins | per-app i18n tables (`tr_()`) |
| the 8 merged apps | per-app i18n tables - menus, prompts, verdicts |
| the ~280 classic apps | per-app i18n tables (`_hint` + body strings) |
| L0 service lines (help -w group names, registry titles) | the l0 table (`etc/i18n/l0/{en,zh}.json`) |

A missing translation falls back to English, then to the key itself
- an untranslated app can never crash or print garbage; it shows
the key so the gap is visible and fixable.

## Rendering

The CJK pipeline is always compiled in: the 16x16 GB2312 font, the
GBK/UTF-8 decoder chain, and the console renderer. Both the serial
line and the framebuffer console render Chinese; serial encoding
follows `serial_encoding` (`utf8` default, `gbk` switchable).

## Input - the IME

The IME composes pinyin into GB2312 zones 16-55 (level-1: the 3755
common characters) plus phrase completions:

```
ush> ime             (IME status: level-1 chars, phrases)
ush> ime -i          (interactive compose session)
```

Inside a compose session: type pinyin, pick the candidate, ESC
cancels. The display service exposes the same engine to programs
through L1SVC (IME begin / key / end ops).

## The ASCII contract

Program *names*, file paths and service-level **output lines are
ASCII English** - they are the script-facing contract (wizards and
tests probe them: `"A/B disk: present"`, `"New version available."`).
Chinese is the *human* rendering; scripts and probes keep a stable
English surface in every locale.
