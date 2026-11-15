<!-- SPDX-License-Identifier: Apache-2.0 -->
# 发布说明模板

每份 GitHub Release 说明都遵循这个形状（填满每一节；
"none" 是一个可接受的、诚实的取值）：

```markdown
# v<version>

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
- BIOS + UEFI boots: banner + ring-3 ush> + boot complete

## Known issues

- <issue + workaround>
(or: "None known.")
```

三个 SHA256 值从上传的确切字节计算得出；网站的下载页必须
显示同样的三个值（update_website 脚本在机制上保证这一
点）。
