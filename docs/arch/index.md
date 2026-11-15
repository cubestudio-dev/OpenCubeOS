<!-- SPDX-License-Identifier: Apache-2.0 -->
# Architecture deep dives

| Page | What it covers |
|------|----------------|
| [system-map](system-map.md) | the four components (OCBL/OCSE/OCOS/OCIRE), version chain, physical memory contract, boot modes, A/B |
| [tree-map](tree-map.md) | every directory of the tree: what is inside and why it exists |
| [hidden-mechanisms](hidden-mechanisms.md) | the twelve load-bearing behaviors that span files and components |
| [private-headers](private-headers.md) | the `ocos/interface/` ring-3 API contract, header by header |
| [boot-flow](boot-flow.md) | OCBL stage1 -> core -> scanner -> menu -> mb2 handoff -> kmain |
| [memory](memory.md) | PMM, VMM, heap, growth-by-fault, DMA bounds, the identity window |
| [scheduler](scheduler.md) | task_t, priorities, round-robin, the context switch |
| [interrupts](interrupts.md) | IDT/GDT/TSS, IST discipline, PIC/LAPIC/MSI, the exception path |
| [syscall-ring3](syscall-ring3.md) | int 0x80 ABI, the 54-syscall table, SYS_L1SVC, kcap |
| [vfs](vfs.md) | the VFS contract + each filesystem's real behaviour |
| [network-stack](network-stack.md) | layer by layer, NIC polling to CUBIC/SACK, TLS and SSH |
| [driver-model](driver-model.md) | OC_DRIVER_DECL registration, family ops, probe order |
| [tui-engine](tui-engine.md) | the desktop, the pages, and why they bypass kcap |
| [chinese-layer](chinese-layer.md) | encoding, font generation, the system IME session protocol |
| [crash-recovery](crash-recovery.md) | panic path, OCDUMP01, crashlog ring, watchdogs, the OCIRE takeover |
| [ota-ab](ota-ab.md) | slots, flags, the update chain, the 8-step install, rollback |
