<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 cubestudio-dev <cubestudio@qq.com> -->

# Open Cube OS — WP-06 Extension Interfaces

**WP-06 adds 5 new extension points (items 22-26). Total L1 surface is now 26.**

WP-06 brings networking to Open Cube OS: a PCI bus driver, an e1000 / virtio-net NIC driver, an Ethernet/ARP/IP/ICMP/UDP/TCP stack, a DHCP client, a DNS client, a BSD-style socket API, and a set of network shell commands. The whole stack runs in polling mode — `net_poll()` must be called periodically (every 10 ms by default via the WP-02 timer API).

All WP-01..WP-05 interfaces remain unchanged. WP-06 interfaces are additive.

WP-06 also uses the WP-03 `shell_register_command()` API to register nine network shell commands (`ifconfig`, `ip`, `route`, `ping`, `netstat`, `dhcp`, `dns`, `lspci`, `wget`). That API is documented in `EXTENSIONS_WP03.md` section 14.

---

## Interface 22: Network init, poll, and configuration

### Signature

```c
#include "net.h"

void net_init(void);
void net_poll(void);
void net_start_timer(void);

u32  net_get_ip(void);
u32  net_get_mask(void);
u32  net_get_gateway(void);
u32  net_get_dns(void);
void net_set_ip(u32 ip, u32 mask, u32 gateway);
void net_set_dns(u32 dns);
void net_get_mac(u8 mac[6]);
int  net_get_link_status(void);

void net_get_stats(net_stats_t *out);
```

### Purpose

`net_init()` is the single entry point that brings up the whole WP-06 network stack. It:

