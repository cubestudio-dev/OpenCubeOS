<!-- SPDX-License-Identifier: Apache-2.0 -->
# Network

The net surface lives behind `net-tool` (wizard) and the classic
commands underneath it.

## Address setup

```
ush> net-tool               -> "obtain an address via DHCP"
ush> dhcp                   (the classic form - same call)
ush> ip                     (show IP / mask / gateway)
```

Static addressing through the wizard collects address, mask and
gateway one field at a time, so the positional argument line can
never be assembled in the wrong order. Classic form:

```
ush> ip 10.0.2.15 255.255.255.0 10.0.2.2
```

## Name resolution

```
ush> dns example.com              A record (+CNAME when present)
ush> dns example.com aaaa         AAAA / cname / mx / txt / ns / srv
ush> dnstest                      every record type against the resolver
```

DNS goes through the configured resolver; under QEMU user-mode
networking the slirp stack forwards to the real upstream.

## Connectivity tests

```
ush> net-tool               -> ping / wget / ssh / dnstest submenu
ush> ping 10.0.2.2
ush> wget example.com 80 /
ush> wget https.example 443 /           (TLS is built in)
```

`ping` and `wget` ride the kernel NETCMD bridge - the exact syscall
the historical apps used.

## WiFi and Bluetooth

```
ush> net-tool               -> WiFi manager: status / scan / test / sdio
ush> wifi scan              (beacon-derived network list)
ush> net-tool               -> Bluetooth: status / usb / uart / test
ush> bt                     (transport + controller status)
```

The wireless layer reports honest states per family (ath, brcm,
iwlwifi, rtlwifi): registered, scanning, connected - a machine
without the hardware says so instead of pretending.

## Firewall

```
ush> firewall               (list rules + policies)
ush> firewall add ...       (rule management - see the commands page)
ush> firewall ct            (the connection-tracking table)
```

## Routing and ARP

```
ush> route                  (the full routing table)
ush> route add 10.0.0.0 255.0.0.0 10.0.2.2
ush> arp                    (the ARP cache)
```

## SSH in and out

```
ush> sshd                   (serve the shell - port/user/pass optional)
ush> ssh 10.0.2.2           (connect out; credentials as arguments)
```

The SSH stack runs the real KEX/cipher flow; host keys can be
pre-seeded on the /etc volume for unattended boots. Under QEMU,
forward a port with `-netdev user,hostfwd=tcp::2223-:22` and connect
from the host.

## Troubleshooting

- **No IP after DHCP** - check the NIC model (`hwinfo` -> PCI) and
  that the `-netdev user` exists; the e1000 family is the best-tested.
- **DNS times out** - the slirp DNS forward only works with user-mode
  networking; on a bridged tap you need a real resolver.
- **First SSH connect stalls** - the ARP cache is cold after DHCP;
  run one client-direction command first (`ping` the peer) to warm
  it, then connect.

More: [../troubleshooting/network-issues.md](../troubleshooting/network-issues.md).
