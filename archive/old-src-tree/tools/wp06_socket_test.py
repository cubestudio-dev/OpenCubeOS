#!/usr/bin/env python3
"""WP-06 socket-mode validation.

Two QEMU instances are connected back-to-back via a socket netdev (L2 Ethernet
bridge between two guests, no slirp, no host networking).  Both run the Open
Cube OS ISO.  We configure static IPs on each and ping from client -> server.

If the ping succeeds we have proven the network stack works on a REAL Ethernet
path (TX -> wire -> RX in both directions, ARP, IP, ICMP) exercised by two
independent instances of our own stack -- not a slirp internal shortcut.
"""
import subprocess, time, os, sys, socket, threading, re

ISO = sys.argv[1] if len(sys.argv) > 1 else '/home/z/my-project/oc-os/build/opencube.iso'
QEMU = '/home/z/opt/extract/usr/bin/qemu-system-x86_64'
QEMU_DATA = '/home/z/opt/extract/usr/share/qemu'
SEABIOS = '/home/z/opt/extract/usr/share/seabios'

# QEMU 10 needs libcapstone/libslirp/libpmem/etc which live in two dirs.
os.environ['LD_LIBRARY_PATH'] = (
    '/tmp/qlibs/extracted/usr/lib/x86_64-linux-gnu:'
    '/home/z/opt/extract/usr/lib/x86_64-linux-gnu:'
    + os.environ.get('LD_LIBRARY_PATH', ''))

SERV_SOCK = '/tmp/oc-srv.sock'
CLI_SOCK  = '/tmp/oc-cli.sock'
NET_SOCK  = '127.0.0.1:12345'   # TCP socket for the -netdev socket channel

for s in (SERV_SOCK, CLI_SOCK):
    try: os.unlink(s)
    except: pass

def qemu_args(serial_sock, mac, net_role):
    """net_role: 'listen' or 'connect'."""
    a = [
        QEMU, '-L', QEMU_DATA, '-L', SEABIOS,
        '-m', '256M', '-cdrom', ISO, '-boot', 'd',
        '-display', 'none',
        '-serial', 'unix:%s,server,nowait' % serial_sock,
        '-monitor', 'none', '-vga', 'std', '-snapshot', '-no-reboot',
        '-netdev', 'socket,id=n0,%s=%s' % (net_role, NET_SOCK),
        '-device', 'e1000,netdev=n0,mac=%s' % mac,
    ]
    return a

