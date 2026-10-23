<!-- SPDX-License-Identifier: Apache-2.0 -->
# Coding style

The tree is self-consistent; match it.

## C

- C11 (`-std=gnu11`), 4-space indent, K&R braces, no tabs in .c/.h.
- Types: the kernel's fixed-width set (`u8/u16/u32/u64/s8/...`,
    `types.h`) - not uint8_t spellings.
- One declaration per line where the type matters; initialize what
  you can.
- Every file opens with the SPDX + copyright comment block and a
  Purpose paragraph that says WHY the file exists. Future readers
  (and the docs generator) rely on it.
- Function comments: contract, arguments, return codes - especially
  every negative return.
- Error handling: check, report with a clear message, and unwind -
  never leak a half-initialized device registration.

## Assembly

- `.S` files (preprocessed): boot.S and the arch trampolines are the
  reference. Comment the ABI at every entry point.

## Shell commands

- Register with `shell_register_command_ex` + the work-package tag so
  `help -w` stays truthful.
- One-line help, `usage:` printout on argument errors, exit non-zero
  on failure.
- ASCII only.

## Naming

- Files: `kernel/<area>/core_<thing>.c`, `drivers/<bus>/driver_<hw>.c`,
  `shell/shell_cmds_<domain>.c`, `fs/fs_<fs>.c` - the tree's layout
  IS the taxonomy (the docs generator groups by it).
- Symbols: `<module>_<verb>_<object>` (e.g. `driver_block_flush`,
  `klog_write`, `ota_ab_install`). Static helpers stay file-local.
- All-caps constants with a module prefix (`OC_MB2_TAG_END`,
  `SHELL_MAX_COMMANDS`).

## Limits are part of the design

When you add a bounded table, the limit constant carries a comment
explaining the number (see `SHELL_MAX_COMMANDS` in shell.h - the
history of 232->256 is documented at the definition). Keep that
discipline: the next person tuning it needs the story.

## What not to do

- No dead code "for later" - delete it; git remembers.
- No silent truncation - refuse with a message (alias values are the
  model: stored in full or refused).
- No `// TODO` without a BUG- number or a work-package reference.
- No new third-party code: the tree is original, Apache 2.0, and
  stays that way.
