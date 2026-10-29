3.0.0-WP04 G11a - ssh + sshd migrate to L1 (net ops 13/14)
==========================================================

The v2.2.0 SSH engines (services/net/net_ssh.c 1867 L + net_sshd.c
2219 L + net_ssh.h, protocol subsets of RFC 4251-4254: banner/KEXINIT
with strict-kex, curve25519-sha256 + dh-group14-sha256, NEWKEYS,
password+publickey userauth, channel exec, byte-threshold rekey) are
resurrected VERBATIM into services/net - WP-03 deleted them with the
L0 user plane and net_l1_cmds.h documented the ring-3 takeover.

Two new net category ops (append-only, after TCPTEST=12):
  L1SVC_NET_SSH  13   ssh <ip> [port] [user] [password]
  L1SVC_NET_SSHD 14   sshd [port] [user] [password]
Bodies = the v2.2.0 core/main.c shell_cmd_ssh / shell_cmd_sshd
verbatim (net_l1.c).  The ssh client op renders through the same
core_kcap capture as every other net command.  sshd is routed
DIRECTLY (no capture): its engine logs console-direct through
net_sshd_log exactly as v2.2.0 - an interactive one-session server
must show its session live, and a capture window would swallow the
transcript until the syscall returns.

net_sshd.c adaptation (the ONLY functional change, documented
in-file): shell_execute_captured() died with the L0 shell.  The exec
path now spawns the command's L1 APP (src/L1/<name>/) and waits for
the process to retire INSIDE core_kcap_run_fn (WP-03's console
capture infra) - every console byte the app writes lands in
exec_out, the same contract the old shell-capture chain provided,
including hook chaining + restore (serial mirror survives).

Apps: src/L1/ssh/main.c + src/L1/sshd/main.c (thin l1svc_main
wrappers, cat "net").

E2E (tools/ssh_g11a_e2e.py, rig: slirp user-net + hostfwd
tcp::2223-:22 + build/etc.img FAT /etc volume with
host_keygen_etc.py-injected keys - TCG runs minutes-long RSA keygen,
so the e2e pre-seeds the per-installation keys exactly like the
v2.2.0 rig):
  A. guest `ssh 10.0.2.2 2222 oc oc` -> host paramiko_sshd.py:
     authenticated (USERAUTH_SUCCESS) + exec output
     "hello-from-OpenCubeOS-kernel-ssh" round trip - PASS
  B. guest `dhcp` + `sshd 22 oc oc` -> host paramiko client through
     the hostfwd: banner/KEX/userauth OK, exec "uname" returns
     "Open Cube OS" (the L1 uname app run by the captured-spawn path),
     guest logs "exec: captured N bytes of output" - PASS
ssh_sshd_e2e.log holds the full transcript (both directions).

Host-side timeouts are generous on purpose: the guest handshake (KEX
+ AES + RSA under TCG) takes tens of seconds per stage; the first e2e
draft's short banner timeout aborted sessions the guest was still
serving (the 8 retry attempts each cut a live half-handshake).

Build: 0 errors 0 warnings; 263 embedded programs.