class Guest:
    def __init__(self, name, serial_sock, mac, net_role):
        self.name = name
        self.buf = bytearray()
        self.proc = subprocess.Popen(qemu_args(serial_sock, mac, net_role),
                                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        self.ser = None
        # connect to the serial unix socket (qemu is the server)
        for _ in range(100):
            try:
                self.ser = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                self.ser.connect(serial_sock)
                break
            except Exception:
                time.sleep(0.1)
        if self.ser is None:
            raise RuntimeError('%s: could not connect to serial' % name)
        self.ser.setblocking(False)
        self.reader = threading.Thread(target=self._reader, daemon=True)
        self.reader.start()

    def _reader(self):
        while True:
            try:
                data = self.ser.recv(4096)
                if not data: break
                self.buf.extend(data)
            except (BlockingIOError, InterruptedError):
                time.sleep(0.05)
            except Exception:
                break

    def send(self, s):
        if isinstance(s, str): s = s.encode()
        self.ser.sendall(s)

    def wait_for(self, pat, timeout=30):
        rx = re.compile(pat)
        t0 = time.time()
        start = len(self.buf)
        while time.time() - t0 < timeout:
            text = self.buf[start:].decode('latin-1', errors='replace')
            text = re.sub(r'\x1b\[[0-9;]*[a-zA-Z]', '', text).replace('\r','')
            if rx.search(text):
                return text
            time.sleep(0.1)
        return None

    def tail(self, n=40):
        text = self.buf.decode('latin-1', errors='replace')
        text = re.sub(r'\x1b\[[0-9;]*[a-zA-Z]', '', text).replace('\r','')
        lines = [l for l in text.split('\n') if l.strip()]
        return '\n'.join(lines[-n:])

    def quit(self):
        try: self.proc.terminate()
        except: pass
        try: self.proc.wait(timeout=3)
        except:
            try: self.proc.kill()
            except: pass

def main():
    print('=== WP-06 socket-mode validation ===', flush=True)
    print('ISO: %s (%d bytes)' % (ISO, os.path.getsize(ISO)), flush=True)
    print('NET channel: %s' % NET_SOCK, flush=True)

    print('\n[1] starting SERVER guest (listen, mac ...34:01)...', flush=True)
    srv = Guest('server', SERV_SOCK, '52:54:00:12:34:01', 'listen')
    print('    server qemu pid=%d' % srv.proc.pid, flush=True)

    print('[2] starting CLIENT guest (connect, mac ...34:02)...', flush=True)
    cli = Guest('client', CLI_SOCK, '52:54:00:12:34:02', 'connect')
    print('    client qemu pid=%d' % cli.proc.pid, flush=True)

    print('\n[3] waiting for both guests to reach the shell...', flush=True)
    # The kernel prints a shell prompt.  Look for a distinctive boot marker.
    s_boot = srv.wait_for(r'WP-06|net:|e1000:|shell|>|oc>', timeout=40)
    c_boot = cli.wait_for(r'WP-06|net:|e1000:|shell|>|oc>', timeout=40)
    print('    server boot marker: %s' % ('OK' if s_boot else 'TIMEOUT'), flush=True)
    print('    client boot marker: %s' % ('OK' if c_boot else 'TIMEOUT'), flush=True)
    time.sleep(2)  # let the prompt settle

    print('\n[4] configuring SERVER static IP 10.0.0.1/24 ...', flush=True)
    srv.send('ip 10.0.0.1 255.255.255.0 10.0.0.1\n')
    time.sleep(1.5)
    srv.send('ifconfig\n')
    time.sleep(1.5)
    print('--- server ifconfig output ---', flush=True)
    print(srv.tail(30), flush=True)

    print('\n[5] configuring CLIENT static IP 10.0.0.2/24 ...', flush=True)
    cli.send('ip 10.0.0.2 255.255.255.0 10.0.0.1\n')
    time.sleep(1.5)
    cli.send('ifconfig\n')
    time.sleep(1.5)
    print('--- client ifconfig output ---', flush=True)
    print(cli.tail(30), flush=True)

    print('\n[6] ping from CLIENT (10.0.0.2) -> SERVER (10.0.0.1) ...', flush=True)
    # mark the buffer position so we can slice exactly the ping output
    cli_mark = len(cli.buf)
    cli.send('ping 10.0.0.1\n')
    # ping sends 4 echos with up to 3s timeout each + delay
    time.sleep(18)
    text = cli.buf[cli_mark:].decode('latin-1', errors='replace')
    text = re.sub(r'\x1b\[[0-9;]*[a-zA-Z]', '', text).replace('\r','')
    print('--- client ping output ---', flush=True)
    print(text, flush=True)

    replies = text.count('reply received')
    timeouts  = text.count('timeout')
    print('\n[7] result: %d replies, %d timeouts' % (replies, timeouts), flush=True)

    # Diagnostics: dump netstat on both to see TX/RX counts + e1000 regs.
    print('\n[8] diagnostics: netstat on both guests', flush=True)
    cli_mark2 = len(cli.buf)
    cli.send('netstat\n'); time.sleep(2)
    print('--- client netstat ---', flush=True)
    print(cli.buf[cli_mark2:].decode('latin-1','replace')
          .encode('ascii','replace').decode(), flush=True)
    srv_mark2 = len(srv.buf)
    srv.send('netstat\n'); time.sleep(2)
    print('--- server netstat ---', flush=True)
    print(srv.buf[srv_mark2:].decode('latin-1','replace')
          .encode('ascii','replace').decode(), flush=True)

    # also dump server tail
    print('\n--- server tail (last 15 lines) ---', flush=True)
    print(srv.tail(15), flush=True)

    cli.quit(); srv.quit()

    if replies > 0:
        print('\n=== SOCKET-MODE VALIDATION: PASS ===', flush=True)
        print('The network stack works on a real L2 Ethernet path between two'
              ' independent QEMU guests.', flush=True)
        return 0
    print('\n=== SOCKET-MODE VALIDATION: FAIL ===', flush=True)
    return 1

if __name__ == '__main__':
    sys.exit(main())
