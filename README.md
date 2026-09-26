# Open Cube OS - WP-08b Batch 6 (Audit P0/P1 Fixed)


Copyright 2026 cubestudio-dev <cubestudio@qq.com>
Licensed under the Apache License, Version 2.0.
**Open Cube OS** is an open-source operating-system kernel. Its positioning:

- **It is not "a system you can use daily".** It is "a kernel that can be extended into anything".
- Its value is not what it ships with, but the interfaces it exposes to upper layers.
- Architecture is two-tier:
  - **L0** = Open Cube OS = the complete kernel.
  - **L1** = upper-layer extensions, built on L0's extension interfaces. L0 ships without any L1.
- L0 is licensed Apache 2.0.
- Design principle: "everything is extensible".

## WP-01 (done) - Boot + framebuffer + text rendering

- BIOS + UEFI dual boot via GRUB multiboot2.
- 64-bit long mode, 4 GiB identity mapping.
- 800x600x32 RGB framebuffer.
- 8x16 bitmap font, char-grid console with cursor + scroll.
- Four L0->L1 extension interfaces (fb access, renderer swap, font engine, console hook).

## WP-02 (done) - Interrupts + timer + keyboard

- IDT + GDT + TSS: 256-entry IDT, IST stacks for #DF/#MC.
- 8259 PIC remap: IRQ0-15 -> vectors 32-47.
- CPU exception handlers: #DE/#UD/#PF/#GP/#DF with diagnostic dump + L1 handler chain.
- PIT @ 100 Hz: real system tick counter, real millisecond timestamps.
- PS/2 keyboard + COM1 serial input -> unified keyboard queue.
- Console input line editor + interactive `oc>` prompt.
- Four new L0->L1 extension interfaces (IRQ, timer, keyboard, exception).

## WP-03 (done) - Physical memory + virtual memory + kernel heap

WP-03 adds:

- **PMM**: bitmap allocator for 4KB page frames, parses multiboot2 mmap, reserves kernel/framebuffer/mbi regions, emergency callback support.
- **VMM**: 4-level page tables, create/destroy address spaces, map/unmap/protect pages, real page fault handler (stack growth, heap growth, illegal detection).
- **Heap**: free-list allocator with first-fit + coalescing + double-free detection, kmalloc/kzalloc/kfree/krealloc.
- **Shell**: 13 registered commands (mem/heap/vmmap/vmtest/memtest/frag + WP-02 ones), L1 command registration API.
- **Four new L0->L1 extension interfaces**: PMM, VMM, heap, shell command registration.
- **em dash fix**: all 36 source files scanned, all non-ASCII replaced with ASCII.

## Repository layout

```
oc-os/
+-- boot/                       # Assembly boot stubs (WP-01)
|   +-- multiboot2_header.S
|   +-- boot.S
|   +-- long_mode_init.S
+-- kernel/                     # C kernel
|   +-- types.h, string.{c,h}, multiboot2.{c,h}    # WP-01 base
|   +-- fb.{c,h}, font.{c,h}, font_data.c          # WP-01 framebuffer + font
|   +-- console.{c,h}, ext.{c,h}, ext_selftest.c   # WP-01 console + extensions
|   +-- log.{c,h}                                   # WP-02: real timestamps
|   +-- idt.{c,h}, idt_stub.S, idt_load.S          # WP-02: IDT/GDT/TSS
|   +-- pic.h                                       # WP-02: 8259 PIC
|   +-- exceptions.{c,h}                           # WP-02: CPU exceptions
|   +-- irq.{c,h}                                   # WP-02: IRQ dispatch
|   +-- timer.{c,h}                                 # WP-02: PIT + soft timers
|   +-- keyboard.{c,h}                              # WP-02: PS/2 keyboard
|   +-- serial_in.{c,h}                             # WP-02: COM1 RX
|   +-- console_in.{c,h}                            # WP-02: line editor
|   +-- pmm.{c,h}                                   # WP-03: physical memory manager
|   +-- vmm.{c,h}                                   # WP-03: virtual memory manager
|   +-- heap.{c,h}                                  # WP-03: kernel heap
|   +-- shell.{c,h}                                 # WP-03: shell command registration
|   +-- ext_wp2.h, ext_wp3.h                       # extension API summaries
|   +-- kmain.c                                     # Kernel main
+-- tools/
|   +-- gen_font.py, build_iso.sh, qemu_shot_vnc.py
|   +-- wp02_test.py, wp03_test.py                 # interactive test scripts
+-- docs/
|   +-- BUILD.md, EXTENSIONS.md, EXTENSIONS_WP02.md, EXTENSIONS_WP03.md, STATUS.md
+-- releases/                                        # version archives (WP01/WP02/WP03)
+-- linker.ld, grub.cfg, Makefile, LICENSE
+-- README.md
```

## Quick start

```sh
make iso
make run-bios     # or: make run-uefi
```

Once the `oc>` prompt appears, type `help` for commands. Try `mem`, `heap`,
`vmmap`, `vmtest`, `memtest`, `frag` for the WP-03 memory management features.

## License

Apache 2.0. See `LICENSE`.

## AI Disclosure

This project was developed by cubestudio-dev with the assistance
of AI tools. All design decisions, architecture, specifications,
project management, code review, quality assurance, and acceptance
testing were performed by cubestudio-dev. AI tools were used as
implementation assistants.

## Copyright

Copyright 2026 cubestudio-dev <cubestudio@qq.com>.
