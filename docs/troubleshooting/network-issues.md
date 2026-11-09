<!-- SPDX-License-Identifier: Apache-2.0 -->
# Network troubleshooting

## Ordered triage

```
oc> lspci              (NIC on the bus?)
oc> ifconfig           (driver bound? MAC present?)
oc> nicstat            (counters moving?)
oc> dhcp               (address + gateway + DNS)
oc> ping 10.0.2.2      (ICMP to the QEMU gateway)
oc> arp                (L2 resolution happened?)
oc> dns example.com    (UDP + resolver OK?)
oc> wget 10.0.2.2      (full TCP + HTTP round trip)
```

Each step isolates a layer: bus -> driver -> L2 -> L3 -> L4 ->
application.

## QEMU specifics

- User-mode networking always provides 10.0.2.2 (gateway) and DNS
  forwarding - no host configuration needed.
- The default `-device e1000` is the best-tested path.
- Two-instance tests: shared `-netdev socket/multicast` gives a real
  two-node link for the NIC tests.

## TLS/SSH specifics

- Certificate failures print the exact verification stage
  (chain/hostname/policy) - read the message; the trust model
  (un-anchored warns, mismatch is fatal for named hosts) is in
  [arch/network-stack.md](../arch/network-stack.md).
- sshd needs the /etc volume seeded (`host_keygen_etc`) - the E2E
  suite's prerequisite.
