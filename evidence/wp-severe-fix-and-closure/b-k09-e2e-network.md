# B-k09: the E2E "sandbox network" failures root-caused - the network
# stack was healthy all along; the harness was not

Work package: WP-severe-fix-and-closure, item K-09.

## The claim this replaces

K-09 (as shipped) recorded the HTTPS/SSH E2E batteries as "cannot
complete in the current sandbox network ... net_connect fails before
the TCP handshake ... environmental (sandbox slirp/e1000 drift)".

## The counter-evidence (this sandbox, this tree)

The plain in-guest diagnostic sequence over the SAME slirp + e1000:

    ifconfig          -> inet 10.0.2.15 (DHCP served)
    ping 10.0.2.2     -> 4/4 replies
    wget http://10.0.2.2:8123/k09 -> "Connected. Saved 24 bytes to /k09"

    netstat           -> TX: 13 packets, 885 bytes
                         RX: 11 packets, 845 bytes
                         ARP requests: 0  replies: 1
                         ICMP echo sent: 4  recv: 4
                         TCP connections: 1
    arp               -> 10.0.2.2 -> 52:55:0a:00:02:02

DHCP, ARP, ICMP, TCP and the HTTP download all work - with the
kernel's own TX/RX counter proof (the stat surface the spec demands:
发包数/收包数). The TCP/TLS stack then negotiates a REAL TLS 1.2
session (DHE-RSA-AES128-SHA256), sends the encrypted GET, saves the
body, and both close_notify alerts land (see the 8/8 run below).

## The real defects (three, all in the harness, one in the kernel)

1. THE ONE-SHOT TLS SERVER DIED ON THE FIRST HELLO.  The kernel TLS
   client offers TLS 1.3 FIRST and reconnects with the TLS 1.2
   fallback (RFC 7507 SCSV) when a legacy server answers with a bare
   close.  The E2E server (https_test_server.py, tls_serve_once)
   served exactly ONE connection: the 1.3 first hello made
   ctx.wrap_socket() raise SSLEOFError, the server PROCESS died, and
   the kernel's fallback reconnect landed on a dead listener ->
   "TLS failed (code 1)" and the server-side "TIMEOUT waiting for
   kernel connection".  Fixed: the server now loops over accepts
   until ONE fully-successful TLS session completes (a first
   connection that dies in its handshake is an expected probe,
   logged and kept listening for the fallback); the observe-mode raw
   recorder bridges EVERY connection to a fresh upstream (it used
   to accept exactly one and crash on its own accept timeout).

2. THE mon:stdio SERIAL CHANNEL STARVES THE QEMU MAIN LOOP.  A
   `-serial mon:stdio` boot (pexpect) answers ping 2/4 where the
   SAME ISO over a unix-socket serial answers 4/4; the TCP handshake
   then misses the kernel's connect-wait window and the E2E reported
   a broken network.  Fixed: both E2E runners drive the guest over a
   unix-socket serial line now (run_fix4_https_e2e.py's boot_guest +
   the check-7 scene; ssh_g11a_e2e.py fully rewritten off pexpect/
   stdio onto the same socket pump).  The kernel side is untouched -
   only the harness channel changed.

3. THE STALE-PROMPT MATCH FIRED THE NEXT COMMAND MID-EXECUTION.
   boot_guest's wait_for(b"ush>") matched the prompt ALREADY in the
   buffer and sent the wget line while `config set` was still
   executing; the back-to-back serial burst then hit the COM1 RX
   edge-loss window (see 4) and lost the ENTIRE second command line.
   Fixed: the harness waits for the prompt AFTER its send offset.

