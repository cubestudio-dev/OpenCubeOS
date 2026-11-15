<!-- SPDX-License-Identifier: Apache-2.0 -->
# Network troubleshooting

## Ordered triage

Run `net-tool` and walk the menu, or use the classic spellings:

```
ush> hwinfo                 (wizard: PCI page - is the NIC on the bus?)
ush> ip                     (address bound? 0.0.0.0 = none)
ush> dhcp                   (address + gateway + DNS in one shot)
ush> ping 10.0.2.2          (ICMP to the QEMU gateway)
ush> arp                    (L2 resolution happened?)
ush> dns example.com        (UDP + resolver OK?)
ush> wget example.com 80 /  (full TCP + HTTP round trip)
ush> tcpstats               (per-connection counters)
```

Each step isolates a layer: bus -> driver -> L2 -> L3 -> L4 ->
application. The `update` wizard runs the first steps of exactly
this ladder (IP -> DHCP fix -> DNS) before it ever checks for
updates.

## QEMU specifics

- User-mode networking always provides 10.0.2.2 (gateway) and DNS
  forwarding - no host configuration needed.
- The default `-device e1000` is the best-tested path.
- Two-instance tests: shared `-netdev socket/multicast` gives a real
  two-node link for the NIC tests.
- **First connect to sshd stalls**: the ARP cache is cold after
  DHCP - run one client-direction command first (`ping` the peer)
  to warm it.

## TLS/SSH specifics

- Certificate failures print the exact verification stage
  (chain/hostname/policy) - read the message; the trust model
  (un-anchored warns, mismatch is fatal for named hosts) is in
  [arch/network-stack.md](../arch/network-stack.md).
- sshd host keys: generate on first boot, or pre-seed
  `ssh_host_key` on the /etc volume (`tools/host_keygen_etc.py`,
  or the mtools recipe in [../dev/e2e-suites.md](../dev/e2e-suites.md))
  - the E2E suite's prerequisite.
