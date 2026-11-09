<!-- SPDX-License-Identifier: Apache-2.0 -->
# Networking

A self-contained TCP/IP stack with everything from NIC drivers to an
SSH server - no ported libc, no third-party code.

## Bring-up

```
oc> dhcp                  (request an address on nic0)
oc> ping 10.0.2.2         (ICMP echo to the QEMU gateway)
oc> dns example.com       (resolve via the QEMU DNS forwarder)
oc> wget 10.0.2.2         (fetch http://10.0.2.2/)
oc> wget host 8080 /path  (explicit port and path)
oc> route                 (routing table)
oc> tcpstats              (TCP counters + congestion state)
oc> firewall              (stateful firewall rules)
```

In QEMU the default `-device e1000` matches the e1000 driver
out of the box:

```
qemu-system-x86_64 -m 512 -cdrom opencube.iso \
    -netdev user,id=n1 -device e1000,netdev=n1
```

## Supported NICs

| Family | Cards | Driver |
|--------|-------|--------|
| Intel e1000 | e1000 (8254x) | `drivers/nic/driver_nic.c` |
| Intel e1000e | 8257x ICH/PCH NICs | `driver_nic_e1000e.c` |
| Intel igb | 82575/82576 (VFs included) | `driver_nic_igb.c` |
| Intel ixgbe | 82598/82599 10 GbE | `driver_nic_ixgbe.c` |
| Realtek rtl8139 | 8139/810x fast ethernet | `driver_nic_rtl8139.c` |
| Realtek rtl8169 | 8168/8125/810x gigabit families | `driver_nic_rtl8169.c` |
| Broadcom BCM57xx | 5708/5709/5714 era | `driver_nic_bcm57xx.c` |

## The stack

ARP, IPv4, ICMP, UDP, DNS, TCP with congestion control
(`net_tcp_cc.c`), a socket API for user programs, and a stateful
firewall. Every layer is visible through commands: `arp`,
`route`, `tcpstats`, `firewall`, `nicstat`.

## HTTPS client (TLS 1.2 / 1.3)

`wget https://...` negotiates TLS with RSA and ECDHE key exchange,
verifies X.509 chains (RFC 6125 hostname policy - IP-literal URLs
have no reference identity), and rejects tampered certificates. The
crypto core (AES, SHA-2 family, HMAC, RSA, NIST curves, Ed25519-style
primitives) is the same code `cryptotest` validates with NIST vectors.

DH key exchange correctness and timing: `dhtest`.

## SSH: client and server

- **Client** (`ssh` command): connect, authenticate, run a remote
  command - key exchange via curve25519-style primitives.
- **Server** (`sshd`): a real SSH server *inside the guest*. Host and
  client keys live on the /etc volume (`host_keygen_etc` seeds them);
  authorized keys are managed there too. From your host:

  ```
  ssh -p <port> root@localhost     (forward a QEMU port to try it)
  ```

  The E2E suite exercises a full paramiko login round-trip against
  the in-guest sshd.

## DNS

`dns <name>` resolves through the configured resolver (QEMU's
user-mode stack forwards to the host). Cached entries are visible
through the network stats commands.

## What is not there (honest list)

- IPv6 is not implemented.
- Wi-Fi association: the wireless stack (`wifi` command) covers
  status/scan/test across the ath/brcm/iwlwifi/rtlwifi families and
  the SDIO bus - full association on real hardware depends on the
  card family; in QEMU there is no Wi-Fi device model to attach.
- NAT traversal/port forwarding are QEMU user-mode properties, not
  kernel features.
