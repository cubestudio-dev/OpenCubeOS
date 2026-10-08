/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * sshd.c - Open Cube OS kernel-side SSH-2.0 server (WP-09).
 *
 * Implements a real, working SSH server so remote peers can execute shell
 * commands inside the kernel:
 *
 *   - TCP server via net_tcp_listen() + net_accept()          (net.c, WP-09)
 *   - SSH-2.0 transport: banner, KEXINIT, KEXDH (RFC 3526 group14
 *     diffie-hellman-group14-sha256), NEWKEYS
 *   - aes128-cbc + hmac-sha2-256 encrypted packet framing (same wire format
 *     as the ssh.c client, with c2s/s2c roles mirrored for the server side)
 *   - USERAUTH: password check (oc / oc by default, settable via command args)
 *   - CHANNEL: session open, "exec" requests, exit-status, EOF, CLOSE
 *   - exec engine: runs a real kernel shell command line via
 *     shell_execute_line() while a console hook captures the output bytes
 *   - host authentication: real RSA-2048 (PKCS#1 v1.5 / rsa-sha2-256)
 *     signature over the exchange hash H, computed with the shared
 *     big-integer modular exponentiation engine (crypto.c)
 *
 * Verified end-to-end against paramiko (see tools/net_ssh_test_client.py).
 */

#include "types.h"
#include "lib_string.h"
#include "screen_console.h"
#include "l1_ext.h"
#include "core_timer.h"
#include "crypto_core.h"
#include "crypto_bn.h"
#include "mem_pmm.h"
#include "net_core.h"
#include "net_ssh.h"
#include "shell.h"
#include "driver_input_keyboard.h"   /* BUG-0132 FIX: ^C cancels blocking accept */
/* BUG-0075 FIX (A14-24): the embedded universal host key header
 * (net_sshd_rsa_key.h) was DELETED - see the comment at the
 * net_sshd_hostkey_ensure() call in net_sshd_main(). The private
 * exponent must never ship inside a downloadable image again. */
#include "crypto_curve25519.h"
#include "crypto_rsa.h"
#include "crypto_sha512.h"
#include "fs_vfs.h"

/* ============================================================
 * BUG-0075 FIX (A14-24): decouple the three roles the embedded
 * "universal" RSA-2048 key used to play.
 *
 * The stock kernel image shipped a FIXED RSA-2048 key pair (in
 * net_sshd_rsa_key.h) that simultaneously was:
 *   1. the sshd HOST key (signs the exchange hash),
 *   2. the sshd AUTHORIZED USER key (publickey auth accepted the
 *      embedded public blob), and
 *   3. the SSH CLIENT identity key (net_ssh.c signed with the
 *      embedded private exponent).
 * Since the key ships in the public source tree, "publickey" auth
 * was world-access, and the host key is known to every attacker, so
 * a MITM could impersonate any sshd.
 *
 * Now each role has its own, per-installation secret under /etc:
 *   /etc/ssh_host_key        512 bytes: n[256] || d[256]  (e fixed 65537)
 *     - loaded if present; otherwise GENERATED on first sshd start
 *       (Miller-Rabin RSA-2048, seeded from the CSPRNG) and saved.
 *     - if /etc is unavailable, sshd falls back to the embedded key
 *       and says so loudly (legacy behavior, not silently trusted).
 *   /etc/ssh_authorized_keys one 512-hex-digit modulus per line
 *     - publickey auth ONLY accepts signatures under these moduli;
 *       no file (or empty) => publickey is refused entirely and only
 *       password auth remains.
 *   /etc/ssh_client_key      512 bytes: n[256] || d[256]
 *     - used by the SSH CLIENT (net_ssh.c) for publickey auth; absent
 *       => client falls back to password auth (see net_ssh.c).
 * ============================================================ */
#define SSHD_HOST_KEY_PATH     "/etc/ssh_host_key"
#define SSHD_AUTHORIZED_KEYS   "/etc/ssh_authorized_keys"

/* forward decl: logging lives further down; the key bootstrap logs */
static void net_sshd_log(const char *msg);

static u8 g_hk_n[256];
static u8 g_hk_d[256];
static int g_hk_ready = 0;       /* host key loaded/generated */
static int g_hk_custom = 0;      /* 1 = per-installation key (the embedded fallback was removed with BUG-0075) */

/* authorized public keys (moduli only; e is fixed 65537) */
static u8 g_auth_n[8][256];
static int g_auth_count = 0;

/* ---- byte-array helpers (big-endian, fixed width) ---- */
static void be_inc(u8 *a, int len) {
    for (int i = len - 1; i >= 0; i--) if (++a[i] != 0) break;
}
static void be_dec(u8 *a, int len) {
    for (int i = len - 1; i >= 0; i--) if (a[i]-- != 0) break;
}
static int be_is_zero(const u8 *a, int len) {
    for (int i = 0; i < len; i++) if (a[i]) return 0;
    return 1;
}
static int be_is_one(const u8 *a, int len) {
    if (a[len - 1] != 1) return 0;
    for (int i = 0; i < len - 1; i++) if (a[i]) return 0;
    return 1;
}
static int be_bit(const u8 *a, int len, int bit) {
    /* bit 0 = LSB of the last byte */
    if (bit < 0 || bit >= len * 8) return 0;
    return (a[len - 1 - bit / 8] >> (bit % 8)) & 1;
}
static void be_shr1(u8 *a, int len) {
    for (int i = 0; i < len - 1; i++) a[i] = (u8)((a[i] << 1) | (a[i + 1] >> 7));
    a[len - 1] = (u8)(a[len - 1] << 1);
}
static int be_cmp(const u8 *a, const u8 *b, int len) {
    for (int i = 0; i < len; i++) {
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    }
    return 0;
}
/* r = a - b (a >= b), width len */
static void be_sub(u8 *r, const u8 *a, const u8 *b, int len) {
    int borrow = 0;
    for (int i = len - 1; i >= 0; i--) {
        int d = a[i] - b[i] - borrow;
        borrow = d < 0;
        r[i] = (u8)(d & 0xFF);
    }
}
/* r = a + b (mod 2^(8*len)), returns carry */
static int be_add(u8 *r, const u8 *a, const u8 *b, int len) {
    int carry = 0;
    for (int i = len - 1; i >= 0; i--) {
        int d = a[i] + b[i] + carry;
        r[i] = (u8)(d & 0xFF);
        carry = d > 0xFF;
    }
    return carry;
}

/* Miller-Rabin probable-prime test for an odd big-endian candidate.
 * 16 fixed small-prime bases: error probability < 4^-16 per FIPS-style
 * analysis for non-adversarial (CSPRNG-generated) candidates. */
static int rsa_probable_prime(const u8 *p_in, int len) {
    static u8 pm1[128];
    static u8 r[128];
    memcpy(pm1, p_in, len);
    be_inc(pm1, len);                    /* pm1 = p-1 */
    int s = 0;
    memcpy(r, pm1, len);
    while (!be_bit(r, len, 0)) { be_shr1(r, len); s++; }
    static const u8 bases[][2] = {
        {0x00, 0x02}, {0x00, 0x03}, {0x00, 0x05}, {0x00, 0x07},
        {0x00, 0x0B}, {0x00, 0x0D}, {0x00, 0x11}, {0x00, 0x13},
        {0x00, 0x17}, {0x00, 0x1D}, {0x00, 0x1F}, {0x00, 0x25},
        {0x00, 0x29}, {0x00, 0x2B}, {0x00, 0x2F}, {0x00, 0x31},
    };
    u8 y[128];
    u8 two = 2;
    for (int bi = 0; bi < 16; bi++) {
        if (crypto_bn_mod_exp(y, len, bases[bi], 2, r, len, p_in, len) != 0) return 0;
        if (be_is_one(y, len) || memcmp(y, pm1, len) == 0) continue;
        int hit_pm1 = 0;
        for (int t = 1; t < s; t++) {
            if (crypto_bn_mod_exp(y, len, y, len, &two, 1, p_in, len) != 0) return 0;
            if (memcmp(y, pm1, len) == 0) { hit_pm1 = 1; break; }
            if (be_is_one(y, len)) return 0;   /* 1 before the last round: composite */
        }
        if (!hit_pm1) return 0;                /* composite */
    }
    return 1;
}

/* Generate one 1024-bit prime with the top two bits set (so p*q is
 * exactly 2048 bits). Blocks in QEMU for a while (~seconds); callers
 * print a notice. */
static int rsa_gen_prime(u8 *out_be, int len) {
    u8 cand[128];
    for (;;) {
        crypto_random(cand, len);
        cand[0] |= 0xC0;          /* 1024-bit value, n=p*q exactly 2048 bits */
        cand[len - 1] |= 0x01;    /* odd */
        cand[len - 1] &= 0xFE | 0x01;  /* keep odd (no-op, documentation) */
        /* small-prime trial division up to 1000 for speed */
        int small_prime_divides = 0;
        for (int sp = 3; sp <= 1000; sp += 2) {
            /* mod via repeated subtraction is too slow; use 32-bit windows:
             * compute cand mod sp from the byte stream */
            u32 rem = 0;
            for (int i = 0; i < len; i++) rem = (rem * 256 + cand[i]) % (u32)sp;
            if (rem == 0) { small_prime_divides = 1; break; }
        }
        if (small_prime_divides) continue;
        if (rsa_probable_prime(cand, len)) {
            memcpy(out_be, cand, len);
            return 0;
        }
    }
}

/* d = e^{-1} mod phi via binary extended gcd (phi is EVEN, e is odd:
 * use the (odd, even)-safe variant with modular halving). */
static int rsa_modinv_e65537(const u8 *phi_be, int len, u8 *d_out) {
    static u8 u[257], v[257], x1[257], x2[257], t[257];
    memset(u, 0, sizeof(u)); memset(v, 0, sizeof(v));
    memset(x1, 0, sizeof(x1)); memset(x2, 0, sizeof(x2));
    int W = len;                      /* working width (2048-bit phi) */
    /* buffers are len+1 wide: (x + phi) can reach 2^2049 */
    u[W] = 0; /* u = e = 65537 */
    u[W - 1] = 0x01; u[W - 2] = 0x00; u[W - 3] = 0x01;   /* 0x010001 */
    memcpy(v, phi_be, len); v[W] = 0;
    x1[W] = 0; x1[W - 1] = 1;        /* x1 = 1 */
    /* x2 = 0 */
    while (!be_is_zero(u, W) && !be_is_zero(v, W)) {
        while (!be_bit(u, W, 0)) {
            be_shr1(u, W);
            if (!be_bit(x1, W, 0)) be_shr1(x1, W);
            else { /* x1 = (x1 + phi) / 2 */
                be_add(t, x1, phi_be, W);
                be_shr1(t, W);
                memcpy(x1, t, W);
            }
        }
        while (!be_bit(v, W, 0)) {
            be_shr1(v, W);
            if (!be_bit(x2, W, 0)) be_shr1(x2, W);
            else { be_add(t, x2, phi_be, W); be_shr1(t, W); memcpy(x2, t, W); }
        }
        if (be_cmp(u, v, W) >= 0) {
            be_sub(u, u, v, W);
            /* x1 = x1 - x2 mod phi */
            u8 tb[257];
            memcpy(tb, x1, W); tb[W] = 0;
            int borrow = 0;
            for (int i = W - 1; i >= 0; i--) {
                int dd = tb[i] - x2[i] - borrow;
                borrow = dd < 0;
                tb[i] = (u8)(dd & 0xFF);
            }
            if (borrow) be_add(tb, tb, phi_be, W);
            memcpy(x1, tb, W);
        } else {
            be_sub(v, v, u, W);
            u8 tb[257];
            memcpy(tb, x2, W); tb[W] = 0;
            int borrow = 0;
            for (int i = W - 1; i >= 0; i--) {
                int dd = tb[i] - x1[i] - borrow;
                borrow = dd < 0;
                tb[i] = (u8)(dd & 0xFF);
            }
            if (borrow) be_add(tb, tb, phi_be, W);
            memcpy(x2, tb, W);
        }
    }
    /* u == 1 => x1 is the inverse; v == 1 => x2 */
    int one_u = (u[W - 1] == 1 && be_is_zero(u, W - 1));
    int one_v = (v[W - 1] == 1 && be_is_zero(v, W - 1));
    if (one_u) memcpy(d_out, x1, len);
    else if (one_v) memcpy(d_out, x2, len);
    else return -1;
    /* sanity: e*d mod phi == 1 cannot be cheaply verified (even modulus);
     * the handshake itself validates the pair (self-test below). */
    return 0;
}

/* Verify (n, d) is a functional RSA pair: sign a known 20-byte vector
 * with d and recover it with e (crypto_rsa_verify_pkcs1 path). */
static int rsa_pair_selftest(const u8 *n_be, const u8 *d_be) {
    /* s = x^d mod n, x = 0x02 (arbitrary small base); then s^e mod n == x */
    u8 sig[256], rec[256];
    u8 x[1] = { 0x02 };
    if (crypto_bn_mod_exp(sig, 256, x, 1, d_be, 256, n_be, 256) != 0) return 0;
    static const u8 e3[3] = { 0x01, 0x00, 0x01 };
    if (crypto_bn_mod_exp(rec, 256, sig, 256, e3, 3, n_be, 256) != 0) return 0;
    return (rec[255] == 0x02) ? 1 : 0;
}

/* Load /etc/ssh_host_key; generate + persist it if missing. */
static int net_sshd_hostkey_ensure(void) {
    if (g_hk_ready) return 0;
    /* 1. try to load */
    {
        int fd = fs_vfs_open(SSHD_HOST_KEY_PATH, VFS_O_RDONLY);
        if (fd >= 0) {
            u8 buf[512];
            int n = fs_vfs_read(fd, buf, 512);
            fs_vfs_close(fd);
            if (n == 512) {
                memcpy(g_hk_n, buf, 256);
                memcpy(g_hk_d, buf + 256, 256);
                if (rsa_pair_selftest(g_hk_n, g_hk_d)) {
                    g_hk_ready = 1;
                    g_hk_custom = 1;
                    return 0;
                }
                net_sshd_log("host key file corrupt; regenerating");
            }
        }
    }
    /* 2. generate (Miller-Rabin RSA-2048; slow in QEMU, once per disk) */
    net_sshd_log("generating per-installation host key (RSA-2048, one-time)...");
    u8 p_be[128], q_be[128];
    u64 p_word[16], q_word[16], prod_word[32];
    u8 n_be[256], phi_be[256], d_be[256];
    for (;;) {
        if (rsa_gen_prime(p_be, 128) != 0) return -1;
        if (rsa_gen_prime(q_be, 128) != 0) return -1;
        if (memcmp(p_be, q_be, 128) == 0) continue;
        /* n = p*q */
        crypto_bn_from_be(p_word, 16, p_be, 128);
        crypto_bn_from_be(q_word, 16, q_be, 128);
        crypto_bn_mul(prod_word, p_word, q_word, 16);
        crypto_bn_to_be(prod_word, 32, n_be, 256);
        /* phi = (p-1)*(q-1) */
        be_dec(p_be, 128);
        be_dec(q_be, 128);
        crypto_bn_from_be(p_word, 16, p_be, 128);
        crypto_bn_from_be(q_word, 16, q_be, 128);
        crypto_bn_mul(prod_word, p_word, q_word, 16);
        crypto_bn_to_be(prod_word, 32, phi_be, 256);
        /* restore the primes (needed again? no — done) */
        break;
    }
    if (rsa_modinv_e65537(phi_be, 256, d_be) != 0) return -1;
    if (!rsa_pair_selftest(n_be, d_be)) return -1;
    memcpy(g_hk_n, n_be, 256);
    memcpy(g_hk_d, d_be, 256);
    g_hk_ready = 1;
    g_hk_custom = 1;
    /* persist */
    int fd = fs_vfs_open(SSHD_HOST_KEY_PATH, VFS_O_WRONLY | VFS_O_CREAT | VFS_O_TRUNC);
    if (fd >= 0) {
        u8 buf[512];
        memcpy(buf, n_be, 256);
        memcpy(buf + 256, d_be, 256);
        fs_vfs_write(fd, buf, 512);
        fs_vfs_close(fd);
        net_sshd_log("host key generated and saved to " SSHD_HOST_KEY_PATH);
    } else {
        net_sshd_log("host key generated (RAM only: /etc not writable)");
    }
    return 0;
}

/* Load the authorized public key moduli. */
static void net_sshd_authkeys_reload(void) {
    g_auth_count = 0;
    int fd = fs_vfs_open(SSHD_AUTHORIZED_KEYS, VFS_O_RDONLY);
    if (fd < 0) return;   /* no file => publickey auth disabled */
    static u8 buf[4096];
    int n = fs_vfs_read(fd, buf, (int)sizeof(buf) - 1);
    fs_vfs_close(fd);
    if (n <= 0) return;
    buf[n] = 0;
    int p = 0;
    while (p < n && g_auth_count < 8) {
        int e = p;
        while (e < n && buf[e] != '\n') e++;
        buf[e] = 0;
        /* skip comments/blank */
        const char *line = (const char *)buf + p;
        int lp = 0;
        while (line[lp] == ' ' || line[lp] == '\t' || line[lp] == '\r') lp++;
        if (line[lp] != '#' && line[lp] != 0) {
            int hlen = 0;
            while (line[lp + hlen] && line[lp + hlen] != '\r') hlen++;
            if (hlen == 512) {
                int ok = 1;
                for (int i = 0; i < 512; i++) {
                    char c = line[lp + i];
                    int v;
                    if (c >= '0' && c <= '9') v = c - '0';
                    else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
                    else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
                    else { ok = 0; break; }
                    u8 nib = (u8)v;
                    if (i & 1) g_auth_n[g_auth_count][i / 2] |= nib;
                    else g_auth_n[g_auth_count][i / 2] = (u8)(nib << 4);
                }
                if (ok) g_auth_count++;
            }
        }
        p = e + 1;
    }
}

int net_sshd_hostkey_is_custom(void) { return g_hk_custom; }

/* ---- protocol constants (mirror ssh.c) ---- */
#define SSHD_MSG_DISCONNECT        1
#define SSHD_MSG_SERVICE_REQUEST   5
#define SSHD_MSG_SERVICE_ACCEPT    6
#define SSHD_MSG_KEXINIT          20
#define SSHD_MSG_NEWKEYS          21
#define SSHD_MSG_KEXDH_REPLY      31
#define SSHD_MSG_KEXDH_INIT       30
#define SSHD_MSG_USERAUTH_REQUEST 50
#define SSHD_MSG_USERAUTH_FAILURE 51
#define SSHD_MSG_USERAUTH_SUCCESS 52
#define SSHD_MSG_USERAUTH_BANNER  53
#define SSHD_MSG_CHANNEL_OPEN     90
#define SSHD_MSG_CHANNEL_OPEN_CONF 91
#define SSHD_MSG_CHANNEL_OPEN_FAIL 92
#define SSHD_MSG_CHANNEL_WINDOW    93
#define SSHD_MSG_CHANNEL_DATA     94
#define SSHD_MSG_CHANNEL_EOF      96
#define SSHD_MSG_CHANNEL_CLOSE    97
#define SSHD_MSG_CHANNEL_REQUEST  98
#define SSHD_MSG_CHANNEL_SUCCESS  99
#define SSHD_MSG_CHANNEL_FAILURE 100

/* RFC 4253 §11.1 disconnect reason codes used by this server */
#define SSHD_DISCONNECT_SERVICE_NOT_AVAILABLE   7
#define SSHD_DISCONNECT_TOO_MANY_CONNECTIONS   12
#define SSHD_DISCONNECT_NO_MORE_AUTH_METHODS   14

/* BUG-0222 (A14-36): authentication attempt budget per connection */
#define SSHD_AUTH_MAX_ATTEMPTS 6

/* BUG-0224 (A14-39): payload capacity of one page frame (all packet
 * payload buffers are heap frames, never stack arrays) */
#define SSHD_PKT_CAP 4096

/* BUG-0220 (A14-34): strict-kex tokens (RFC 9144). The server advertises
 * ..._s_..., the client ..._c_...; each side looks for the PEER's token. */
#define SSHD_KEX_STRICT_CLIENT "kex-strict-c-v00@openssh.com"
#define SSHD_KEX_STRICT_SERVER "kex-strict-s-v00@openssh.com"

#define SSHD_DH_BYTES 256
static const char *SSHD_BANNER = "SSH-2.0-OpenCubeOS_sshd_WP-09\r\n";

typedef struct {
    int sock;                       /* TCP socket fd for the accepted conn */
    /* strings + payloads for H */
    char client_banner[128];
    char server_banner[64];
    u8  client_kexinit[4096];
    int client_kexinit_len;
    u8  server_kexinit[1024];
    int server_kexinit_len;
    /* negotiated algorithms */
    int kex_curve25519;             /* 1 = curve25519-sha256 */
    int cipher_ctr;                 /* 1 = aes128-ctr */
    int kex_strict;                 /* 1 = strict-kex agreed (RFC 9144, BUG-0220) */
    /* DH */
    u8  crypto_x25519_priv[32];
    u8  crypto_dh_priv[SSHD_DH_BYTES];
    u8  crypto_dh_pub[SSHD_DH_BYTES];      /* f = g^y mod p */
    u8  client_pub[SSHD_DH_BYTES];  /* e */
    u8  shared_secret[SSHD_DH_BYTES];
    u8  exchange_hash[32];
    u8  session_id[32];
    int session_id_set;
    /* direction keys (server视角: C=client-to-server = incoming, S = outgoing) */
    u8  iv_in[16], iv_in_next[16];       /* A: c2s */
    u8  iv_out[16], iv_out_next[16];     /* B: s2c */
    u8  ctr_in[16], ctr_out[16];         /* aes128-ctr rolling counters */
    u8  enc_in[16];                      /* C */
    u8  enc_out[16];                     /* D */
    u8  mac_in[32];                      /* E */
    u8  mac_out[32];                     /* F */
    u32 write_seq, read_seq;
    int encrypted;
    /* auth */
    char auth_user[32];
    char auth_pass[32];
    /* channel */
    u32 peer_channel;               /* client's channel id */
    u32 our_channel;                /* our channel id */
    int channel_open;
    /* exec capture */
    u8  exec_out[4096];
    int exec_len;
    int exec_active;
} net_sshd_ctx_t;

static net_sshd_ctx_t g_ssd;

/* BUG-0223 (A14-37): the CONFIGURED credentials get their own store.
 * ctx IS the singleton g_ssd and ctx->auth_user is overwritten by every
 * client attempt, so the old self-compare was vacuously true AND clobbered
 * the configured username on the first attempt. Bounded copy: the fixed
 * 32-byte constant-time compare below is then always in-bounds. */
static char g_sshd_cfg_user[32];
static char g_sshd_cfg_pass[32];

/* BUG-0226 (A14-40): listener fd while sshd owns it; the session loops
 * use it to probe for (and refuse) extra queued connections. */
static int g_sshd_listen_fd = -1;

static void net_sshd_probe_extra_connection(void);                 /* BUG-0226 */
static void net_sshd_send_disconnect(net_sshd_ctx_t *ctx, u32 reason, const char *desc); /* BUG-0222/0226 */

static void net_sshd_write_u32(u8 *buf, int *p, u32 v);

static void net_sshd_log(const char *msg) {
    screen_console_puts("[sshd] ");
    screen_console_puts(msg);
    screen_console_puts("\n");
}

static void net_sshd_log_hex(const char *prefix, const u8 *buf, int n) {
    char buf2[160];
    strcpy(buf2, "[sshd] ");
    strcat(buf2, prefix);
    char hex[8];
    for (int i = 0; i < n && (int)strlen(buf2) < 130; i++) {
        u64_to_hex(buf[i], hex, 2);
        strcat(buf2, hex);
    }
    screen_console_puts(buf2);
    screen_console_puts("\n");
}

/* BUG-0221 (A14-35): constant-time all-zero test (RFC 7748 S6.1) - OR all
 * bytes into one accumulator and branch only on the final value. */
static int net_sshd_secret_is_zero(const u8 *buf, int len) {
    u8 acc = 0;
    for (int i = 0; i < len; i++) acc |= buf[i];
    return acc == 0;
}

/* BUG-0223 (A14-37): constant-time string equality over a FIXED window.
 * Both buffers are NUL-padded char[32] stores, so comparing all 32 bytes
 * (XOR-accumulate, no early exit, no length leak) decides equality of the
 * strings. Timing is independent of where (or whether) they differ. */
static int net_sshd_ct_eq(const char *a, const char *b, int cap) {
    u8 diff = 0;
    for (int i = 0; i < cap; i++) diff |= (u8)a[i] ^ (u8)b[i];
    return diff == 0;
}

/* BUG-0220 (A14-34): strict-kex (RFC 9144) - when the extension was
 * negotiated, both directions' sequence numbers restart at 0 immediately
 * after the final NEWKEYS of the initial AND every rekey exchange. */
static void net_sshd_reset_seq(net_sshd_ctx_t *ctx) {
    if (!ctx->kex_strict) return;
    ctx->write_seq = 0;
    ctx->read_seq = 0;
    net_sshd_log("strict-kex: sequence numbers reset to 0");
}

/* ---------- raw (unencrypted) packet framing ---------- */

static int net_sshd_send_packet_unencrypted(net_sshd_ctx_t *ctx, u8 msg_type, const u8 *payload, int payload_len) {
    int pad_len = 8 - ((6 + payload_len) % 8);
    if (pad_len < 4) pad_len += 8;
    u8 *pkt = (u8 *)(uintptr_t)mem_pmm_alloc_frame();
    if (!pkt) { net_sshd_log("send_unenc: pmm_alloc_frame failed"); return -1; }
    int pkt_len = 1 + 1 + payload_len + pad_len;
    pkt[0] = (u8)(pkt_len >> 24);
    pkt[1] = (u8)(pkt_len >> 16);
    pkt[2] = (u8)(pkt_len >> 8);
    pkt[3] = (u8)(pkt_len & 0xFF);
    pkt[4] = (u8)pad_len;
    pkt[5] = msg_type;
    if (payload_len > 0) memcpy(pkt + 6, payload, payload_len);
    crypto_random(pkt + 6 + payload_len, pad_len);
    int rc = net_send(ctx->sock, pkt, 4 + pkt_len);
    mem_pmm_free_frame((u64)(uintptr_t)pkt);
    if (rc >= 0) ctx->write_seq++;
    return rc;
}

static int net_sshd_recv_packet_unencrypted(net_sshd_ctx_t *ctx, u8 *msg_type, u8 *payload, int *payload_len) {
    u8 len_buf[4];
    int n = 0;
    u64 start = core_timer_ticks();
    while (n < 4) {
        int got = net_recv(ctx->sock, len_buf + n, 4 - n);
        if (got > 0) { n += got; start = core_timer_ticks(); }
        else {
            net_poll();
            if (core_timer_ticks() - start > 3000) {
                net_sshd_log("recv_unenc: timeout reading 4-byte length");
                return -1;
            }
        }
    }
    int pkt_len = ((int)len_buf[0] << 24) | ((int)len_buf[1] << 16) |
                  ((int)len_buf[2] << 8) | len_buf[3];
    /* P0fix1 BUG-0014 (A14-08): the body is ONE 4096B page frame; the old
     * limit of 35000 let ANY pre-auth network client write ~31KB past it. */
    if (pkt_len < 1 || pkt_len > 3500) {
        char b[64]; strcpy(b, "recv_unenc: bad pkt_len=");
        char n2[10]; u64_to_str((u64)pkt_len, n2); strcat(b, n2);
        net_sshd_log(b);
        return -1;
    }
    u8 *body = (u8 *)(uintptr_t)mem_pmm_alloc_frame();
    if (!body) return -1;
    int got = 0;
    while (got < pkt_len) {
        int r = net_recv(ctx->sock, body + got, pkt_len - got);
        if (r > 0) { got += r; start = core_timer_ticks(); }
        else {
            net_poll();
            if (core_timer_ticks() - start > 3000) {
                char b[64]; strcpy(b, "recv_unenc: body timeout, got ");
                char n2[10]; u64_to_str((u64)got, n2); strcat(b, n2);
                strcat(b, "/");
                u64_to_str((u64)pkt_len, n2); strcat(b, n2);
                net_sshd_log(b);
                mem_pmm_free_frame((u64)(uintptr_t)body);
                return -1;
            }
        }
    }
    int pad_len = body[0];
    *msg_type = body[1];
    {
        char b[80]; strcpy(b, "recv_unenc: pkt_len=");
        char n2[10]; u64_to_str((u64)pkt_len, n2); strcat(b, n2);
        strcat(b, " type="); u64_to_str((u64)*msg_type, n2); strcat(b, n2);
        strcat(b, " first8=");
        for (int i = 0; i < 4; i++) { char hx[4]; u64_to_hex(body[i], hx, 2); strcat(b, hx); }
        net_sshd_log(b);
    }
    int plen = pkt_len - 1 - pad_len - 1;
    if (plen < 0) plen = 0;
    if (plen > *payload_len) plen = *payload_len;
    memcpy(payload, body + 2, plen);
    *payload_len = plen;
    mem_pmm_free_frame((u64)(uintptr_t)body);
    ctx->read_seq++;
    return 0;
}

/* ---------- encrypted packet framing (server side) ---------- */

static int net_sshd_send_packet_encrypted(net_sshd_ctx_t *ctx, u8 msg_type, const u8 *payload, int payload_len) {
    int block_size = 16, mac_size = 32;
    /* WP-09 fix: the old formula used (5 + payload_len) and packet_length =
 * 1 + payload_len + pad_block, which declared one more padding byte than
 * was actually written. Peers stripped the declared padding and ate the
 * last payload byte (SERVICE_ACCEPT arrived as "ssh-useraut " on the
 * wire). Correct layout: body = pad_len(1) + msg_type(1) + payload(N)
 * + padding(P), total_unenc = 4 + body, and 4 + body must be a multiple
 * of block_size. */
    int pad_block = block_size - ((6 + payload_len) % block_size);
    if (pad_block < 4) pad_block += block_size;
    int packet_length = 2 + payload_len + pad_block;
    int total_unenc = 4 + packet_length;
    int total_send = total_unenc + mac_size;

    static u8 pkt[16384];
    if (total_send > (int)sizeof(pkt)) return -1;
    pkt[0] = (u8)(packet_length >> 24);
    pkt[1] = (u8)(packet_length >> 16);
    pkt[2] = (u8)(packet_length >> 8);
    pkt[3] = (u8)(packet_length & 0xFF);
    pkt[4] = (u8)pad_block;
    pkt[5] = msg_type;
    if (payload_len > 0) memcpy(pkt + 6, payload, payload_len);
    crypto_random(pkt + 6 + payload_len, pad_block);

    {
        char b[128]; strcpy(b, "[sshd] SEND pkt: ");
        char hx[4];
        for (int i = 0; i < 32 && i < total_unenc; i++) {
            u64_to_hex(pkt[i], hx, 2);
            strcat(b, hx);
        }
        strcat(b, " total_unenc=");
        char n2[12]; u64_to_str((u64)total_unenc, n2); strcat(b, n2);
        net_sshd_log(b);
    }

    /* MAC over seq || unencrypted packet */
    static u8 mac_input[16384];
    int mi = 0;
    mac_input[mi++] = (u8)(ctx->write_seq >> 24);
    mac_input[mi++] = (u8)(ctx->write_seq >> 16);
    mac_input[mi++] = (u8)(ctx->write_seq >> 8);
    mac_input[mi++] = (u8)(ctx->write_seq & 0xFF);
    if (total_unenc > (int)sizeof(mac_input) - 4) return -1;
    memcpy(mac_input + mi, pkt, total_unenc); mi += total_unenc;
    u8 mac[32];
    crypto_hmac_sha256(ctx->mac_out, 32, mac_input, mi, mac);

    static u8 enc[16384];
    if (total_unenc > (int)sizeof(enc)) return -1;
    if (ctx->cipher_ctr) {
        crypto_aes128_ctr_encrypt(ctx->enc_out, ctx->ctr_out, pkt, total_unenc, enc);
        for (int blk = 0; blk < total_unenc / 16; blk++)
            for (int ci = 15; ci >= 0; ci--)
                if (++ctx->ctr_out[ci] != 0) break;
    } else {
        crypto_aes128_cbc_encrypt(ctx->enc_out, ctx->iv_out_next, pkt, total_unenc, enc);
        memcpy(ctx->iv_out_next, enc + total_unenc - 16, 16);  /* CBC chain */
    }

    static u8 out[16384];
    memcpy(out, enc, total_unenc);
    memcpy(out + total_unenc, mac, mac_size);
    int rc = net_send(ctx->sock, out, total_send);
    if (rc >= 0) ctx->write_seq++;
    return rc;
}

static int net_sshd_recv_packet_encrypted(net_sshd_ctx_t *ctx, u8 *msg_type, u8 *payload, int *payload_len) {
    int block_size = 16, mac_size = 32;
    u8 first_block[16];
    int n = 0;
    u64 start = core_timer_ticks();
    while (n < block_size) {
        int got = net_recv(ctx->sock, first_block + n, block_size - n);
        if (got > 0) { n += got; start = core_timer_ticks(); }
        else {
            net_poll();
            if (core_timer_ticks() - start > 3000) return -1;
        }
    }
    u8 dec_first[16];
    if (ctx->cipher_ctr) {
        crypto_aes128_ctr_encrypt(ctx->enc_in, ctx->ctr_in, first_block, block_size, dec_first);
        for (int ci = 15; ci >= 0; ci--)
            if (++ctx->ctr_in[ci] != 0) break;
    } else {
        crypto_aes128_cbc_decrypt(ctx->enc_in, ctx->iv_in_next, first_block, block_size, dec_first);
    }
    int packet_length = ((int)dec_first[0] << 24) | ((int)dec_first[1] << 16) |
                        ((int)dec_first[2] << 8) | dec_first[3];
    {
        char b[64]; strcpy(b, "[sshd] recv_enc: pkt_len=");
        char n2[12]; u64_to_str((u64)packet_length, n2); strcat(b, n2);
        net_sshd_log(b);
    }
    if (packet_length < 1 || packet_length > 3500) {
        char b[64]; strcpy(b, "recv: bad packet_length=");
        char n[10]; u64_to_str((u64)packet_length, n); strcat(b, n);
        net_sshd_log(b);
        return -1;
    }
    int leftover = block_size - 4;
    int remaining = packet_length - leftover;
    if (remaining < 0 || remaining % block_size != 0) {
        char b[64]; strcpy(b, "recv: bad blocking, remaining=");
        char n[10]; u64_to_str((u64)remaining, n); strcat(b, n);
        net_sshd_log(b);
        return -1;
    }

    static u8 rest_buf[16384];
    /* P0fix1 BUG-0018 (A14-12): validate every buffer BEFORE receiving or
     * decrypting. The old code had NO body_len check at all (the client
     * side checks too late), so a 35000-byte packet overflowed rest_buf,
     * dec_rest, body AND mac_input. */
    if (remaining + mac_size > (int)sizeof(rest_buf)) return -1;
    n = 0;
    while (n < remaining + mac_size) {
        int r = net_recv(ctx->sock, rest_buf + n, (remaining + mac_size) - n);
        if (r > 0) { n += r; start = core_timer_ticks(); }
        else {
            net_poll();
            if (core_timer_ticks() - start > 3000) return -1;
        }
    }
    static u8 dec_rest[16384];
    if (ctx->cipher_ctr) {
        if (remaining > 0) {
            crypto_aes128_ctr_encrypt(ctx->enc_in, ctx->ctr_in, rest_buf, remaining, dec_rest);
            for (int blk = 0; blk < remaining / 16; blk++)
                for (int ci = 15; ci >= 0; ci--)
                    if (++ctx->ctr_in[ci] != 0) break;
        }
    } else {
        if (remaining > 0) {
            crypto_aes128_cbc_decrypt(ctx->enc_in, first_block, rest_buf, remaining, dec_rest);
        }
        if (remaining >= 16) memcpy(ctx->iv_in_next, rest_buf + remaining - 16, 16);
        else memcpy(ctx->iv_in_next, first_block, 16);
    }

    static u8 body[16384];
    int body_len = leftover + remaining;
    /* P0fix1 BUG-0018 (A14-12): bounds-check before copying. mac_input
     * (declared below) is also 16384B and receives 8 + body_len bytes. */
    if (body_len > (int)sizeof(body) || 8 + body_len > 16384) return -1;
    memcpy(body, dec_first + 4, leftover);
    if (remaining > 0) memcpy(body + leftover, dec_rest, remaining);

    static u8 mac_input[16384];
    int mi = 0;
    mac_input[mi++] = (u8)(ctx->read_seq >> 24);
    mac_input[mi++] = (u8)(ctx->read_seq >> 16);
    mac_input[mi++] = (u8)(ctx->read_seq >> 8);
    mac_input[mi++] = (u8)(ctx->read_seq & 0xFF);
    mac_input[mi++] = (u8)(packet_length >> 24);
    mac_input[mi++] = (u8)(packet_length >> 16);
    mac_input[mi++] = (u8)(packet_length >> 8);
    mac_input[mi++] = (u8)(packet_length & 0xFF);
    memcpy(mac_input + mi, body, body_len); mi += body_len;
    u8 expected_mac[32];
    crypto_hmac_sha256(ctx->mac_in, 32, mac_input, mi, expected_mac);
    for (int i = 0; i < mac_size; i++) {
        if (rest_buf[remaining + i] != expected_mac[i]) {
            net_sshd_log("MAC verify FAIL on incoming packet");
            net_sshd_log_hex("  expected MAC (first 8): ", expected_mac, 8);
            net_sshd_log_hex("  received  MAC (first 8): ", rest_buf + remaining, 8);
            return -1;
        }
    }

    int pad_len = body[0];
    *msg_type = body[1];
    {
        char b[80]; strcpy(b, "[sshd] recv_enc OK: type=");
        char n2[12]; u64_to_str((u64)*msg_type, n2); strcat(b, n2);
        strcat(b, " plen="); u64_to_str((u64)(body_len - 2 - pad_len), n2); strcat(b, n2);
        net_sshd_log(b);
    }
    int plen = body_len - 1 - pad_len - 1;
    if (plen < 0) plen = 0;
    if (plen > *payload_len) plen = *payload_len;
    memcpy(payload, body + 2, plen);
    *payload_len = plen;
    ctx->read_seq++;
    return 0;
}

/* ---------- mpint helpers ---------- */

static int net_sshd_write_mpint(u8 *buf, int *p, const u8 *val, int val_len) {
    int start = 0;
    while (start < val_len - 1 && val[start] == 0) start++;
    int n = val_len - start;
    int need_zero = (val[start] & 0x80) ? 1 : 0;
    int total = n + need_zero;
    int final_len = total;
    if (final_len < 1) final_len = 1;
    buf[(*p)++] = (u8)(final_len >> 24);
    buf[(*p)++] = (u8)(final_len >> 16);
    buf[(*p)++] = (u8)(final_len >> 8);
    buf[(*p)++] = (u8)(final_len & 0xFF);
    for (int i = 0; i < need_zero; i++) buf[(*p)++] = 0;
    memcpy(buf + *p, val + start, n);
    *p += n;
    return 4 + total;
}

static void net_sshd_write_str(u8 *buf, int *p, const void *data, int len) {
    buf[(*p)++] = (u8)(len >> 24);
    buf[(*p)++] = (u8)(len >> 16);
    buf[(*p)++] = (u8)(len >> 8);
    buf[(*p)++] = (u8)(len & 0xFF);
    memcpy(buf + *p, data, len);
    *p += len;
}

static void net_sshd_write_cstr(u8 *buf, int *p, const char *s) {
    int len = (int)strlen(s);
    net_sshd_write_str(buf, p, s, len);
}

static u32 net_sshd_read_u32(const u8 *b) {
    return ((u32)b[0] << 24) | ((u32)b[1] << 16) | ((u32)b[2] << 8) | (u32)b[3];
}

/* ---------- key derivation (server directions) ---------- */

static void net_sshd_derive_keys(net_sshd_ctx_t *ctx) {
    /* K_mpint */
    int start = 0;
    while (start < SSHD_DH_BYTES - 1 && ctx->shared_secret[start] == 0) start++;
    int n = SSHD_DH_BYTES - start;
    int need_zero = (ctx->shared_secret[start] & 0x80) ? 1 : 0;
    int k_total = n + need_zero;
    u8 k_mpint[264];
    k_mpint[0] = (u8)(k_total >> 24);
    k_mpint[1] = (u8)(k_total >> 16);
    k_mpint[2] = (u8)(k_total >> 8);
    k_mpint[3] = (u8)(k_total & 0xFF);
    if (need_zero) k_mpint[4] = 0;
    memcpy(k_mpint + 4 + need_zero, ctx->shared_secret + start, n);
    int k_mpint_len = 4 + k_total;

    u8 msg[512];
    u8 digest[32];
#define SD_COMPUTE_KEY(X_char, out_buf, out_len) do { \
        int mp = 0; \
        memcpy(msg + mp, k_mpint, k_mpint_len); mp += k_mpint_len; \
        memcpy(msg + mp, ctx->exchange_hash, 32); mp += 32; \
        msg[mp++] = X_char; \
        memcpy(msg + mp, ctx->session_id, 32); mp += 32; \
        sha256(msg, mp, digest); \
        memcpy(out_buf, digest, out_len); \
    } while (0)
    SD_COMPUTE_KEY('A', ctx->iv_in, 16);
    SD_COMPUTE_KEY('B', ctx->iv_out, 16);
    SD_COMPUTE_KEY('C', ctx->enc_in, 16);
    SD_COMPUTE_KEY('D', ctx->enc_out, 16);
    SD_COMPUTE_KEY('E', ctx->mac_in, 32);
    SD_COMPUTE_KEY('F', ctx->mac_out, 32);
#undef SD_COMPUTE_KEY
    memcpy(ctx->iv_in_next, ctx->iv_in, 16);
    memcpy(ctx->ctr_in, ctx->iv_in, 16);
    memcpy(ctx->ctr_out, ctx->iv_out, 16);
    memcpy(ctx->iv_out_next, ctx->iv_out, 16);
}

