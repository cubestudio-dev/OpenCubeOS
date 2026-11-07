<!-- SPDX-License-Identifier: Apache-2.0 -->
# Architecture deep dives

| Page | What it covers |
|------|----------------|
| [boot-flow](boot-flow.md) | GRUB -> boot.S -> mb2 -> kmain, the two-stage link |
| [memory](memory.md) | PMM, VMM, heap, growth-by-fault, DMA bounds |
| [scheduler](scheduler.md) | the model, ticks, policy watchdog, signals |
| [interrupts](interrupts.md) | IDT/IST, PIC/LAPIC, the exception path |
| [syscall-ring3](syscall-ring3.md) | ABI, ELF loading, processes, ld.so |
| [vfs](vfs.md) | the VFS contract + each filesystem's real behaviour |
| [network-stack](network-stack.md) | layer by layer, NIC drivers to TLS/SSH |
| [driver-model](driver-model.md) | registration surfaces, PCI/USB cores |
| [tui-engine](tui-engine.md) | windows, widgets, desktop, the bridge |
| [chinese-layer](chinese-layer.md) | font/encoding/IME pieces, both worlds |
| [crash-recovery](crash-recovery.md) | panic path, IST rationale, posture |
| [ota-ab](ota-ab.md) | slots, flags, the update chain |