1. Initialises the PCI bus driver (`pci_init()`).
2. Initialises ARP, UDP, and TCP state tables.
3. Probes for a NIC, trying virtio-net first (best in QEMU) and falling back to e1000.
4. If a NIC is found, sets default IP `10.0.2.15`, mask `255.255.255.0`, gateway `10.0.2.2`, DNS `10.0.2.3` (QEMU's user-mode networking defaults).

After `net_init()` returns, the NIC is up but the stack is idle — nothing is processed until `net_poll()` is called. `net_poll()` reads up to 8 pending RX frames from the NIC and dispatches each through the Ethernet → ARP / IP → ICMP / UDP / TCP pipeline. `net_start_timer()` registers a periodic timer (via the WP-02 `oc_timer_register_periodic()` API) that calls `net_poll()` every 10 ms, so application code does not need to poll manually.

The configuration getters/setters expose the current IPv4 configuration. All IPv4 addresses are 32-bit host-order values; use the `IP4(a,b,c,d)` macro to build them and the byte-order helpers (`htonl`, `ntohl`, `htons`, `ntohs`) when laying them into packet headers.

### Parameters

| Function | Description |
|---|---|
| `net_init()` | Bring up the network stack. Probes for a NIC and configures defaults. No return value. Called once at boot. |
| `net_poll()` | Process up to 8 pending RX frames. Call periodically (every 10 ms). No return value. Safe to call from a timer IRQ callback. |
| `net_start_timer()` | Register a periodic timer that calls `net_poll()` every 10 ms. Uses `oc_timer_register_periodic()` from `EXTENSIONS_WP02.md` section 6. |
| `net_get_ip()` / `net_get_mask()` / `net_get_gateway()` / `net_get_dns()` | Return the current IPv4 address / netmask / default gateway / DNS server as a host-order `u32`. Returns 0 if no NIC or no configuration. |
| `net_set_ip(ip, mask, gateway)` | Set the IPv4 address, netmask, and default gateway (host-order `u32`s). No return value. |
| `net_set_dns(dns)` | Set the DNS resolver address. |
| `net_get_mac(mac)` | Copy the NIC's 6-byte Ethernet MAC into `mac[6]`. Fills zeros if no NIC. |
| `net_get_link_status()` | Return 1 if the link is up, 0 otherwise. virtio-net is always up after init; e1000 reads the LU bit of `E1000_STATUS`. |
| `net_get_stats(out)` | Fill `*out` with cumulative counters (see struct below). |

### `net_stats_t`

```c
typedef struct {
    u64 tx_packets;
    u64 rx_packets;
    u64 tx_bytes;
    u64 rx_bytes;
    u64 arp_requests;
    u64 arp_replies;
    u64 icmp_echo_sent;
    u64 icmp_echo_recv;
    u64 tcp_connections;
} net_stats_t;
```

### Return value

- `net_init`, `net_poll`, `net_start_timer`, `net_set_ip`, `net_set_dns`, `net_get_mac`, `net_get_stats`: no return value.
- `net_get_ip` / `net_get_mask` / `net_get_gateway` / `net_get_dns`: host-order `u32` (0 if unset).
- `net_get_link_status`: 1 = link up, 0 = link down or no NIC.

### Example

```c
#include "net.h"

void net_demo(void) {
    net_init();           /* probe NIC, configure defaults */
    net_start_timer();    /* net_poll() every 10 ms from now on */

    /* Override the auto-configured address. */
    net_set_ip(IP4(192, 168, 1, 50),
               IP4(255, 255, 255, 0),
               IP4(192, 168, 1, 1));
    net_set_dns(IP4(8, 8, 8, 8));

    /* Inspect. */
    u8 mac[6];
    net_get_mac(mac);
    /* mac[0..5] are the NIC's Ethernet address */

    if (!net_get_link_status()) return;   /* cable unplugged? */

    net_stats_t st;
    net_get_stats(&st);
    /* st.tx_packets, st.rx_bytes, ... */
}
```

### Caveats

- **Polling, no IRQ (yet)**: the NIC RX path is polled. If `net_poll()` is not called for more than a few hundred milliseconds, frames can be dropped because the NIC ring overflows. Always call `net_start_timer()` (or roll your own periodic loop).
- **Single NIC**: only one NIC is initialised. The virtio-net probe runs first; if it succeeds, e1000 is never tried. To force e1000, disable virtio-net in the build.
- **No routing table**: only the default gateway is honoured. There is no per-destination routing table (the `route` shell command always shows just the default route).
- **DHCP overrides manual config**: if `dhcp_discover()` succeeds, it overwrites `ip` / `mask` / `gateway` / `dns` with the values the DHCP server handed out.

---

## Interface 23: Socket API

### Signature

```c
#include "net.h"

#define SOCK_TCP 1
#define SOCK_UDP 2

int net_socket (int type);
int net_bind   (int fd, u32 ip, u16 port);
int net_connect(int fd, u32 ip, u16 port);
int net_send   (int fd, const void *data, int len);
int net_recv   (int fd, void *buf, int len);
int net_close  (int fd);

/* Auxiliary lower-level helpers (documented for completeness). */
int tcp_connect(u32 dst_ip, u16 dst_port);
int tcp_send   (int sock, const void *data, int len);
int tcp_close  (int sock);
int tcp_listen (u16 port, tcp_handler_fn handler);
int udp_bind   (u16 port, udp_handler_fn handler);
int udp_send   (u32 dst_ip, u16 dst_port, u16 src_port, const void *data, int len);
```

### Purpose

The socket API is the recommended L1 way to do networking. It mirrors the BSD socket API enough that example code is portable in spirit, while staying small enough to fit the Open Cube OS kernel. Two socket types are supported:

- `SOCK_TCP` — reliable, connection-oriented. `net_connect()` performs the TCP 3-way handshake; `net_send()` / `net_recv()` transfer data; `net_close()` performs the 4-way teardown (FIN/ACK).
- `SOCK_UDP` — datagram, connectionless. `net_bind()` sets the local source port; `net_send()` transmits one datagram; `net_recv()` blocks (polling) until a datagram arrives or a 5-second timeout fires.

The lower-level `tcp_*` and `udp_*` helpers are exposed for code that needs finer control (raw TCP sockets, custom demuxing), but most L1 code should use the `net_*` socket API.

Sockets are integer file-descriptor-like handles indexing a fixed-size socket table (`MAX_SOCKETS`). `net_recv()` is **blocking** — it busy-polls `net_poll()` for up to 5 seconds (500 ticks at 100 Hz). Do not call it from an IRQ handler.

### Parameters

| Function | Description |
|---|---|
| `net_socket(type)` | Allocate a socket of `type` (`SOCK_TCP` or `SOCK_UDP`). Returns fd `>= 0` on success, -1 if the socket table is full or `type` is invalid. |
| `net_bind(fd, ip, port)` | Set the local source `port` for the socket. The `ip` argument is currently ignored (the NIC's only IP is used). Returns 0 on success, -1 on bad fd. |
| `net_connect(fd, ip, port)` | For `SOCK_TCP`: perform the 3-way handshake to `(ip, port)`. For `SOCK_UDP`: just record `(ip, port)` as the default peer. Returns 0 on success, -1 on bad fd or handshake failure. |
| `net_send(fd, data, len)` | Send `len` bytes. TCP: segments the data and retransmits if needed. UDP: sends one datagram. Returns the number of bytes sent (== `len` on success), -1 on error. |
| `net_recv(fd, buf, len)` | Receive up to `len` bytes into `buf`. Blocks (polling `net_poll()`) for up to 5 seconds. Returns the number of bytes received (0..`len`), -1 on timeout or error. |
| `net_close(fd)` | For `SOCK_TCP`: send FIN, transition to TIME-WAIT, free the socket. For `SOCK_UDP`: just free the socket. Returns 0 on success, -1 on bad fd. |

### Return value

- `net_socket`: fd `>= 0` on success, -1 on error.
- `net_bind`, `net_connect`, `net_close`: 0 on success, -1 on error.
- `net_send`: byte count on success (== `len`), -1 on error.
- `net_recv`: byte count on success (0..`len`), -1 on timeout / error.

### Example — TCP client

```c
#include "net.h"

void tcp_fetch(void) {
    int fd = net_socket(SOCK_TCP);
    if (fd < 0) return;

    /* Connect to 10.0.2.2:80 (QEMU host). */
    if (net_connect(fd, IP4(10, 0, 2, 2), 80) != 0) {
        net_close(fd);
        return;
    }

    net_send(fd, "GET / HTTP/1.0\r\n\r\n", 18);

    char buf[512];
    int n = net_recv(fd, buf, sizeof(buf) - 1);
    if (n > 0) { buf[n] = 0; /* first chunk of HTTP response */ }

    net_close(fd);
}
```

### Example — UDP echo client

```c
#include "net.h"

void udp_echo(void) {
    int fd = net_socket(SOCK_UDP);
    if (fd < 0) return;

    net_bind(fd, 0, 12345);                              /* src port 12345 */
    net_send(fd, "ping", 4);                             /* to default peer set by net_connect */

    char buf[64];
    int n = net_recv(fd, buf, sizeof(buf));              /* wait up to 5s for reply */
    if (n > 0) { /* got reply */ }

    net_close(fd);
}
```

### Example — TCP server (low-level `tcp_listen`)

```c
static void on_connect(u32 ip, u16 port) {
    /* A new client connected. The handler runs in the net_poll() context. */
}

void start_server(void) {
    tcp_listen(8080, on_connect);
}
```

### Caveats

- **No `listen`/`accept` at the socket API level**: TCP server sockets use the lower-level `tcp_listen(port, handler)` API. A proper `net_listen` / `net_accept` pair is on the roadmap.
- **Blocking `net_recv`**: 5-second busy-poll. Never call from an IRQ context.
- **Single in-flight `recv` per socket**: the per-socket RX buffer is 1 KiB. A datagram or segment larger than `len` is truncated.
- **No non-blocking mode, no `select`/`poll`**: a future WP will add `O_NONBLOCK` and a readiness notification API.
- **No TLS / SSL**: plaintext only.

---

## Interface 24: DNS client

### Signature

```c
#include "net.h"

int dns_resolve(const char *name, u32 *ip_out);
```

### Purpose

Resolve a domain name (e.g. `"example.com"`) to an IPv4 address by sending a UDP DNS query to the configured DNS server (`net_get_dns()`). The query is a standard recursive A-record lookup (RD bit set). The first A record in the response is returned.

If no DNS server is configured (`net_get_dns() == 0`) or no NIC is up, `dns_resolve()` returns -1 immediately without sending anything.

This is a thin, synchronous client — no caching, no IPv6, no DNSSEC. If you need to look up the same name more than once, cache the result in L1.

### Parameters

| Function | Description |
|---|---|
| `dns_resolve(name, ip_out)` | Send a DNS A-record query for `name` to the configured DNS server, wait up to 5 seconds (500 ticks) for a response, and on success write the resolved IPv4 address (host-order `u32`) into `*ip_out`. Returns 0 on success, -1 on timeout, no NIC, no DNS server, or malformed response. |

### Return value

- 0 on success (the resolved address is in `*ip_out`).
- -1 on any failure.

### Example

```c
#include "net.h"

void dns_demo(void) {
    if (net_get_dns() == 0) {
        net_set_dns(IP4(8, 8, 8, 8));   /* or call dhcp_discover() */
    }

    u32 ip;
    if (dns_resolve("example.com", &ip) == 0) {
        /* ip == IP4(93, 184, 215, 14) (or whatever the resolver returned) */
        int fd = net_socket(SOCK_TCP);
        net_connect(fd, ip, 80);
        /* ... */
        net_close(fd);
    }
}
```

### Caveats

- **Synchronous**: blocks for up to 5 seconds polling the NIC.
- **No caching**: every call sends a fresh query. L1 should cache the result.
- **A records only**: AAAA (IPv6), CNAME chains, MX, TXT, etc. are not supported.
- **No DNS-over-TCP, no truncation retry**: if the response is truncated (TC bit set), `dns_resolve` returns -1.
- **Uses UDP source port 1077**: a fixed source port (`1024 + DNS_PORT`) is bound to receive the response. This means only one `dns_resolve()` call can be in flight at a time.

---

## Interface 25: DHCP client

### Signature

```c
#include "net.h"

int dhcp_discover(void);
```

### Purpose

Bring up IPv4 configuration automatically by sending a DHCP DISCOVER, waiting for a DHCP OFFER, replying with a DHCP REQUEST, and waiting for the DHCP ACK. On success, the kernel's `ip` / `mask` / `gateway` / `dns` are all set from the ACK's options, replacing any previous manual configuration.

The DHCP transaction uses transaction ID `0x12345678`, source UDP port 68, destination UDP port 67, broadcast flag set. The implementation registers a temporary UDP handler on port 68 to receive the OFFER and ACK.

`dhcp_discover()` is synchronous — it busy-polls `net_poll()` while waiting. Total worst-case runtime is a few seconds.

### Parameters

| Function | Description |
|---|---|
| `dhcp_discover()` | Send DISCOVER, receive OFFER, send REQUEST, receive ACK. Apply the offered IP, mask, gateway (option 3), and DNS (option 6) to the kernel's network configuration. Returns 0 on success, -1 on timeout, no NIC, or malformed response. |

### Return value

- 0 on success — `net_get_ip()` / `net_get_mask()` / `net_get_gateway()` / `net_get_dns()` now reflect the DHCP-assigned values.
- -1 on any failure (no NIC, timeout, malformed reply).

### Example

```c
#include "net.h"

void dhcp_demo(void) {
    net_init();
    net_start_timer();

    if (dhcp_discover() == 0) {
        /* net_get_ip() now returns the DHCP-assigned address.
         * Same for mask, gateway, DNS. */
    } else {
        /* Fall back to manual config. */
        net_set_ip(IP4(10, 0, 2, 15),
                   IP4(255, 255, 255, 0),
                   IP4(10, 0, 2, 2));
        net_set_dns(IP4(10, 0, 2, 3));
    }
}
```

### Caveats

- **No lease renewal**: the client does not track lease time or send DHCP RENEW / DHCP RELEASE. The lease expires silently; the kernel keeps using the address.
- **No DHCPREQUEST retry**: a single DISCOVER → OFFER → REQUEST → ACK exchange. If any step is lost, `dhcp_discover()` returns -1; the caller must retry.
- **Fixed transaction ID**: `0x12345678` for every call. This is fine for a single-host kernel but would clash if two Open Cube OS instances ran on the same network segment.
- **Uses UDP port 68**: a temporary handler is registered on port 68 during the exchange. If application code also binds to port 68, the DHCP handler will clobber it.

---

## Interface 26: Network shell commands

### Signature

```c
#include "net.h"

void net_register_shell_commands(void);
```

The commands themselves are registered via the WP-03 `shell_register_command(name, handler, help)` API (see `EXTENSIONS_WP03.md` section 14). The handlers live in `kernel/net.c` and are not exposed individually — they are accessed through the shell at the `oc>` prompt.

### Purpose

`net_register_shell_commands()` registers nine network-related shell commands so an interactive user can configure, inspect, and exercise the network stack from the `oc>` prompt without writing C code. Call it once at boot, after `net_init()` and `net_start_timer()`.

### Registered commands

| Command | Description |
|---|---|
| `ifconfig` | Print the current NIC configuration: MAC, IPv4 address, mask, gateway, DNS, link status. No arguments. |
| `ip [a.b.c.d mask g.w]` | With no args, same as `ifconfig`. With three dotted-quad args, calls `net_set_ip(ip, mask, gateway)`. |
| `route` | Print the routing table (currently just the default gateway). |
| `ping <host>` | Send ICMP echo requests to `<host>` (an IPv4 dotted-quad, or a hostname resolved via DNS). Prints RTT for each reply. Stops after 4 echoes or Ctrl-C. |
| `netstat` | Print cumulative TX/RX packet and byte counters, ARP cache, and a list of all open sockets. |
| `dhcp` | Run `dhcp_discover()`. On success prints the assigned IP, mask, gateway, and DNS. |
| `dns <name>` | Run `dns_resolve(name)` and print the resolved IPv4 address. |
| `lspci` | List all PCI devices discovered by `pci_init()`: bus/dev/func, vendor:device ID, class code, base addresses. |
| `wget <host> [port] [path]` | Open a TCP connection to `host:port` (default 80), send `GET path HTTP/1.0`, and dump the response body to the console. `host` may be a dotted-quad or a DNS name. |

### Parameters

- `net_register_shell_commands()` takes no parameters and returns no value.

### Return value

- `net_register_shell_commands`: no return value.
- Each shell command handler returns 0 on success, non-zero on error (the shell prints the command's own diagnostic, so the return value is mainly observed by shell scripts / pipes).

### Example

```c
#include "net.h"

void net_boot(void) {
    net_init();
    net_start_timer();
    net_register_shell_commands();   /* now: ifconfig, ping, dhcp, dns, ... work at oc> */
}
```

Interactive session example:

```
oc> ifconfig
eth0  MAC=52:54:00:12:34:56
      IP=10.0.2.15  mask=255.255.255.0
      GW=10.0.2.2    DNS=10.0.2.3
      link=UP
oc> dhcp
Sending DHCP discover...
DHCP success: IP=10.0.2.16
  mask=255.255.255.0
  gateway=10.0.2.2
  DNS=10.0.2.3
oc> dns example.com
example.com -> 93.184.215.14
oc> ping 93.184.215.14
64 bytes from 93.184.215.14: seq=0 time=12ms
64 bytes from 93.184.215.14: seq=1 time=11ms
^C
oc> wget example.com 80 /
HTTP/1.0 200 OK
Content-Type: text/html
...
oc> netstat
Network statistics:
  TX: 17 packets, 1432 bytes
  RX: 23 packets, 8743 bytes
  ARP requests: 4  replies: 3
  ICMP echo sent: 4  recv: 4
  TCP connections: 2
Sockets:
  [1] type=TCP remote=93.184.215.14:80
ARP cache:
  10.0.2.2 -> 52:54:00:12:34:56
oc> lspci
bus 0  dev 3  func 0  vendor=8086 device=100E class=020000 (ethernet)
bus 0  dev 4  func 0  vendor=1AF4 device=1001 class=010000 (storage)
```

### Caveats

- **`ping` runs synchronously**: it blocks the shell for the duration. Ctrl-C is the only way to bail out early.
- **`wget` dumps to console only**: there is no redirect-to-file yet (the shell's `>` redirect, added in WP-05, does work for `wget`'s output if you write `wget host 80 / > /tmp/page.html`).
- **`ifconfig` is read-only**: unlike the GNU/Linux `ifconfig`, it cannot bring interfaces up or down. The interface is always up after `net_init()`.
- **`lspci` is informational**: it has no flags or filters. For per-device details use `lspci` output to look up the vendor/device ID in a PCI ID database.
- **All commands require `net_init()` to have run**: calling them before `net_init()` will print `no NIC` and exit.

---

## ABI Stability

All WP-06 functions, structs, and their typedefs are frozen:

- **Network init / poll / config (item 22)**: `net_init`, `net_poll`, `net_start_timer`, `net_get_ip`, `net_get_mask`, `net_get_gateway`, `net_get_dns`, `net_set_ip`, `net_set_dns`, `net_get_mac`, `net_get_link_status`, `net_get_stats` — signatures frozen. The `net_stats_t` struct layout is frozen.
- **Socket API (item 23)**: `net_socket`, `net_bind`, `net_connect`, `net_send`, `net_recv`, `net_close` — signatures frozen. The `SOCK_TCP` / `SOCK_UDP` constants are frozen. The lower-level `tcp_connect`, `tcp_send`, `tcp_close`, `tcp_listen`, `udp_bind`, `udp_send` helpers and the `tcp_handler_fn` / `udp_handler_fn` typedefs are also frozen.
- **DNS (item 24)**: `dns_resolve` — signature frozen.
- **DHCP (item 25)**: `dhcp_discover` — signature frozen.
- **Shell commands (item 26)**: `net_register_shell_commands` — signature frozen. The set of registered command names (`ifconfig`, `ip`, `route`, `ping`, `netstat`, `dhcp`, `dns`, `lspci`, `wget`) is frozen; their argument syntax is frozen.

Constants frozen: `ETH_ADDR_LEN`, `ETH_FRAME_MAX`, `ETH_TYPE_ARP`, `ETH_TYPE_IP`, `IP_PROTO_ICMP`, `IP_PROTO_TCP`, `IP_PROTO_UDP`, `TCP_SYN/ACK/FIN/RST/PSH`, `DHCP_CLIENT_PORT`, `DHCP_SERVER_PORT`, `DNS_PORT`, the `IP4(a,b,c,d)` macro, and the `htons/ntohs/htonl/ntohl` byte-order helpers.

The `net_stats_t` struct layout is frozen with respect to the fields documented above. New fields may be appended at the end in future WPs; callers must zero-initialise the struct before passing it in to allow this.

Future WPs may add new functions (e.g. `net_listen`, `net_accept`, `net_set_nonblock`) but will not change existing ones incompatibly.

## Loading model

WP-06 still does not have a dynamic module loader — L1 is linked into the same binary as L0. The extension API is designed to survive the transition to loadable modules unchanged: a loadable protocol driver would just call `udp_bind()` / `tcp_listen()` from its module-init function, and a loadable NIC driver would hook into the existing NIC-probe path.