/* ---------- exchange hash ---------- */

/* P0fix1 BUG-0017 (A14-11): compute the exact worst-case input size BEFORE
 * writing. client_kexinit_len can be ~4096 on its own (pre-auth), and the
 * old code concatenated everything into one 4096B page with no checks
 * (banner + kexinits + K_S + mpints ~5.5KB -> ~1.4KB past the page).
 * Returns 0, or -1 when the input does not fit (caller aborts the session). */
static int net_sshd_compute_hash(net_sshd_ctx_t *ctx, u8 hash[32]) {
    const int cap = 4096;
    int need = 0;
    need += 4 + (int)strlen(ctx->client_banner);
    need += 4 + (int)strlen(ctx->server_banner);
    need += 4 + ctx->client_kexinit_len;
    need += 4 + ctx->server_kexinit_len;
    need += 4 + 276;                       /* K_S block (<= 400B, max 276 used) */
    if (ctx->kex_curve25519)
        need += (4 + 32) + (4 + 32) + (4 + 1 + SSHD_DH_BYTES);
    else
        need += 3 * (4 + 1 + SSHD_DH_BYTES);
    if (need > cap) return -1;

    u8 *buf = (u8 *)(uintptr_t)mem_pmm_alloc_frame();
    if (!buf) return -1;
    int p = 0;
    /* client = V_C (cookie in I_C etc); we are V_S */
    net_sshd_write_cstr(buf, &p, ctx->client_banner);
    net_sshd_write_cstr(buf, &p, ctx->server_banner);
    net_sshd_write_str(buf, &p, ctx->client_kexinit, ctx->client_kexinit_len);
    net_sshd_write_str(buf, &p, ctx->server_kexinit, ctx->server_kexinit_len);

    /* K_S: ssh-rsa host key blob = string "ssh-rsa" + mpint e + mpint N */
    {
        u8 ks[400];
        int kp = 0;
        net_sshd_write_cstr(ks, &kp, "ssh-rsa");
        u8 em[3] = {0x01, 0x00, 0x01};  /* e = 65537 */
        net_sshd_write_mpint(ks, &kp, em, 3);
        net_sshd_write_mpint(ks, &kp, g_hk_n, 256);
        net_sshd_write_str(buf, &p, ks, kp);
    }

    if (ctx->kex_curve25519) {
        /* RFC 8731: e and f are 32-byte strings in the exchange hash */
        net_sshd_write_str(buf, &p, ctx->client_pub + SSHD_DH_BYTES - 32, 32);
        u8 f_pub[32];
        crypto_x25519_public(ctx->crypto_x25519_priv, f_pub);
        net_sshd_write_str(buf, &p, f_pub, 32);
        net_sshd_write_mpint(buf, &p, ctx->shared_secret, SSHD_DH_BYTES);
    } else {
        net_sshd_write_mpint(buf, &p, ctx->client_pub, SSHD_DH_BYTES);  /* e */
        net_sshd_write_mpint(buf, &p, ctx->crypto_dh_pub, SSHD_DH_BYTES);      /* f */
        net_sshd_write_mpint(buf, &p, ctx->shared_secret, SSHD_DH_BYTES); /* K */
    }
    sha256(buf, p, hash);
    mem_pmm_free_frame((u64)(uintptr_t)buf);
    return 0;
}

