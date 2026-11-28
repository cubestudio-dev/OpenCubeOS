<!-- SPDX-License-Identifier: Apache-2.0 -->
# The wizard library (l1_wizard_api.h)

The shared primitives behind the 8 merged apps. Header-only in
`ocos/interface/l1_wizard_api.h` - every app compiles its own copy
(the `_shared` style); the verbatim passthrough paths never touch
it (they keep the historical `l1svc_main()` semantics).

## Interaction contract

- **Output buffer**: the caller owns a `WIZ_OUT_CAP` (32768)
  **static** buffer - BSS, not stack (the l1svc_main note: 32 KiB
  on the user stack is a risk, BSS is mapped per-exec).
- **Line input**: `SYS_READLINE` - kernel line editor, echo +
  backspace, blocking, printable ASCII only (no Ctrl chars: never
  promise "Ctrl+C to quit" on a READLINE prompt).
- **Single-key picks**: `SYS_GETCH` non-blocking poll (the ush
  spin-wait pattern). Digits 1..9 select, `q`/`Q`/ESC abort,
  out-of-range digits are ignored.
- **Strings**: every human-visible line renders through `tr_()` -
  the whole wizard follows `lang en|zh` live.

## The API

| Primitive | What it does |
|-----------|--------------|
| `wiz_svc(cat, op, arg, out)` | run one L1SVC op, print its text; on failure prints `tr_("wiz_svc_failed")`; returns the raw rc |
| `wiz_svc_quiet(cat, op, arg, out)` | same call, no print - the caller probes `out` |
| `wiz_find(hay, needle)` | substring probe (1/0) - the anchor test |
| `wiz_token_after(hay, key, dst, cap)` | copy the first token after `key` (`"IP: 10.0.2.15"` + `"IP:"` -> `"10.0.2.15"`); NULL when the key is absent |
| `wiz_ask(prompt, buf, cap)` | prompt + one READLINE line (always NUL-terminated; empty input = "") |
| `wiz_confirm(what)` | one-shot y/N - ONLY a bare `y`/`Y` answers yes |
| `wiz_pause()` | `tr_("wiz_pause")` + ENTER |
| `wiz_menu(title, items, n)` | numbered single-key menu over tr_()-able KEYS (1..9); returns the 1-based choice or 0 on q/ESC |
| `wiz_banner(title)` / `wiz_step(n, total, what)` | framing lines |
| `wiz_ok(msg)` / `wiz_fail(msg)` | `[OK]` / `[!!]` verdict lines |

## The anchor discipline

Wizards read the SERVICE's own English output lines - the same
lines scripts parse - never a second "structured" spelling:

```c
wiz_svc_quiet(L1SVC_CAT_OTA, L1SVC_OTA_UPDATE, "status", out);
if (wiz_find(out, "A/B disk: present")) { ... }
```

The anchors are part of the compatibility surface (see
[surface-map.md](surface-map.md)): renaming them breaks the
wizards, the E2E suites and the user docs at once.

## The safety model

- Destructive ops (`mkfs.*`, `abdisk`, `install`, `rm`, the power
  actions) are **never one keystroke**: ask for the target name,
  make the user RETYPE it, then a bare `y` - three deliberate
  steps. The classic/direct doors stay verbatim (scripts keep
  their no-prompt behavior).
- Number fields (volume, sleep seconds, chmod mode) are validated
  digits-only before they ever reach a service.
- Cross-domain repair is the point: the `update` wizard fixes a
  missing IP through DHCP (net service) and a missing A/B disk
  through lsblk + abdisk (block service) before checking for
  updates.

## The shared i18n keys

Every mega-app's resource carries: `wiz_title`, `wiz_select`,
`wiz_quit`, `wiz_pause`, `wiz_svc_failed` (+ its own keys). A
missing key falls back to English, then to the key string - the
wizard never crashes on a translation gap.
