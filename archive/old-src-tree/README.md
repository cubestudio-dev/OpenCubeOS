# Old Source Tree (WP-08a era)

This is the root-level source tree from the handoff zip extraction. It was
the original copy of the kernel source that was extracted when the handoff
package was unpacked. It has been superseded by the src-WP08a/ content
(which is now at the repository root).

## Why archived

The root-level source tree was a duplicate of src-WP08a/ (which was the
active working directory). After reorganization, src-WP08a/'s content was
moved to the repository root, making this old copy redundant.

## Unique content

This old tree has 2 files that are NOT in the current active source:

- `kernel/dynlink.c` (19,970 bytes) — WP-08b WIP "In-kernel dynamic linker"
- `kernel/dynlink.h` (5,755 bytes) — header for the above

These were the original WP-08b approach (dynamic linking inside the kernel).
They were replaced by the user-space ld.so approach (userprogs/ld_so.c).
Kept here for historical reference.

## SHA256 Checksums

Key files:
- kernel/dynlink.c: see file
- kernel/dynlink.h: see file

All other files are identical to the current active source tree (verified
2026-09-25 via SHA256 comparison).

## Archived date

2026-09-25