/* ---------- exec engine ---------- */

static void net_sshd_run_exec(net_sshd_ctx_t *ctx, const char *cmd) {
    net_sshd_log("exec: running command via shell_execute_captured()");
    ctx->exec_len = 0;
    /* WP-09 fix: use the shell's own capture mechanism. The old code swapped
     * the boot-time console hook (the serial mirror) for a private capture
     * hook and then set the hook to NULL afterwards, which permanently
     * silenced serial output: the kernel kept running (CHANNEL_DATA was sent
     * and the client received the output) but every later log line and the
     * shell prompt vanished from the serial console, so the test harness saw
     * a "hang". shell_execute_captured() chains to the previous hook and
     * restores it when done. */
    shell_execute_captured(cmd, (char *)ctx->exec_out, (int)sizeof(ctx->exec_out) - 1);
    ctx->exec_out[sizeof(ctx->exec_out) - 1] = 0;
    ctx->exec_len = (int)strlen((const char *)ctx->exec_out);
    ctx->exec_active = 0;
    char b[64];
    strcpy(b, "exec: captured ");
    char n[10];
    u64_to_str((u64)ctx->exec_len, n);
    strcat(b, n);
    strcat(b, " bytes of output");
    net_sshd_log(b);
}

/* ---------- protocol phases ---------- */

