# Old Tests

## Contents
- dyn_test.elf (13,496 bytes)

## Why archived
dyn_test.elf was an old copy of so_test.elf (identical 13,496 bytes).
In kmain.c, `dyn_test` is now an alias for `so_test` (both point to the
same embedded byte array userprog_so_test). The standalone dyn_test.elf
file was redundant.

## SHA256
Run `sha256sum dyn_test.elf` to verify.

## Archived date
2026-09-25
