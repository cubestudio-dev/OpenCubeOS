/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 2.0.0 - services/net L1 surface (SYS_L1SVC cat "net"):
 * the diagnostic half of the v1 net command class.  ifconfig/ping/
 * netstat/wget reach the SAME bodies through the ush built-ins +
 * SYS_NETCMD bridge (those four stay L0-declared); ssh/sshd stay L0
 * as interactive-session commands.  Bodies live in services/net/
 * net_core.c, referenced by the op table. */
#include "core_registry.h"
#include "l1svc_defs.h"
#include "net_l1_cmds.h"
#include "core_kcap.h"
#include "screen_console.h"  /* 3.0.0-WP04 G11a: ssh/sshd command bodies */
#include "lib_string.h"   /* strncpy for the NETCMD bridge */

typedef int (*net_cmd_fn_t)(const char *);

/* ---- 3.0.0-WP04 G11a: ssh / sshd command bodies -----------------------
 * Verbatim v2.2.0 core/main.c shell_cmd_ssh / shell_cmd_sshd bodies;
 * the console writes are captured into the op's out buffer by the
 * same core_kcap run the other net commands use.  sshd's interactive
 * session logging prints console-direct from inside the engine
 * (net_sshd_log) and is mirrored by the capture chain, exactly like
 * the v2.x serial-mirror contract. */