static int net_sshd_send_version(net_sshd_ctx_t *ctx) {
    int len = (int)strlen(SSHD_BANNER);
    int rc = net_send(ctx->sock, SSHD_BANNER, len);
    strcpy(ctx->server_banner, "SSH-2.0-OpenCubeOS_sshd_WP-09");
    return (rc == len) ? 0 : -1;
}

static int net_sshd_recv_version(net_sshd_ctx_t *ctx) {
    int n = 0;
    u64 start = core_timer_ticks();
    while (n < (int)sizeof(ctx->client_banner) - 1) {
        char ch;
        int r = net_recv(ctx->sock, &ch, 1);
        if (r > 0) {
            ctx->client_banner[n++] = ch;
            if (n >= 2 && ctx->client_banner[n-2] == '\r' && ctx->client_banner[n-1] == '\n') break;
            start = core_timer_ticks();
        } else {
            net_poll();
            if (core_timer_ticks() - start > 3000) return -1;
        }
    }
    ctx->client_banner[n] = 0;
    /* strip CRLF for H */
    if (n >= 2) ctx->client_banner[n-2] = 0;
    else if (n >= 1) ctx->client_banner[n-1] = 0;
    net_sshd_log_hex("client banner: ", (const u8 *)ctx->client_banner,
               (int)strlen(ctx->client_banner) > 16 ? 16 : (int)strlen(ctx->client_banner));
    return 0;
}

static const char *SSHD_KEX_KEXALGOS = "curve25519-sha256,curve25519-sha256@libssh.org,diffie-hellman-group14-sha256,kex-strict-s-v00@openssh.com";
static const char *SSHD_KEX_HOSTKEYS = "rsa-sha2-256";
static const char *SSHD_KEX_CIPHERS  = "aes128-ctr,aes128-cbc";
static const char *SSHD_KEX_MACS     = "hmac-sha2-256";
static const char *SSHD_KEX_COMP     = "none";

static int net_sshd_send_kexinit(net_sshd_ctx_t *ctx, int encrypted) {
    u8 cookie[16];
    crypto_random(cookie, 16);
    u8 payload[1024];
    int p = 0;
    memcpy(payload + p, cookie, 16); p += 16;
    net_sshd_write_cstr(payload, &p, SSHD_KEX_KEXALGOS);
    net_sshd_write_cstr(payload, &p, SSHD_KEX_HOSTKEYS);
    net_sshd_write_cstr(payload, &p, SSHD_KEX_CIPHERS);
    net_sshd_write_cstr(payload, &p, SSHD_KEX_CIPHERS);
    net_sshd_write_cstr(payload, &p, SSHD_KEX_MACS);
    net_sshd_write_cstr(payload, &p, SSHD_KEX_MACS);
    net_sshd_write_cstr(payload, &p, SSHD_KEX_COMP);
    net_sshd_write_cstr(payload, &p, SSHD_KEX_COMP);
    net_sshd_write_cstr(payload, &p, "");   /* languages client->server */
    net_sshd_write_cstr(payload, &p, "");   /* languages server->client */
    payload[p++] = 0;                  /* first_kex_packet_follows */
    payload[p++] = 0; payload[p++] = 0; payload[p++] = 0; payload[p++] = 0; /* reserved */
    /* save for H (payload starts at msg_type in the wire; for H we include msg_type) */
    u8 wire[1100];
    wire[0] = SSHD_MSG_KEXINIT;
    memcpy(wire + 1, payload, p);
    memcpy(ctx->server_kexinit, wire, p + 1);
    ctx->server_kexinit_len = p + 1;
    /* BUG-0219: during a rekey this exchange runs under the currently
     * active keys, so the framing depends on the session phase. */
    if (encrypted)
        return net_sshd_send_packet_encrypted(ctx, SSHD_MSG_KEXINIT, payload, p);
    return net_sshd_send_packet_unencrypted(ctx, SSHD_MSG_KEXINIT, payload, p);
}

/* Check whether a comma-separated SSH name-list contains `name`
 * (exact member match, RFC 4251 S6 name-list semantics). */
static int net_sshd_name_has(const u8 *list, int list_len, const char *name) {
    int nlen = (int)strlen(name);
    for (int i = 0; i + nlen <= list_len; i++) {
        int m = 1;
        for (int j = 0; j < nlen; j++) {
            if (list[i + j] != (u8)name[j]) { m = 0; break; }
        }
        if (m && (i + nlen == list_len || list[i + nlen] == (u8)',')) return 1;
    }
    return 0;
}

/* Save the client KEXINIT (msg_type included, matching paramiko's I_C)
 * and negotiate algorithms. BUG-0224 (A14-39): split from recv_kexinit so
 * the rekey responder (BUG-0219) can reuse it on a payload already
 * received; the old recv path also staged the payload through a second
 * 4 KiB stack array (wire[]), which is gone - the frame is copied
 * straight into ctx->client_kexinit. */
