<!-- SPDX-License-Identifier: Apache-2.0 -->
# Release note template

Every GitHub Release note follows this shape (fill every section;
"none" is an acceptable, honest value):

```markdown
# WP-<stage>

<one-paragraph summary: what this work package delivers>

## Changes

- <area>: <change>
- ...

## New interfaces

- <name> (<header>) - <one line>
(or: "None in this release.")

## New shell commands

- `name` - <one line>
(or: "None in this release.")

## Assets (SHA256)

| Asset | Size | SHA256 |
|-------|------|--------|
| opencube-<ver>.iso | <n> | <sha> |
| opencube-<ver>-src.zip | <n> | <sha> |
| opencube-<ver>-update.tar.gz | <n> | <sha> |

## Test evidence

- Build: 0 errors 0 warnings; reproducible-check PASS
- Regression: 28/28
- Watchdog/logging/health E2E: 14/14
- dhtest / cryptotest: PASS
- SSH E2E: 5/5 - HTTPS E2E: 7/7
- BIOS + UEFI boots: banner + 233 commands

## Known issues

- <issue + workaround>
(or: "None known.")
```

The three SHA256 values are computed from the exact bytes uploaded;
the website's download page must show the same three values (the
update_website script guarantees this mechanically).
