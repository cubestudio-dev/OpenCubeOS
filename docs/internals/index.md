<!-- SPDX-License-Identifier: Apache-2.0 -->
# Subsystem internals

The deep dives behind the developer handbook
([../dev/index.md](../dev/index.md)) - mechanisms, verbatim
structures, and the trigger -> processing -> impact contracts of
each subsystem.

| Page | What it covers |
|------|----------------|
| [klog](klog.md) | the 1024-entry ring, 5 levels, 4 channels, emit hook, /var/log/kernel.log + 1 MiB rotation x3, dmesg/logcat/logctl |
| [crash-dump](crash-dump.md) | the panic sequence, OCDUMP01 format, the 22-u64 register snapshot, the 32-slot crashlog ring, IST discipline, the boot-state relay |
| [health](health.md) | the 1 s five-dimension sampler, 3 deduplicated alarm bits, the alarm-to-watchdog feed |
| [watchdog](watchdog.md) | the 15 s soft watchdog, the 8 policy slots, WDAT + Intel TCO, the 6300ESB boot-state relay, the dual-arm rule |
| [drbg-crypto](drbg-crypto.md) | HMAC_DRBG (SP 800-90A) seeding, the primitive families, the embedded root CA store, cryptotest/dhtest |
| [ota-ab](ota-ab.md) | the A/B slot layout, boot-flag semantics, the 16-op table, the install/rollback/verify chain |
| [ocembed-guard](ocembed-guard.md) | the OCIRE embed: mailbox state machine, log ring, guard process, whole-disk write protection, the injected log agent |

Related deep pages under `docs/dev/`:
[data-structures.md](../dev/data-structures.md) (the verbatim
structure collection),
[call-chains.md](../dev/call-chains.md) (six path dissections),
[interface-dictionary.md](../dev/interface-dictionary.md) (all
208 L1SVC ops). Architecture overview:
[../root/ARCHITECTURE.md](../root/ARCHITECTURE.md).