static int net_sshd_parse_kexinit(net_sshd_ctx_t *ctx, const u8 *payload, int plen) {
    if (plen < 0 || 1 + plen > (int)sizeof(ctx->client_kexinit)) return -2;
    ctx->client_kexinit[0] = SSHD_MSG_KEXINIT;
    memcpy(ctx->client_kexinit + 1, payload, plen);
    ctx->client_kexinit_len = plen + 1;
    /* Negotiate: pick our first-listed algorithm that the client offers.
     * Client KEXINIT payload (after msg_type): cookie(16) + 10 name-lists. */
    ctx->kex_curve25519 = 0;
    ctx->cipher_ctr = 0;
    {
        int q = 16;
        int lens[8] = {0};
        const u8 *ptrs[8] = {0};
        for (int li = 0; li < 8; li++) {
            if (q + 4 > plen) break;
            int L = (int)net_sshd_read_u32(payload + q);
            q += 4;
            /* P0fix1 BUG-0016 (A14-10): `q + L` overflowed for L near 2^31
             * and left a negative q -> wild-address reads on the next
             * iteration and in name_has(). Subtract instead of add. */
            if (L < 0 || L > plen - q) break;
            ptrs[li] = payload + q;
            lens[li] = L;
            q += L;
        }
        /* kex: accept either RFC 8731 spelling (comma-bounded) */
        if (net_sshd_name_has(ptrs[0], lens[0], "curve25519-sha256") ||
            net_sshd_name_has(ptrs[0], lens[0], "curve25519-sha256@libssh.org")) {
            ctx->kex_curve25519 = 1;
        }
        /* BUG-0220 (A14-34): strict-kex (RFC 9144). The CLIENT advertises
         * kex-strict-c-v00@openssh.com; when present in the client's list,
         * both sides reset both sequence numbers after every NEWKEYS. */
        ctx->kex_strict = net_sshd_name_has(ptrs[0], lens[0], SSHD_KEX_STRICT_CLIENT);
        /* cipher: aes128-ctr in both directions */
        int c2s = net_sshd_name_has(ptrs[2], lens[2], "aes128-ctr");
        int s2c = net_sshd_name_has(ptrs[3], lens[3], "aes128-ctr");
        ctx->cipher_ctr = (c2s && s2c) ? 1 : 0;
        char nb[64];
        strcpy(nb, ctx->kex_curve25519 ?
                  "negotiated KEX: curve25519-sha256" :
                  "negotiated KEX: diffie-hellman-group14-sha256");
        net_sshd_log(nb);
        {
            char db[64];
            strcpy(db, "kex list len: ");
            char n2[10]; u64_to_str((u64)lens[0], n2); strcat(db, n2);
            net_sshd_log(db);
            net_sshd_log_hex("kex list head: ", ptrs[0], lens[0] > 32 ? 32 : lens[0]);
        }
        strcpy(nb, ctx->cipher_ctr ? "negotiated cipher: aes128-ctr" :
                    "negotiated cipher: aes128-cbc");
        net_sshd_log(nb);
    }
    return 0;
}

static int net_sshd_recv_kexinit(net_sshd_ctx_t *ctx) {
    /* BUG-0224 (A14-39): the 4 KiB payload moved from the stack to a page
     * frame (freed on every exit; leak-free error paths). */
    u8 *payload = (u8 *)(uintptr_t)mem_pmm_alloc_frame();
    if (!payload) { net_sshd_log("recv_kexinit: no frame for payload"); return -1; }
    u8 rtype;
    int plen = SSHD_PKT_CAP;
    int rc = net_sshd_recv_packet_unencrypted(ctx, &rtype, payload, &plen);
    if (rc == 0 && rtype != SSHD_MSG_KEXINIT) rc = -2;
    if (rc == 0) rc = net_sshd_parse_kexinit(ctx, payload, plen);
    mem_pmm_free_frame((u64)(uintptr_t)payload);
    return rc;
}

static int net_sshd_send_kexdh_reply_curve25519(net_sshd_ctx_t *ctx, int encrypted) {
    /* server x25519: keypair, K = X25519(y, e), reply K_S || string f || sig */
    crypto_random(ctx->crypto_x25519_priv, 32);
    u8 f_pub[32];
    crypto_x25519_public(ctx->crypto_x25519_priv, f_pub);
    u8 shared32[32];
    if (crypto_x25519_shared(ctx->crypto_x25519_priv, ctx->client_pub + SSHD_DH_BYTES - 32, shared32) != 0)
        return -1;
    if (net_sshd_secret_is_zero(shared32, 32)) {
        /* BUG-0221 (A14-35): RFC 7748 §6.1 - an all-zero X25519 output means
         * the client's public key was invalid (small-order point); the
         * resulting session keys would be attacker-derivable. Abort KEX. */
        net_sshd_log("curve25519 shared secret is all-zero (invalid client key)");
        return -1;
    }
    memset(ctx->shared_secret, 0, SSHD_DH_BYTES);
    memcpy(ctx->shared_secret + SSHD_DH_BYTES - 32, shared32, 32);
    net_sshd_log("curve25519 KEX: shared secret computed");

    /* exchange hash H = SHA256(V_C||V_S||I_C||I_S||K_S||string e||string f||mpint K) */
    if (net_sshd_compute_hash(ctx, ctx->exchange_hash) < 0) {
        /* BUG-0017: exchange-hash input exceeded the page -> abort. */
        net_sshd_log("exchange hash input too large");
        return -9;
    }
    /* RFC 4253 §8: session_id is the FIRST exchange hash and never changes;
     * rekey derivations must keep using it (BUG-0219). */
    if (!ctx->session_id_set) {
        memcpy(ctx->session_id, ctx->exchange_hash, 32);
        ctx->session_id_set = 1;
    }
    net_sshd_log_hex("H (first 16): ", ctx->exchange_hash, 16);

    /* RSA signature: EM carries SHA256(H) per OpenSSH/paramiko semantics */
    u8 em[256];
    memset(em, 0xFF, 256);
    em[0] = 0x00; em[1] = 0x01;
    static const u8 digest_info[] = {
        0x30,0x31,0x30,0x0d,0x06,0x09,0x60,0x86,0x48,0x01,0x65,0x03,0x04,0x02,0x01,0x05,0x00,0x04,0x20
    };
    u8 h_digest[32];
    sha256(ctx->exchange_hash, 32, h_digest);
    memcpy(em + (256 - 51), digest_info, 19);
    memcpy(em + (256 - 32), h_digest, 32);
    em[256 - 52] = 0x00;
    net_sshd_log("computing RSA signature (2048-bit modexp, ~26s)...");
    u8 sig[256];
    crypto_dh_modexp_n(em, g_hk_d, g_hk_n, sig, 256);

    u8 blob[1400];
    int p = 0;
    {
        u8 ks[400];
        int kp = 0;
        net_sshd_write_cstr(ks, &kp, "ssh-rsa");
        u8 em3[3] = {0x01, 0x00, 0x01};
        net_sshd_write_mpint(ks, &kp, em3, 3);
        net_sshd_write_mpint(ks, &kp, g_hk_n, 256);
        net_sshd_write_str(blob, &p, ks, kp);
    }
    /* string f (32 bytes, RFC 8731) */
    net_sshd_write_str(blob, &p, f_pub, 32);
    {
        u8 sbuf[400];
        int sp = 0;
        net_sshd_write_cstr(sbuf, &sp, "rsa-sha2-256");
        net_sshd_write_str(sbuf, &sp, sig, 256);
        net_sshd_write_str(blob, &p, sbuf, sp);
    }
    if (encrypted)
        return net_sshd_send_packet_encrypted(ctx, SSHD_MSG_KEXDH_REPLY, blob, p);
    return net_sshd_send_packet_unencrypted(ctx, SSHD_MSG_KEXDH_REPLY, blob, p);
}

static int net_sshd_send_kexdh_reply(net_sshd_ctx_t *ctx, int encrypted) {
    if (ctx->kex_curve25519) return net_sshd_send_kexdh_reply_curve25519(ctx, encrypted);
    /* server DH: y, f = g^y mod p, K = e^y mod p */
    crypto_random(ctx->crypto_dh_priv, SSHD_DH_BYTES);
    ctx->crypto_dh_priv[0] &= 0x7F;
    u8 g_val[SSHD_DH_BYTES];
    memset(g_val, 0, SSHD_DH_BYTES);
    g_val[SSHD_DH_BYTES - 1] = 2;
    net_sshd_log("computing f = g^y mod p (2048-bit modexp, ~26s)...");
    u64 t0 = core_timer_ticks();
    crypto_dh_modexp_n(g_val, ctx->crypto_dh_priv, crypto_dh_group14_prime, ctx->crypto_dh_pub, SSHD_DH_BYTES);
    u64 t1 = core_timer_ticks();
    net_sshd_log("computing K = e^y mod p (~26s)...");
    crypto_dh_modexp_n(ctx->client_pub, ctx->crypto_dh_priv, crypto_dh_group14_prime, ctx->shared_secret, SSHD_DH_BYTES);
    u64 t2 = core_timer_ticks();
    char b[80];
    strcpy(b, "DH modexp times: ");
    char num[10];
    u64_to_str((t1 - t0) * 1000 / (u64)OC_TIMER_HZ, num);
    strcat(b, num); strcat(b, " / ");
    u64_to_str((t2 - t1) * 1000 / (u64)OC_TIMER_HZ, num);
    strcat(b, num); strcat(b, " ms");
    net_sshd_log(b);

    /* exchange hash H */
    if (net_sshd_compute_hash(ctx, ctx->exchange_hash) < 0) {
        /* BUG-0017: exchange-hash input exceeded the page -> abort. */
        net_sshd_log("exchange hash input too large");
        return -9;
    }
    /* RFC 4253 §8: session_id is the FIRST exchange hash and never changes;
     * rekey derivations must keep using it (BUG-0219). */
    if (!ctx->session_id_set) {
        memcpy(ctx->session_id, ctx->exchange_hash, 32);
        ctx->session_id_set = 1;
    }
    net_sshd_log_hex("H (first 16): ", ctx->exchange_hash, 16);

    /* RSA signature over H: sig = RSASSA-PKCS1-v1_5-SIGN(d, H)
     * EM = 0x00 0x01 FF..FF 0x00 || DigestInfo(SHA-256) || H  (256 bytes) */
    u8 em[256];
    memset(em, 0xFF, 256);
    em[0] = 0x00; em[1] = 0x01;
    static const u8 digest_info[] = {
        0x30,0x31,0x30,0x0d,0x06,0x09,0x60,0x86,0x48,0x01,0x65,0x03,0x04,0x02,0x01,0x05,0x00,0x04,0x20
    };
    int di_len = (int)sizeof(digest_info);
    /* EM = 0x00 0x01 || PS(0xFF..) || 0x00 || DigestInfo(SHA-256) || digest
     * total 256 bytes; PS length = 256 - 3 - 19 - 32 = 202.
     * WP-09 fix: paramiko's RSAKey.verify_ssh_sig() hashes `data` (the
     * exchange hash H) with SHA-256 before verification, so the EM must
     * carry SHA256(H) — not H itself (RFC 4253 §8.2 semantics as
     * implemented by OpenSSH/paramiko). */
    u8 h_digest[32];
    sha256(ctx->exchange_hash, 32, h_digest);
    int t_len = di_len + 32;
    memcpy(em + (256 - t_len), digest_info, di_len);
    memcpy(em + (256 - 32), h_digest, 32);
    em[256 - t_len - 1] = 0x00;  /* separator byte before DigestInfo */
    net_sshd_log("computing RSA signature (2048-bit modexp, ~26s)...");
    u8 sig[256];
    u64 t3 = core_timer_ticks();
    crypto_dh_modexp_n(em, g_hk_d, g_hk_n, sig, 256);
    u64 t4 = core_timer_ticks();
    strcpy(b, "RSA sign time: ");
    u64_to_str((t4 - t3) * 1000 / (u64)OC_TIMER_HZ, num);
    strcat(b, num); strcat(b, " ms");
    net_sshd_log(b);

    /* KEXDH_REPLY: string K_S + mpint f + string signature */
    u8 blob[1400];
    int p = 0;
    /* K_S = string "ssh-rsa" + mpint e + mpint N */
    {
        u8 ks[400];
        int kp = 0;
        net_sshd_write_cstr(ks, &kp, "ssh-rsa");
        u8 em3[3] = {0x01, 0x00, 0x01};
        net_sshd_write_mpint(ks, &kp, em3, 3);
        net_sshd_write_mpint(ks, &kp, g_hk_n, 256);
        net_sshd_write_str(blob, &p, ks, kp);
    }
    net_sshd_write_mpint(blob, &p, ctx->crypto_dh_pub, SSHD_DH_BYTES);
    /* signature blob: string "rsa-sha2-256" + string sig */
    {
        u8 sbuf[400];
        int sp = 0;
        net_sshd_write_cstr(sbuf, &sp, "rsa-sha2-256");
        net_sshd_write_str(sbuf, &sp, sig, 256);
        net_sshd_write_str(blob, &p, sbuf, sp);
    }
    {
        int rc;
        if (encrypted)
            rc = net_sshd_send_packet_encrypted(ctx, SSHD_MSG_KEXDH_REPLY, blob, p);
        else
            rc = net_sshd_send_packet_unencrypted(ctx, SSHD_MSG_KEXDH_REPLY, blob, p);
        char b[64]; strcpy(b, "KEXDH_REPLY send rc=");
        char n[10]; u64_to_str((u64)(rc < 0 ? -rc : rc), n); strcat(b, n);
        strcat(b, " (p=");
        u64_to_str((u64)p, n); strcat(b, n); strcat(b, ")");
        net_sshd_log(b);
        return rc;
    }
}

/* Parse the KEXDH_INIT payload (mpint e for group14 / 32-byte string for
 * curve25519) into ctx->client_pub. Shared by the initial exchange and the
 * rekey responder (BUG-0219). Returns 0 or -7 (bad e). */
