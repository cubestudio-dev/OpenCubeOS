<!-- SPDX-License-Identifier: Apache-2.0 -->
# 发布说明模板

每个 GitHub 发布说明都遵循以下格式（请填写每个部分；
"none"（无）是可接受的、诚实的值）：

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

三个 SHA256 值是根据上传的确切字节计算的；
网站的下载页面必须显示相同的三个值（update_website 脚本会机械地保证这一点）。
