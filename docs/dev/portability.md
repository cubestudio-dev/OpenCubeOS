<!-- SPDX-License-Identifier: Apache-2.0 -->
# Portability notes for contributors

The tree builds on GCC 14.x locally and on newer toolchains on CI.
Three classes of difference have already bitten - keep them in mind:

## 1. Stricter warning classes on newer GCC

`-Waddress-of-packed-member` (part of `-Wall` since GCC 9, enforced
harder on newer releases): casting a packed-struct pointer to a
naturally-aligned type is an error under `-Werror`. The multiboot2
tag types are ALL declared `__attribute__((packed))` for exactly this
reason - including the mmap tag (4x u32, no layout change) and the
BASIC_MEMINFO reader which goes through a packed tag type instead of
a bare `u32*` cast.

Rule: when you cast a pointer that came from packed memory, target a
packed type.

## 2. Stricter assemblers (binutils/GAS)

`setcc`/`setc` accept BYTE registers only. The rdrand wrapper
declared its flag as `u32` with an `"=q"` constraint - older GAS
silently truncated to the byte subregister, newer GAS rejects the
operand size. It is `u8` now.

Rule: setcc-style flags are `u8`, constraint `"=q"`.

## 3. Toolchain layout assumptions

`OC_TOOLS` may point at a relocatable tree (`<root>/usr/{bin,lib}`)
OR a system root (`/usr` with `{bin,lib}` directly under it). The
embed_grub generator probes both layouts - do not assume either one.

## Compile-flag contract

The kernel flags are fixed in the Makefile:
`-ffreestanding -fno-stack-protector -fno-pie -fno-pic -mno-red-zone
-mno-sse -mno-mmx -mno-3dnow -mcmodel=kernel
-fno-asynchronous-unwind-tables -Wall -Wextra -Werror -O2 -g
-std=gnu11`. New flags belong in the Makefile (one place), not
scattered in files.

## Cross builds

`make ARCH=arm64` / `ARCH=riscv` use the aarch64/riscv64 cross
toolchains and their own linker rules (`--hash-style=sysv
--build-id=none`; `-n` and PHDR layouts do not mix). The bring-up
shells are real (PL011/NS16550A divisors, timer counters, heartbeat)
but deliberately minimal - x86_64 is the feature-complete
architecture.