static int net_sshd_parse_kexdh_init(net_sshd_ctx_t *ctx, const u8 *payload, int plen) {
    if (plen < 4) return -7;
    int e_len = (int)net_sshd_read_u32(payload);
    /* right-align into 256 bytes */
    memset(ctx->client_pub, 0, SSHD_DH_BYTES);
    if (ctx->kex_curve25519) {
        if (e_len != 32) return -7;
        memcpy(ctx->client_pub + SSHD_DH_BYTES - 32, payload + 4, 32);
    } else {
        /* P0fix1 BUG-0015 (A14-09): a negative e_len skipped both clamps
         * and made `SSHD_DH_BYTES - el` a wild offset with a huge memcpy
         * length. Also bound `el` by the actual payload size. */
        const u8 *ed = payload + 4;
        int el = e_len;
        if (el < 0) return -7;
        if (4 + el > plen) return -7;   /* declared e extends past payload */
        if (el > 0 && ed[0] == 0) { ed++; el--; }
        if (el > SSHD_DH_BYTES) { ed += (el - SSHD_DH_BYTES); el = SSHD_DH_BYTES; }
        memcpy(ctx->client_pub + (SSHD_DH_BYTES - el), ed, el);
    }
    return 0;
}

/* BUG-0219 (A14-33): client-initiated rekey responder (RFC 4253 §9).
 * Called from the session loops when SSH_MSG_KEXINIT arrives mid-session;
 * kexinit_payload/plen describe the already-received client KEXINIT.
 * The whole exchange runs under the CURRENTLY ACTIVE keys; the new keys
 * apply after NEWKEYS. While the exchange runs no other packets are
 * accepted: a data packet mixed into the key exchange desynchronizes the
 * cipher streams, so the documented policy is drop-with-disconnect (the
 * session is torn down when this returns non-zero). */
static int net_sshd_handle_client_rekey(net_sshd_ctx_t *ctx,
                                        const u8 *kexinit_payload, int kexinit_plen) {
    net_sshd_log("client initiated rekey (KEXINIT mid-session)");
    u8 *payload = (u8 *)(uintptr_t)mem_pmm_alloc_frame();
    if (!payload) { net_sshd_log("rekey: no frame for packet buffer"); return -1; }
    int rc = -1;
    /* 1. the client's KEXINIT already arrived - parse/negotiate it */
    if (net_sshd_parse_kexinit(ctx, kexinit_payload, kexinit_plen) != 0) goto out;
    /* 2. our own KEXINIT (encrypted with the currently active keys) */
    if (net_sshd_send_kexinit(ctx, 1) != 0) goto out;
    /* 3. KEXDH_INIT under the old keys */
    for (;;) {
        u8 mtype;
        int plen = SSHD_PKT_CAP;
        if (net_sshd_recv_packet_encrypted(ctx, &mtype, payload, &plen) != 0) goto out;
        if (mtype != SSHD_MSG_KEXDH_INIT) {
            net_sshd_log("rekey: non-KEX packet inside exchange - dropping");
            goto out;
        }
        if (net_sshd_parse_kexdh_init(ctx, payload, plen) != 0) goto out;
        break;
    }
    /* 4. KEXDH_REPLY + our NEWKEYS (old keys; derive happens after step 5) */
    if (net_sshd_send_kexdh_reply(ctx, 1) != 0) goto out;
    if (net_sshd_send_packet_encrypted(ctx, SSHD_MSG_NEWKEYS, (const u8 *)0, 0) != 0) goto out;
    /* 5. the client's NEWKEYS (old keys) */
    {
        u8 mtype;
        int plen = SSHD_PKT_CAP;
        if (net_sshd_recv_packet_encrypted(ctx, &mtype, payload, &plen) != 0) goto out;
        if (mtype != SSHD_MSG_NEWKEYS) {
            net_sshd_log("rekey: expected NEWKEYS - dropping");
            goto out;
        }
    }
    /* 6. new keys take effect in both directions; session_id stays the
     * FIRST exchange hash (RFC 4253 §8), the derivation uses the new H. */
    net_sshd_derive_keys(ctx);
    ctx->encrypted = 1;
    net_sshd_reset_seq(ctx);   /* BUG-0220: strict-kex reset after every NEWKEYS */
    net_sshd_log("rekey complete - new keys active");
    rc = 0;
out:
    mem_pmm_free_frame((u64)(uintptr_t)payload);
    return rc;
}

static int net_sshd_do_userauth(net_sshd_ctx_t *ctx, u8 payload[], int plen) {
    /* payload: string user + string service + string method + [method data] */
    int off = 0;
    if (off + 4 > plen) return -1;
    int ulen = (int)net_sshd_read_u32(payload + off); off += 4;
    /* P0fix1 BUG-0019 (A14-13): a negative ulen passed `off + ulen > plen`
     * and reached memcpy with a huge size_t length -- pre-password, so any
     * unauthenticated client could crash the sshd. */
    if (ulen < 0 || off + ulen > plen) return -1;
    /* BUG-0223: zero the scratch first - the fixed 32-byte constant-time
     * compare below must not see stale bytes from a previous attempt. */
    memset(ctx->auth_user, 0, sizeof(ctx->auth_user));
    memcpy(ctx->auth_user, payload + off, ulen < 31 ? ulen : 31);
    ctx->auth_user[ulen < 31 ? ulen : 31] = 0;
    off += ulen;
    if (off + 4 > plen) return -1;
    int slen = (int)net_sshd_read_u32(payload + off); off += 4;   /* service */
    off += slen;
    if (off + 4 > plen) return -1;
    int mlen = (int)net_sshd_read_u32(payload + off); off += 4;   /* method */
    off += mlen;
    /* WP-09 fix: fixed-length compare — strcmp relied on the byte after
     * "password" being NUL (it is the FALSE boolean in practice, but that is
     * luck, not correctness). */
    int is_password = (mlen == 8 && memcmp((const char *)(payload + (off - mlen)), "password", 8) == 0);
    int is_publickey = (mlen == 9 && memcmp((const char *)(payload + (off - mlen)), "publickey", 9) == 0);
    if (is_publickey) {
        /* RFC 4252 §7: boolean TRUE, string algo, string blob, string sig.
         * BUG-0075 FIX (A14-24): the client's key is no longer compared
         * against the EMBEDDED public key (which the whole world has);
         * its modulus must appear in /etc/ssh_authorized_keys (one
         * 512-hex-digit modulus per line). No file / no match =>
         * publickey auth is refused; password auth remains available. */
        if (off + 1 + 4 > plen) return -1;
        off += 1;                                   /* boolean TRUE */
        int alen = (int)net_sshd_read_u32(payload + off); off += 4;
        if (off + alen + 4 > plen) return -1;
        off += alen;                                /* algorithm name */
        int blen = (int)net_sshd_read_u32(payload + off); off += 4;
        if (blen < 0 || off + blen > plen) return -1;
        const u8 *blob = payload + off; off += blen;
        /* parse the client blob: string "ssh-rsa" + mpint e + mpint n;
         * extract the 256-byte modulus */
        u8 client_n[256];
        int have_client_n = 0;
        do {
            int cp = 0;
            if (blen < 4) break;
            int an = (int)net_sshd_read_u32(blob + cp); cp += 4;
            if (an != 7 || cp + 7 > blen || memcmp(blob + cp, "ssh-rsa", 7) != 0) break;
            cp += 7;
            if (cp + 4 > blen) break;
            int elen = (int)net_sshd_read_u32(blob + cp); cp += 4;
            if (elen <= 0 || elen > 8 || cp + elen + 4 > blen) break;
            cp += elen;
            int nlen = (int)net_sshd_read_u32(blob + cp); cp += 4;
            if (cp + nlen > blen) break;
            /* RSA-2048 modulus, optional leading zero */
            const u8 *nb = blob + cp;
            int nl = nlen;
            if (nl > 0 && nb[0] == 0) { nb++; nl--; }
            if (nl != 256) break;
            memcpy(client_n, nb, 256);
            have_client_n = 1;
        } while (0);
        /* match against /etc/ssh_authorized_keys moduli */
        int blob_ok = 0;
        const u8 *auth_n = NULL;
        if (have_client_n) {
            for (int i = 0; i < g_auth_count; i++) {
                if (memcmp(g_auth_n[i], client_n, 256) == 0) {
                    blob_ok = 1;
                    auth_n = g_auth_n[i];
                    break;
                }
            }
        }
        if (!blob_ok)
            net_sshd_log("publickey: client key NOT in /etc/ssh_authorized_keys (or file absent)");
        int sig_ok = 0;
        if (blob_ok && off + 4 <= plen) {
            int slen = (int)net_sshd_read_u32(payload + off); off += 4;
            if (slen > 0 && off + slen <= plen) {
                /* sig blob = string "rsa-sha2-256"|"rsa-sha2-512" + string sig(256) */
                const u8 *sb = payload + off;
                int sp = 0;
                if (sp + 4 <= slen) {
                    int an = (int)net_sshd_read_u32(sb + sp); sp += 4;
                    /* OpenSSH clients prefer rsa-sha2-512 (RFC 8332); both OK.
                     * The echoed algorithm in the signed data MUST be the one
                     * the client announced. */
                    int sha_alg = 0;
                    int dig_len = 0;
                    if (an == 12 && sp + 12 <= slen &&
                        memcmp(sb + sp, "rsa-sha2-256", 12) == 0) {
                        sha_alg = RSA_SHA256;
                        dig_len = 32;
                    } else if (an == 12 && sp + 12 <= slen &&
                               memcmp(sb + sp, "rsa-sha2-512", 12) == 0) {
                        sha_alg = RSA_SHA512;
                        dig_len = 64;
                    }
                    if (sha_alg != 0) {
                        sp += 12;
                        if (sp + 4 <= slen) {
                            int siglen = (int)net_sshd_read_u32(sb + sp); sp += 4;
                            if (siglen == 256 && sp + siglen <= slen) {
                                /* signed data: string session_id || byte 50 || user ||
                                 * service || "publickey" || TRUE || algo || blob */
                                static u8 sdata[2048];
                                int sdp = 0;
                                sdata[sdp++] = 0; sdata[sdp++] = 0; sdata[sdp++] = 0; sdata[sdp++] = 32;
                                memcpy(sdata + sdp, ctx->session_id, 32); sdp += 32;
                                sdata[sdp++] = 50;
                                int ul = (int)strlen(ctx->auth_user);
                                sdata[sdp++] = (u8)(ul >> 24); sdata[sdp++] = (u8)(ul >> 16);
                                sdata[sdp++] = (u8)(ul >> 8); sdata[sdp++] = (u8)(ul & 0xFF);
                                memcpy(sdata + sdp, ctx->auth_user, ul); sdp += ul;
                                sdata[sdp++] = 0; sdata[sdp++] = 0; sdata[sdp++] = 0; sdata[sdp++] = 14;
                                memcpy(sdata + sdp, "ssh-connection", 14); sdp += 14;
                                sdata[sdp++] = 0; sdata[sdp++] = 0; sdata[sdp++] = 0; sdata[sdp++] = 9;
                                memcpy(sdata + sdp, "publickey", 9); sdp += 9;
                                sdata[sdp++] = 1;
                                sdata[sdp++] = 0; sdata[sdp++] = 0; sdata[sdp++] = 0; sdata[sdp++] = 12;
                                memcpy(sdata + sdp, (sha_alg == RSA_SHA512) ?
                                          "rsa-sha2-512" : "rsa-sha2-256", 12); sdp += 12;
                                sdata[sdp++] = (u8)(blen >> 24); sdata[sdp++] = (u8)(blen >> 16);
                                sdata[sdp++] = (u8)(blen >> 8); sdata[sdp++] = (u8)(blen & 0xFF);
                                memcpy(sdata + sdp, blob, blen); sdp += blen;
                                u8 sdig[64];
                                if (sha_alg == RSA_SHA512) sha512(sdata, sdp, sdig);
                                else sha256(sdata, sdp, sdig);
                                const u8 *rsig = sb + sp;
                                sig_ok = crypto_rsa_verify_pkcs1(auth_n, 256,
                                                          (const u8 *)"\x01\x00\x01", 3,
                                                          sha_alg, sdig, dig_len,
                                                          rsig, 256) == 1;
                                net_sshd_log(sig_ok ? "publickey signature VALID" :
                                       "publickey signature INVALID");
                            }
                        }
                    }
                }
            }
        }
        if (blob_ok && sig_ok) {
            net_sshd_send_packet_encrypted(ctx, SSHD_MSG_USERAUTH_SUCCESS, (const u8 *)0, 0);
            net_sshd_log("USERAUTH_SUCCESS sent (publickey)");
            return 0;
        }
        u8 fail[32];
        int fp = 0;
        net_sshd_write_cstr(fail, &fp, "password,publickey");
        fail[fp++] = 0;
        net_sshd_send_packet_encrypted(ctx, SSHD_MSG_USERAUTH_FAILURE, fail, fp);
        return -4;
    }
    if (!is_password) {
        /* list allowed methods */
        u8 fail[24];
        int fp = 0;
        net_sshd_write_cstr(fail, &fp, "password,publickey");
        fail[fp++] = 0;  /* partial success = FALSE */
        net_sshd_send_packet_encrypted(ctx, SSHD_MSG_USERAUTH_FAILURE, fail, fp);
        return -2;
    }
    if (off + 1 > plen) return -1;
    /* boolean FALSE (0) then string password */
    off += 1;
    if (off + 4 > plen) return -1;
    int pwlen = (int)net_sshd_read_u32(payload + off); off += 4;
    /* P0fix1 BUG-0019 (A14-13): same negative-length fix for the password
     * string. */
    if (pwlen < 0 || off + pwlen > plen) return -1;
    char pw[32];
    memset(pw, 0, sizeof(pw));
    memcpy(pw, payload + off, pwlen < 31 ? pwlen : 31);

    char b[96];
    strcpy(b, "auth attempt user=");
    strcat(b, ctx->auth_user);
    net_sshd_log(b);
    /* BUG-0223 (A14-37): constant-time credential check. The old code
     * strcmp'd ctx->auth_user against g_ssd.auth_user - but ctx IS the
     * singleton g_ssd and auth_user had already been overwritten with the
     * client's attempt, so the username check was vacuously true AND the
     * configured username was clobbered on the first attempt. Now BOTH
     * credentials are compared against the config store over a fixed
     * 32-byte window (XOR-accumulate, both results computed, no early
     * exit, no length or prefix information leaked by timing). */
    int user_ok = net_sshd_ct_eq(ctx->auth_user, g_sshd_cfg_user, 32);
    int pass_ok = net_sshd_ct_eq(pw, g_sshd_cfg_pass, 32);
    if (user_ok && pass_ok) {
        net_sshd_send_packet_encrypted(ctx, SSHD_MSG_USERAUTH_SUCCESS, (const u8 *)0, 0);
        net_sshd_log("USERAUTH_SUCCESS sent");
        return 0;
    }
    u8 fail[16];
    int fp = 0;
    net_sshd_write_cstr(fail, &fp, "password");
    fail[fp++] = 0;
    net_sshd_send_packet_encrypted(ctx, SSHD_MSG_USERAUTH_FAILURE, fail, fp);
    net_sshd_log("USERAUTH_FAILURE sent (bad credentials)");
    return -3;
}