int net_cmd_ssh(const char *args) {
    /* Parse: ssh <ip> [port] [user] [password] */
    char host[64] = {0};
    int port = 2222;  /* default (Python ssh server) */
    char user[32] = "oc";
    char pass[32] = "oc";
    int i = 0, j = 0;
    /* Skip leading spaces */
    while (args[i] == ' ') i++;
    while (args[i] && args[i] != ' ' && j < 63) host[j++] = args[i++];
    host[j] = 0;
    while (args[i] == ' ') i++;
    if (args[i]) {
        port = 0;
        while (args[i] && args[i] >= '0' && args[i] <= '9') {
            port = port * 10 + (args[i] - '0');
            i++;
        }
        if (port == 0) port = 2222;
        while (args[i] == ' ') i++;
        if (args[i]) {
            j = 0;
            while (args[i] && args[i] != ' ' && j < 31) user[j++] = args[i++];
            user[j] = 0;
            while (args[i] == ' ') i++;
            if (args[i]) {
                j = 0;
                while (args[i] && j < 31) pass[j++] = args[i++];
                pass[j] = 0;
            }
        }
    }
    if (!host[0]) {
        screen_console_puts("usage: ssh <ip> [port=2222] [user=oc] [password=oc|-]\n");
        screen_console_puts("  password \"-\" = publickey auth (needs /etc/ssh_client_key; falls back to password if absent)\n");
        return 1;
    }
    /* password "-" = use publickey authentication with the client key
     * from /etc/ssh_client_key. BUG-0075: the kernel no longer embeds a
     * universal identity key, so "-" without an installed key falls
     * back to the default password instead of failing. */
    int use_pubkey = (pass[0] == '-' && pass[1] == 0);
    if (use_pubkey) pass[0] = 0;
    /* Resolve host (IP first) */
    u32 ip = 0;
    /* Try parsing as IP "a.b.c.d" */
    int a = 0, b = 0, c = 0, d = 0;
    int ai = 0;
    int ok = 1;
    int parts[4] = {0, 0, 0, 0};
    int pp = 0;
    int cur = 0;
    int got_digit = 0;
    while (host[ai]) {
        if (host[ai] >= '0' && host[ai] <= '9') {
            cur = cur * 10 + (host[ai] - '0');
            got_digit = 1;
        } else if (host[ai] == '.') {
            if (!got_digit || pp >= 3) { ok = 0; break; }
            parts[pp++] = cur;
            cur = 0; got_digit = 0;
        } else {
            ok = 0; break;
        }
        ai++;
    }
    if (ok && got_digit && pp == 3) {
        parts[3] = cur;
        a = parts[0]; b = parts[1]; c = parts[2]; d = parts[3];
        if (a < 256 && b < 256 && c < 256 && d < 256) {
            ip = ((u32)a << 24) | ((u32)b << 16) | ((u32)c << 8) | (u32)d;
        } else ok = 0;
    } else ok = 0;
    if (!ok) {
        extern int net_dns_resolve(const char *name, u32 *ip);
        if (net_dns_resolve(host, &ip) != 0) {
            screen_console_puts("cannot resolve host\n");
            return 1;
        }
    }
    char buf[80];
    extern int net_ssh_connect(u32 ip, u16 port, const char *user, const char *pass);
    strcpy(buf, "[ssh] connecting to ");
    /* Big-endian display */
    char num[10];
    u64_to_str((u64)((ip >> 24) & 0xFF), num); strcat(buf, num); strcat(buf, ".");
    u64_to_str((u64)((ip >> 16) & 0xFF), num); strcat(buf, num); strcat(buf, ".");
    u64_to_str((u64)((ip >> 8) & 0xFF), num); strcat(buf, num); strcat(buf, ".");
    u64_to_str((u64)(ip & 0xFF), num); strcat(buf, num);
    strcat(buf, ":");
    char n[10]; u64_to_str((u64)port, n); strcat(buf, n);
    strcat(buf, " user="); strcat(buf, user);
    strcat(buf, "\n");
    screen_console_puts(buf);
    int rc = net_ssh_connect(ip, (u16)port, user, use_pubkey ? 0 : pass);
    if (rc < 0 && use_pubkey) {
        /* BUG-0075: publickey needs /etc/ssh_client_key; when it is not
         * installed, retry once with the password instead of dying. */
        screen_console_puts("[ssh] publickey unavailable (no /etc/ssh_client_key), retrying with password\n");
        rc = net_ssh_connect(ip, (u16)port, user, pass[0] ? pass : "oc");
    }
    if (rc == 0) {
        screen_console_puts("[ssh] authenticated (USERAUTH_SUCCESS)\n");
        /* WP-09: full channel open + exec round trip over the encrypted channel.
         * The command is sent via SSH_MSG_CHANNEL_REQUEST exec; server output
         * is read from CHANNEL_DATA until EOF/CLOSE. */
        extern int net_ssh_exec(const char *command, void *output, int output_len);
        static char net_ssh_out[1024];
        int nn = net_ssh_exec("echo hello-from-OpenCubeOS-kernel-ssh", net_ssh_out, (int)sizeof(net_ssh_out) - 1);
        if (nn > 0) {
            net_ssh_out[nn] = 0;
            screen_console_puts("[ssh] exec output: ");
            screen_console_puts(net_ssh_out);
            screen_console_puts("\n");
        } else {
            screen_console_puts("[ssh] exec: no output received\n");
        }
    } else {
        char b2[40]; strcpy(b2, "[ssh] failed (code ");
        char num2[10]; u64_to_str((u64)(-rc), num2);
        strcat(b2, num2); strcat(b2, ")\n");
        screen_console_puts(b2);
    }
    extern void net_ssh_close(void);
    net_ssh_close();
    return (rc == 0) ? 0 : 1;
}

int net_cmd_sshd(const char *args) {
    int port = 22;
    char user[32] = "oc";
    char pass[32] = "oc";
    int i = 0;
    while (args[i] == ' ') i++;
    if (args[i] >= '0' && args[i] <= '9') {
        port = 0;
        while (args[i] && args[i] >= '0' && args[i] <= '9') {
            port = port * 10 + (args[i] - '0');
            i++;
        }
    }
    while (args[i] == ' ') i++;
    int j = 0;
    while (args[i] && args[i] != ' ' && j < 31) user[j++] = args[i++];
    user[j] = 0;
    while (args[i] == ' ') i++;
    j = 0;
    while (args[i] && j < 31) pass[j++] = args[i++];
    pass[j] = 0;

    extern int net_sshd_main(u16 port, const char *user, const char *pass);
    char b[80];
    strcpy(b, "[sshd] starting on port ");
    char n[10];
    u64_to_str((u64)port, n);
    strcat(b, n);
    strcat(b, " (one session, then exit; Ctrl+C cancels wait)");
    screen_console_puts(b);
    screen_console_puts("\n");
    return net_sshd_main((u16)port, user, pass);
}

