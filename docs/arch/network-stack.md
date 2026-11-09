<!-- SPDX-License-Identifier: Apache-2.0 -->
# The network stack

Bottom-up, one layer per file under `net/`:

## NIC drivers (`drivers/nic/`)

e1000, e1000e, igb, ixgbe (Intel families); rtl8139 and the
rtl8169-family (8168/8125/810x); BCM57xx. Each registers through the
L1 driver_nic interface (`driver_nic_register` / send / recv /
link_status / get_mac). DMA descriptors, interrupt-driven RX where
the card supports it, and per-card quirk handling live in the
driver files - bring-up notes per family are under
[../drivers/](../drivers/).

## Ethernet + ARP

Frame RX dispatch by ethertype; the ARP cache with expiry; `arp`
command.

## IPv4 / ICMP / UDP

Fragment handling, ICMP echo (the `ping` command), UDP sockets -
DNS rides on UDP with the resolver configured through the network
setup.

## TCP (`net/net_core.c` + `net_tcp_cc.c`)

A full state machine with congestion control. The RX checksum chain
was a famous bug (BUG-0072: chained internet_checksum() calls
complemented twice - every inbound segment was refused; fixed with
checksum_raw_add + one fold across pseudo-header and segment).
`tcpstats`, `tcpcc_test`, `tcptest` are the observers/tests.

## Sockets for user programs

The socket syscall family (53-syscall surface) backs `run select_test`
style multiplexing and the network user programs.

## TLS 1.2 / 1.3 (`net/net_tls.c`)

RSA and ECDHE key exchange, X.509 chain verification with the RFC
6125 hostname policy (IP-literal URLs have no reference identity -
the mid-handshake disconnect bug was exactly here, BUG-fixed with
net_tls_host_is_ip_literal), and the WP-09 trust model: structural
bounds always fatal; an un-anchored chain warns and continues;
hostname mismatch is fatal only for user-supplied hostnames.

## SSH client + server (`net_ssh.c`, `net_sshd.c`)

Key exchange, host keys on /etc, authorized_keys, full session
round-trip - the E2E suite logs in with paramiko from the host.

## Entropy

HMAC_DRBG (SP 800-90A) seeded from RDRAND + RDTSC, re-mixed every
generate - see [internals/drbg-crypto.md](../internals/drbg-crypto.md).