static void net_sshd_handle_exec(net_sshd_ctx_t *ctx, u8 payload[], int plen) {
    {
        char b[160]; strcpy(b, "[sshd] chan_req payload: ");
        char hx[4];
        for (int i = 0; i < 48 && i < plen; i++) {
            u64_to_hex(payload[i], hx, 2);
            strcat(b, hx);
        }
        net_sshd_log(b);
    }
    int off = 4;
    if (off + 4 > plen) return;
    int reqlen = (int)net_sshd_read_u32(payload + off); off += 4;
    /* BUG-0225 (A14-38): reqlen comes straight from the wire as a signed
     * int; 0xFFFFFFF8 became -8, passed `off + reqlen > plen` and drove
     * `off` negative (out-of-bounds read below the packet buffer).
     * Validate the full 0 <= reqlen <= remaining-bytes window first. */
    if (reqlen < 0 || off + reqlen > plen) return;
    /* WP-09 fix: "exec" is NOT NUL-terminated in the payload — the next byte
     * is want_reply (0x01). strcmp would read past the name into the
     * following field and never match, so every exec request was answered
     * with CHANNEL_FAILURE. Compare the fixed 4 bytes instead. */
    int is_exec = (reqlen == 4 && memcmp((const char *)(payload + off), "exec", 4) == 0);
    off += reqlen;
    if (off >= plen) return;
    int want_reply = payload[off]; off += 1;
    if (!is_exec) {
        if (want_reply) net_sshd_send_packet_encrypted(ctx, SSHD_MSG_CHANNEL_FAILURE, payload, 4);
        return;
    }
    if (off + 4 > plen) return;
    int clen = (int)net_sshd_read_u32(payload + off); off += 4;
    /* WP-09-FIX BUG-013: the old hard limit of 256 bytes rejected most
     * real exec commands (a 405-byte command got CHANNEL_FAILURE) even
     * though the exec output buffer is 4096. Raise the limit to 1024
     * bytes (keeps the on-stack buffer modest). */
    if (clen < 0 || off + clen > plen || clen >= 1024) {
        if (want_reply) net_sshd_send_packet_encrypted(ctx, SSHD_MSG_CHANNEL_FAILURE, payload, 4);
        return;
    }
    char cmd[1024];
    memcpy(cmd, payload + off, clen);
    cmd[clen] = 0;

    char b[96];
    strcpy(b, "exec request: '");
    strcat(b, cmd);
    strcat(b, "'");
    net_sshd_log(b);

    if (want_reply) net_sshd_send_packet_encrypted(ctx, SSHD_MSG_CHANNEL_SUCCESS, payload, 4);
    net_sshd_run_exec(ctx, cmd);
    /* CHANNEL_DATA: u32 recipient + string data.
     * BUG-0224 (A14-39): the 4160-byte dp buffer used to live on the
     * stack; it is a page frame now (leak-free on the alloc-failure path).
     * Output larger than one frame's payload capacity (4096 - 8 header
     * bytes) is sent as multiple CHANNEL_DATA packets - legal stream
     * fragmentation, so the worst handle_exec stack use is cmd[1024]. */
    {
        u8 *dp = (u8 *)(uintptr_t)mem_pmm_alloc_frame();
        if (!dp) {
            net_sshd_log("exec: no frame for CHANNEL_DATA");
        } else {
            int sent = 0;
            while (sent < ctx->exec_len) {
                int chunk = ctx->exec_len - sent;
                if (chunk > SSHD_PKT_CAP - 8) chunk = SSHD_PKT_CAP - 8;
                int dp_len = 4;
                dp[0] = (u8)(ctx->peer_channel >> 24);
                dp[1] = (u8)(ctx->peer_channel >> 16);
                dp[2] = (u8)(ctx->peer_channel >> 8);
                dp[3] = (u8)(ctx->peer_channel & 0xFF);
                net_sshd_write_str(dp, &dp_len, ctx->exec_out + sent, chunk);
                net_sshd_send_packet_encrypted(ctx, SSHD_MSG_CHANNEL_DATA, dp, dp_len);
                sent += chunk;
            }
            mem_pmm_free_frame((u64)(uintptr_t)dp);
        }
    }
    /* exit status 0 as CHANNEL_REQUEST "exit-status".
     * WP-09 fix: the buffer must hold 4 (chan) + 15 (string "exit-status")
     * + 1 (want_reply) + 4 (status) = 24 bytes; the old u8 es[16] overflowed
     * the stack buffer by 8 bytes while writing the reply. */
    {
        u8 es[32];
        int ep = 0;
        es[ep++] = (u8)(ctx->peer_channel >> 24);
        es[ep++] = (u8)(ctx->peer_channel >> 16);
        es[ep++] = (u8)(ctx->peer_channel >> 8);
        es[ep++] = (u8)(ctx->peer_channel & 0xFF);
        net_sshd_write_cstr(es, &ep, "exit-status");
        es[ep++] = 0;
        es[ep++] = 0; es[ep++] = 0; es[ep++] = 0; es[ep++] = 0;
        net_sshd_send_packet_encrypted(ctx, SSHD_MSG_CHANNEL_REQUEST, es, ep);
    }
    /* EOF. WP-09 fix: do NOT send CLOSE here — the client needs time to
     * read CHANNEL_DATA and process CHANNEL_SUCCESS first. The client
     * closes (or the session loop ends) instead. */
    {
        u8 ch[4];
        ch[0] = (u8)(ctx->peer_channel >> 24);
        ch[1] = (u8)(ctx->peer_channel >> 16);
        ch[2] = (u8)(ctx->peer_channel >> 8);
        ch[3] = (u8)(ctx->peer_channel & 0xFF);
        net_sshd_send_packet_encrypted(ctx, SSHD_MSG_CHANNEL_EOF, ch, 4);
    }
    net_sshd_log("exec session complete");
}

static int net_sshd_serve_connection_body(net_sshd_ctx_t *ctx, u8 *payload) {
    u8 msg_type;
    int plen;

    if (net_sshd_send_version(ctx) < 0) return -1;
    if (net_sshd_recv_version(ctx) < 0) return -2;
    net_sshd_log("banner exchange OK");

    if (net_sshd_recv_kexinit(ctx) < 0) return -3;
    if (net_sshd_send_kexinit(ctx, 0) < 0) return -4;
    net_sshd_log("KEXINIT exchange OK");

    /* KEXDH_INIT */
    plen = SSHD_PKT_CAP;
    if (net_sshd_recv_packet_unencrypted(ctx, &msg_type, payload, &plen) < 0) return -5;
    if (msg_type != SSHD_MSG_KEXDH_INIT) return -6;
    /* payload: mpint e (group14) or string e (curve25519, 32 bytes) */
    if (net_sshd_parse_kexdh_init(ctx, payload, plen) != 0) return -7;
    net_sshd_log_hex("client e (first 8): ", ctx->client_pub, 8);

    if (net_sshd_send_kexdh_reply(ctx, 0) < 0) return -8;
    /* our NEWKEYS */
    if (net_sshd_send_packet_unencrypted(ctx, SSHD_MSG_NEWKEYS, (const u8 *)0, 0) < 0) return -9;
    /* client NEWKEYS */
    plen = SSHD_PKT_CAP;
    if (net_sshd_recv_packet_unencrypted(ctx, &msg_type, payload, &plen) < 0) return -10;
    { char b[48]; strcpy(b, "got type after REPLY: ");
              char n[10]; u64_to_str((u64)msg_type, n); strcat(b, n); net_sshd_log(b); }
            if (msg_type != SSHD_MSG_NEWKEYS) return -11;
    net_sshd_derive_keys(ctx);
    ctx->encrypted = 1;
    /* BUG-0220 (A14-34): strict-kex (RFC 9144) - both directions' sequence
     * numbers restart at 0 right after the final NEWKEYS (initial and
     * every rekey exchange). */
    net_sshd_reset_seq(ctx);
    net_sshd_log("NEWKEYS exchange OK — encrypted mode active");

    /* USERAUTH loop. RFC 4252 S5: the client first requests the ssh-userauth
     * service (SERVICE_REQUEST) and must receive SERVICE_ACCEPT before it
     * sends USERAUTH_REQUEST — ignoring the request deadlocks both sides. */
    /* BUG-0222 (A14-36): brute-force budget - at most 6 failed attempts per
     * connection, a constant ~1 s delay after every failure (core_timer
     * ticks, so the penalty is wall-clock and does not reward fast hosts),
     * then SSH_MSG_DISCONNECT(NO_MORE_AUTH_METHODS_AVAILABLE). */
    int auth_failures = 0;
    for (;;) {
        plen = SSHD_PKT_CAP;
        if (net_sshd_recv_packet_encrypted(ctx, &msg_type, payload, &plen) < 0) return -12;
        if (msg_type == SSHD_MSG_DISCONNECT) return -13;
        net_sshd_probe_extra_connection();   /* BUG-0226: refuse queued extras */
        if (msg_type == SSHD_MSG_KEXINIT) {
            /* BUG-0219: a client may rekey at any point after NEWKEYS. */
            if (net_sshd_handle_client_rekey(ctx, payload, plen) != 0) return -17;
            continue;
        }
        if (msg_type == SSHD_MSG_SERVICE_REQUEST) {
            u8 acc[48];
            int ap = 0;
            if (plen >= 4) {
                int slen = (int)net_sshd_read_u32(payload);
                if (slen > 0 && slen <= 32 && 4 + slen <= plen) {
                    net_sshd_write_str(acc, &ap, payload + 4, slen);
                }
            }
            net_sshd_log_hex("SERVICE_REQUEST payload: ", payload, plen);
            net_sshd_log_hex("SERVICE_ACCEPT payload:   ", acc, ap);
            net_sshd_send_packet_encrypted(ctx, SSHD_MSG_SERVICE_ACCEPT, acc, ap);
            net_sshd_log("SERVICE_ACCEPT sent (ssh-userauth)");
            continue;
        }
        if (msg_type == SSHD_MSG_USERAUTH_REQUEST) {
            if (net_sshd_do_userauth(ctx, payload, plen) == 0) break;
            auth_failures++;
            if (auth_failures >= SSHD_AUTH_MAX_ATTEMPTS) {
                net_sshd_send_disconnect(ctx, SSHD_DISCONNECT_NO_MORE_AUTH_METHODS,
                                         "too many authentication failures");
                net_sshd_log("auth failure cap reached - disconnecting");
                return -15;
            }
            {
                /* constant ~1 s penalty per failure (OC_TIMER_HZ ticks) */
                u64 t0 = core_timer_ticks();
                while (core_timer_ticks() - t0 < (u64)OC_TIMER_HZ) net_poll();
            }
        }
        /* ignore others */
    }
    net_sshd_log("client authenticated");

    /* channel loop */
    for (;;) {
        plen = SSHD_PKT_CAP;
        if (net_sshd_recv_packet_encrypted(ctx, &msg_type, payload, &plen) < 0) return -14;
        net_sshd_probe_extra_connection();   /* BUG-0226: refuse queued extras */
        if (msg_type == SSHD_MSG_KEXINIT) {
            /* BUG-0219: client-initiated rekey mid-session. */
            if (net_sshd_handle_client_rekey(ctx, payload, plen) != 0) return -17;
            continue;
        }
        if (msg_type == SSHD_MSG_CHANNEL_OPEN) {
            /* expect string "session" + sender chan + window + max packet.
             * WP-09 fix: fixed-length compare (channel type is not
             * NUL-terminated in the payload; matching worked only because
             * paramiko's first channel id is 0). */
            if (plen >= 11 && memcmp((const char *)(payload + 4), "session", 7) == 0) {
                ctx->peer_channel = net_sshd_read_u32(payload + 4 + 7);
                ctx->our_channel = 0;
                u8 conf[16];
                int cp = 0;
                net_sshd_write_u32(conf, &cp, ctx->peer_channel);
                net_sshd_write_u32(conf, &cp, ctx->our_channel);
                net_sshd_write_u32(conf, &cp, 0x00020000);  /* our window */
                net_sshd_write_u32(conf, &cp, 16384);       /* our max packet */
                net_sshd_send_packet_encrypted(ctx, SSHD_MSG_CHANNEL_OPEN_CONF, conf, cp);
                ctx->channel_open = 1;
                net_sshd_log("session channel open (CONFIRMATION sent)");
            } else {
                /* P0fix1 BUG-0020 (A14-14): the failure reply is 4 (reason)
                 * + 4+31 (message string) + 4 (lang tag) = 43 bytes, but the
                 * buffer was u8 fail[16] -> 27-byte stack overflow that
                 * clobbered the serve_connection frame. Size it for the
                 * actual reply. */
                u8 fail[64];
                int fp = 0;
                u32 rc4 = 3;  /* ADMIN_PROHIBITED */
                fail[fp++] = (u8)(rc4 >> 24); fail[fp++] = (u8)(rc4 >> 16);
                fail[fp++] = (u8)(rc4 >> 8); fail[fp++] = (u8)rc4;
                net_sshd_write_cstr(fail, &fp, "only session channels supported");
                net_sshd_write_u32(fail, &fp, 0);
                net_sshd_send_packet_encrypted(ctx, SSHD_MSG_CHANNEL_OPEN_FAIL, fail, fp);
            }
        } else if (msg_type == SSHD_MSG_CHANNEL_REQUEST) {
            net_sshd_handle_exec(ctx, payload, plen);
            /* WP-09 fix: keep the session loop alive so the client can read
             * CHANNEL_DATA and drive the close handshake. */
            continue;
        } else if (msg_type == SSHD_MSG_CHANNEL_CLOSE) {
            return 0;
        } else if (msg_type == SSHD_MSG_DISCONNECT) {
            return 0;
        }
        /* ignore everything else */
    }
    return 0;
}