static int net_l1_dispatch(u32 op, const char *arg, char *out, int cap) {
    if (!out || cap <= 0) return -1;
    if (op == L1SVC_NET_SSHD) {
        /* 3.0.0-WP04 G11a: sshd is an INTERACTIVE one-session server -
         * its engine logs (net_sshd_log) must reach the console/serial
         * LIVE, exactly as in v2.2.0.  Route it DIRECTLY, not through
         * the core_kcap capture (which would swallow the session log
         * until the syscall returns).  The op text stays empty; the
         * engine's own console output is the session transcript. */
        out[0] = 0;
        return net_cmd_sshd(arg ? arg : "");
    }
    net_cmd_fn_t fn = 0;
    switch (op) {
        case L1SVC_NET_ARP:         fn = net_cmd_arp;         break;
        case L1SVC_NET_DHCP:        fn = net_cmd_dhcp;        break;
        case L1SVC_NET_DNS:         fn = net_cmd_dns;         break;
        case L1SVC_NET_DNSTEST:     fn = net_cmd_dnstest;     break;
        case L1SVC_NET_FIREWALL:    fn = net_cmd_firewall;    break;
        case L1SVC_NET_IP:          fn = net_cmd_ip;          break;
        case L1SVC_NET_LSPCI:       fn = net_cmd_lspci;       break;
        case L1SVC_NET_NF_TEST:     fn = net_cmd_nf_test;     break;
        case L1SVC_NET_ROUTE:       fn = net_cmd_route;       break;
        case L1SVC_NET_TCPCC_TEST:  fn = net_cmd_tcpcc_test;  break;
        case L1SVC_NET_TCPSTATS:    fn = net_cmd_tcpstats;    break;
        case L1SVC_NET_TCPTEST:     fn = net_cmd_tcptest;     break;
        case L1SVC_NET_SSH:        fn = net_cmd_ssh;         break;
        case L1SVC_NET_SSHD:       fn = net_cmd_sshd;        break;
        default: return -1;
    }
    return core_kcap_run_fn(fn, arg, out, cap) == 0 ? 0 : -1;
}

/* v2.0.0 group O: net service identity (stack + diagnostics); the
 * TCP/IP bring-up stays in kmain.  (The v1 declaration named this
 * service for the register-class interface; group O re-declares it
 * for the L1 dispatch surface.) */
OC_SERVICE_DECL_L1("net", "network service (stack + diagnostics + ring-3 surface)",
                   (oc_service_init_fn_t)0, (oc_service_late_fn_t)0,
                   net_l1_dispatch);

/* ---- SYS_NETCMD bridge ------------------------------------------------
 * 3.0.0-WP03: the syscall layer used to synthesize an oc> command
 * line ("ifconfig", "ping <host>", ...) and run it through the
 * shell's captured-execution path.  The shell is gone; the syscall
 * now calls THIS exported entry, which dispatches the same four
 * bodies through the same core_kcap capture.  (op codes are the
 * historical NETCMD_* from core_syscall.h.) */
int net_netcmd_captured(u32 op, const char *arg, char *out, int cap) {
    if (!out || cap <= 0) return -1;
    net_cmd_fn_t fn = 0;
    switch (op) {
        case 1 /* NETCMD_IFCONFIG */: fn = net_cmd_ifconfig; break;
        case 2 /* NETCMD_PING */: {
            /* the historical syscall built "ping <host>" from the
             * argument; the body takes the host as its args string */
            static char host[128];
            if (!arg || !arg[0]) return -1;
            strncpy(host, arg, sizeof(host) - 1);
            host[sizeof(host) - 1] = 0;
            return core_kcap_run_fn(net_cmd_ping, host, out, cap) == 0 ? 0 : -1;
        }
        case 3 /* NETCMD_NETSTAT */: fn = net_cmd_netstat; break;
        case 4 /* NETCMD_WGET */: {
            /* same: "wget <host> [port] [path]" was the composed line */
            static char wargs[256];
            if (!arg || !arg[0]) return -1;
            strncpy(wargs, arg, sizeof(wargs) - 1);
            wargs[sizeof(wargs) - 1] = 0;
            return core_kcap_run_fn(net_cmd_wget, wargs, out, cap) == 0 ? 0 : -1;
        }
        default: return -1;
    }
    return core_kcap_run_fn(fn, arg, out, cap) == 0 ? 0 : -1;
}