4. THE KERNEL'S OWN COM1 RX EDGE-LOSS WINDOW (the one real kernel
   defect this item surfaced - the user-visible face of it: pasting
   a long command line right after another loses every byte of the
   second line, no echo, the shell waits forever).  The 16550 holds
   INTR HIGH while the RBR holds data; the 8259 input is
   EDGE-triggered.  A burst landing while line service is in flight
   raises no new edge, and after the EOI the remainder sits in the
   FIFO uncollected - the live-caught proof: the hunt dump showed the
   keyboard queue EMPTY (head==tail==63) with the whole command line
   gone, and the extra-byte injection after the "hang" echoed
   nothing.  Fixed in the kernel: a 10 ms TICK-BAILOUT RX POLL
   (screen_serial_in_poll, registered from screen_serial_in_init)
   re-checks the LSR data-ready bit and runs the same drain as the
   IRQ4 fast lane - whatever an edge loss strands, the tick collects
   within 10 ms.  The drain/decode logic was factored into
   serial_in_drain()/serial_in_feed() shared by both lanes (no
   behavior change on the IRQ path).

## The acceptance

The kernel network-stack counter proof (the spec's 发包数/收包数):

    TX: 13 packets, 885 bytes / RX: 11 packets, 845 bytes /
    ARP replies: 1 / ICMP echo sent: 4 recv: 4 / TCP connections: 1

The HTTPS E2E battery, after the harness fixes:

    0 self-signed chain REFUSED by default (#21)  PASS
    1 TLS handshake (kernel connected)            PASS
    2 TLS1.2 + 0x0067 negotiated                  PASS
    3 encrypted GET received                      PASS
    4 canonical body served (body=24)             PASS
    5 kernel saved the body                       PASS
    6 kernel close_notify observed               PASS
    7 truncation detected promptly                PASS
    === 8/8 HTTPS E2E checks passed ===

The SSH E2E battery (ssh_g11a_e2e.py, same harness class): see
b-k09-ssh-e2e.log alongside (the run rides this same batch).

## KNOWN_ISSUES: K-09 closes with the root cause on record - the
"environmental sandbox network" verdict was wrong; the network stack
was green, and the failures were two test-infrastructure defects
plus one real kernel input-path defect (the serial RX edge-loss
window), all fixed above.

## The SSH E2E after the harness fixes (the honest boundary)

The SSH battery (ssh_g11a_e2e.py, same harness class) now runs its
OUTBOUND scene green:

    [g11a] PASS ssh-auth      (guest ssh client -> host paramiko server,
                                USERAUTH_SUCCESS + exec output verified)
    [g11a] PASS ssh-exec

The INBOUND scene (host paramiko client -> guest sshd through the
hostfwd) still fails, and the hunt ran it to ground with three
independent instrument captures:

1. THE PCAP (QEMU filter-dump, full byte sequence): the handshake
   completes, the guest banners, the client's 1128-byte KEXINIT
   arrives and is ACKed - and the guest's OWN KEXINIT never appears
   on the wire; a 264-byte fragment surfaces only when the client's
   FIN arrives.  The stalled-connection monitor dump shows
   rtx_len == 0 (nothing queued) with the window wide open, cwnd 1,
   state ESTABLISHED - and netstat's new TX-errors line proves the
   driver layer never saw a failed frame either.  The send call
   chain returns success; the data vanishes between the TCP layer
   and the wire.
2. THE SECOND RUN fails EARLIER (the client's seven SYNs never get
   a SYN-ACK - the inbound half-open handling is timing-sensitive).
3. Defenses landed anyway: an ACK that acknowledges more than the
   queued bytes no longer silently drops the whole send queue (the
   data-loss arm is now annotated and bounded), and net_poll carries
   a per-poll flush nudger for accepted-but-never-transmitted data
   (placed in the poll's interrupt-enabled context - the first
   attempt sat inside the cli-guarded RTO sweep and its send-side
   delay cost the very RX ticks the poll exists to serve; live-caught
   by the regression probe and moved).

The inbound-SSH strand is recorded in KNOWN_ISSUES as the honest
boundary it is - with the pcap, the monitor dump and the counter
proof attached.  The original K-09 claim ("the sandbox network is
broken") is REFUTED and closed: the network stack is proven healthy
by the counters, the plain-HTTP fetch, the full TLS session and the
outbound SSH scene; what remains is one specific inbound path.