/* BUG-0224 (A14-39): 16 KiB boot-stack budget. The old call chain nested
 * ~12 KiB of stack locals under main(): payload[4096] here + payload[4096]
 * + wire[4096] in recv_kexinit + dp[4160] in handle_exec. Every buffer
 * larger than 1 KiB is now a page frame (mem_pmm_alloc_frame), so the
 * worst sshd-internal chain is ~4.3 KiB (handle_exec's cmd[1024] plus
 * small locals over the recv helpers) - leaving > 12 KiB headroom for the
 * shell, driver and RSA/modexp frames below us on the 16 KiB boot stack. */
static int net_sshd_serve_connection(net_sshd_ctx_t *ctx) {
    u8 *payload = (u8 *)(uintptr_t)mem_pmm_alloc_frame();
    if (!payload) { net_sshd_log("serve: no frame for packet buffer"); return -16; }
    int rc = net_sshd_serve_connection_body(ctx, payload);
    mem_pmm_free_frame((u64)(uintptr_t)payload);
    return rc;
}

/* ---------- command entry ---------- */

static void net_sshd_write_u32(u8 *buf, int *p, u32 v) {
    buf[(*p)++] = (u8)(v >> 24);
    buf[(*p)++] = (u8)(v >> 16);
    buf[(*p)++] = (u8)(v >> 8);
    buf[(*p)++] = (u8)(v & 0xFF);
}

int g_tcp_data_trace = 1;

/* BUG-0132 FIX: cancel-check callback polled by net_accept() while sshd
 * blocks waiting for a connection. It drains the keyboard queue on every
 * poll iteration so the console stays responsive; Ctrl+C stops the wait
 * and returns control to the shell instead of freezing it for the full
 * accept timeout ("^C byte never consumed" from the audit run). */
static int sshd_accept_cancel_check(void) {
    int cancel = 0;
    while (driver_input_keyboard_has_key()) {
        int k = driver_input_keyboard_getch();
        if (k == OC_KEY_CTRL_C) cancel = 1;
        /* other keys are drained and dropped: no shell line editor is
         * active while sshd owns the console */
    }
    return cancel;
}

/* ---------- BUG-0226 (A14-40): concurrent-connection policy ----------
 * The shell-capture mechanism (console hook used by shell_execute_captured)
 * and the whole g_ssd context are global singletons, so sessions must stay
 * SEQUENTIAL: one accepted connection is served to completion, then the
 * loop accepts the next one. A connection that queues while a session is
 * active is detected by a throttled (max 1/s) mid-session probe of the
 * listening socket: it is accepted only to receive an immediate
 * SSH_MSG_DISCONNECT (reason 12 TOO_MANY_CONNECTIONS, RFC 4253 §11.1) and
 * is closed - it never hangs waiting for a banner. net_accept() blocks up
 * to 120 s, so the probe registers a cancel callback that ends the wait
 * after ~2 timer ticks (~20 ms), turning it into a quick backlog poll. */
static u64 g_sshd_probe_start;
static int sshd_probe_cancel_check(void) {
    return (core_timer_ticks() - g_sshd_probe_start) >= 2;
}

/* Plaintext SSH_MSG_DISCONNECT for a connection that has no session
 * context yet (only the version banner has been exchanged). */
static void net_sshd_send_disconnect_raw(int sock, u32 reason, const char *desc) {
    u8 pay[96];
    int pp = 0;
    net_sshd_write_u32(pay, &pp, reason);
    net_sshd_write_cstr(pay, &pp, desc);
    net_sshd_write_cstr(pay, &pp, "");       /* language tag */
    int pad = 8 - ((6 + pp) % 8);
    if (pad < 4) pad += 8;
    u8 pkt[128];
    int pkt_len = 2 + pp + pad;
    pkt[0] = (u8)(pkt_len >> 24);
    pkt[1] = (u8)(pkt_len >> 16);
    pkt[2] = (u8)(pkt_len >> 8);
    pkt[3] = (u8)(pkt_len & 0xFF);
    pkt[4] = (u8)pad;
    pkt[5] = SSHD_MSG_DISCONNECT;
    memcpy(pkt + 6, pay, pp);
    crypto_random(pkt + 6 + pp, pad);
    net_send(sock, pkt, 4 + pkt_len);
}

/* Encrypted (in-session) or plaintext DISCONNECT, RFC 4253 §11.1. */
static void net_sshd_send_disconnect(net_sshd_ctx_t *ctx, u32 reason, const char *desc) {
    if (ctx->encrypted) {
        u8 pay[96];
        int pp = 0;
        net_sshd_write_u32(pay, &pp, reason);
        net_sshd_write_cstr(pay, &pp, desc);
        net_sshd_write_cstr(pay, &pp, "");
        net_sshd_send_packet_encrypted(ctx, SSHD_MSG_DISCONNECT, pay, pp);
    } else {
        net_sshd_send_disconnect_raw(ctx->sock, reason, desc);
    }
}

static u64 g_sshd_last_probe;
static void net_sshd_probe_extra_connection(void) {
    int ls = g_sshd_listen_fd;
    if (ls < 0) return;
    u64 now = core_timer_ticks();
    if (now - g_sshd_last_probe < (u64)OC_TIMER_HZ) return;   /* probe at most 1/s */
    g_sshd_last_probe = now;
    g_sshd_probe_start = now;
    net_accept_set_cancel_fn(sshd_probe_cancel_check);
    u32 eip = 0;
    u16 eport = 0;
    int extra = net_accept(ls, &eip, &eport);
    net_accept_set_cancel_fn((net_accept_cancel_fn)0);
    if (extra < 0) return;      /* nothing pending (or probe window elapsed) */
    net_sshd_log("second connection during active session - refusing it");
    /* banner first (the peer cannot parse SSH binary packets without it),
     * then an immediate DISCONNECT, then close - never a hang */
    if (net_send(extra, SSHD_BANNER, (int)strlen(SSHD_BANNER)) > 0) {
        net_sshd_send_disconnect_raw(extra, SSHD_DISCONNECT_TOO_MANY_CONNECTIONS,
                                     "too many connections (one session at a time)");
    }
    net_close(extra);
}

int net_sshd_main(u16 port, const char *user, const char *pass) {
    net_sshd_ctx_t *ctx = &g_ssd;
    memset(ctx, 0, sizeof(*ctx));
    /* BUG-0223 (A14-37): the configured credentials live in their own
     * bounded store (ctx->auth_user is per-attempt scratch space). */
    memset(g_sshd_cfg_user, 0, sizeof(g_sshd_cfg_user));
    memset(g_sshd_cfg_pass, 0, sizeof(g_sshd_cfg_pass));
    if (user) {
        int L = (int)strlen(user);
        if (L > 31) L = 31;
        memcpy(g_sshd_cfg_user, user, L);
    }
    if (pass) {
        int L = (int)strlen(pass);
        if (L > 31) L = 31;
        memcpy(g_sshd_cfg_pass, pass, L);
    }

    /* BUG-0075: per-installation host key (load or generate). The old
     * embedded universal key is GONE from the image (net_sshd_rsa_key.h
     * deleted): a key baked into a downloadable ISO let anyone with the
     * image authenticate to EVERY OpenCubeOS sshd, and doubled as the
     * sshd's own host key. If the per-installation key cannot be
     * produced, sshd now refuses to start (fail closed) instead of
     * silently reusing a publicly-known private key. */
    if (net_sshd_hostkey_ensure() != 0) {
        net_sshd_log("ERROR: cannot obtain a per-installation host key - "
                     "refusing to start sshd (embedded universal key removed)");
        return -1;
    }
    net_sshd_authkeys_reload();
    {
        char kb[96];
        strcpy(kb, "host key: per-installation (");
        char kn[10];
        u64_to_str((u64)g_auth_count, kn);
        strcat(kb, kn);
        strcat(kb, g_auth_count == 1 ? " authorized key)" : " authorized keys)");
        net_sshd_log(kb);
    }

    int ls = net_socket(SOCK_TCP);
    if (ls < 0) { net_sshd_log("listen socket alloc failed"); return 1; }
    net_bind(ls, 0, port);
    if (net_tcp_listen(port, (net_tcp_handler_fn)0) != 0) {
        net_sshd_log("tcp_listen failed");
        net_close(ls);       /* BUG-0226: do not leak the listener */
        return 1;
    }
    char b[64];
    strcpy(b, "listening on port ");
    char n[10];
    u64_to_str((u64)port, n);
    strcat(b, n);
    strcat(b, " (serves one session then returns; Ctrl+C cancels the wait)");
    net_sshd_log(b);
    g_sshd_listen_fd = ls;

    /* BUG-0226 (A14-40): the sshd shell command serves ONE session and
     * then returns to the shell - the documented WP-09 contract
     * (docs/INTERFACES.md S2 "serves one session then returns") and the
     * E2E gate both depend on that. The A14-40 fix is the HANDLING of a
     * connection queued DURING the active session: the mid-session probe
     * refuses it immediately with SSH_MSG_DISCONNECT (never a hang), and
     * the listener is closed leak-free on every exit path. Ctrl+C still
     * cancels the blocking wait. */
    for (;;) {
        u32 cip = 0;
        u16 cport = 0;
        /* BUG-0132 FIX: register the ^C cancel check for the blocking wait. */
        net_accept_set_cancel_fn(sshd_accept_cancel_check);
        int fd = net_accept(ls, &cip, &cport);
        net_accept_set_cancel_fn((net_accept_cancel_fn)0);
        if (fd == -2) {
            net_sshd_log("accept cancelled (Ctrl+C), returning to shell");
            break;
        }
        if (fd < 0) continue;    /* accept timeout: keep waiting (no ls leak) */

        memset(ctx, 0, sizeof(*ctx));   /* fresh per-session state */
        ctx->sock = fd;
        strcpy(b, "connection from ");
        u64_to_str((u64)((cip >> 24) & 0xFF), n); strcat(b, n); strcat(b, ".");
        u64_to_str((u64)((cip >> 16) & 0xFF), n); strcat(b, n); strcat(b, ".");
        u64_to_str((u64)((cip >> 8) & 0xFF), n); strcat(b, n); strcat(b, ".");
        u64_to_str((u64)(cip & 0xFF), n); strcat(b, n);
        net_sshd_log(b);

        int rc = net_sshd_serve_connection(ctx);
        if (rc == 0) net_sshd_log("session finished cleanly");
        else {
            char b2[48];
            strcpy(b2, "session failed (code ");
            u64_to_str((u64)(-rc), n);
            strcat(b2, n); strcat(b2, ")");
            net_sshd_log(b2);
        }
        net_close(fd);
        /* One session per `sshd` invocation (contract above); the shell
         * capture hook is a global singleton, so concurrent sessions are
         * not supportable. Re-run `sshd` for the next session. */
        break;
    }
    g_sshd_listen_fd = -1;
    net_close(ls);
    return 0;
}
