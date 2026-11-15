#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""Open Cube OS WP-07 - OCIRE Ed25519 update-signing key tool.

Two jobs:
  gen    Generate a fresh Ed25519 keypair into build/ocire/keys/
         (ed25519.pub + ed25519.key, 32 raw bytes each) and emit the
         generated C file ocire/generated/ocire_ed25519_pub.c that
         compiles the PUBLIC key into the OCIRE kernel.  The private
         key NEVER enters the repository (build/ is gitignored).
  sign   Sign a file with build/ocire/keys/ed25519.key -> <file>.sig
         (64 raw bytes, RFC 8032).

The Ed25519 implementation below is the classic public-domain
reference arithmetic (RFC 8032 style) - slow, obviously correct, and
dependency-free.  The signature bytes are bit-compatible with
libsodium / OpenSSL / pynacl, which the E2E tests use as independent
cross-checks.

Usage:
  gen_ocire_ed25519.py gen  [--out-dir DIR] [--c-out FILE]
  gen_ocire_ed25519.py sign FILE [KEY]
"""
import hashlib
import os
import secrets
import sys

p = 2**255 - 19
L = 2**252 + 27742317777372353535851937790883648493
d = -121665 * pow(121666, p - 2, p) % p
I = pow(2, (p - 1) // 4, p)


def xrecover(y):
    xx = (y * y - 1) * pow(d * y * y + 1, p - 2, p)
    x = pow(xx, (p + 3) // 8, p)
    if (x * x - xx) % p != 0:
        x = (x * I) % p
    if x % 2 != 0:
        x = p - x
    return x


By = 4 * pow(5, p - 2, p) % p
Bx = xrecover(By)
B = (Bx % p, By % p, 1, Bx * By % p)   # extended coordinates
IDENT = (0, 1, 1, 0)                    # neutral element


def edwards_add(P, Q):
    x1, y1, z1, t1 = P
    x2, y2, z2, t2 = Q
    a = (y1 - x1) * (y2 - x2) % p
    b = (y1 + x1) * (y2 + x2) % p
    c = t1 * 2 * d * t2 % p
    dd = z1 * 2 * z2 % p
    e = b - a
    f = dd - c
    g = dd + c
    h = b + a
    return (e * f % p, g * h % p, f * g % p, e * h % p)


def scalarmult(P, e):
    Q = IDENT
    while e > 0:
        if e & 1:
            Q = edwards_add(Q, P)
        P = edwards_add(P, P)
        e >>= 1
    return Q


def point_compress(P):
    x, y, z, _ = P
    zi = pow(z, p - 2, p)
    x = x * zi % p
    y = y * zi % p
    return int.to_bytes(y | ((x & 1) << 255), 32, "little")


def point_decompress(s):
    if len(s) != 32:
        return None
    y = int.from_bytes(s, "little")
    sign = y >> 255
    y &= (1 << 255) - 1
    x = xrecover(y)
    if x == 0 and sign:
        return None
    if x & 1 != sign:
        x = p - x
    P = (x, y, 1, x * y % p)
    # on-curve check
    if (-x * x + y * y - 1 - d * x * x * y * y) % p != 0:
        return None
    return P


def sha512_mod_l(m):
    h = hashlib.sha512(m).digest()
    return int.from_bytes(h, "little") % L


def secret_expand(secret):
    if len(secret) != 32:
        raise ValueError("the Ed25519 secret must be 32 bytes")
    h = hashlib.sha512(secret).digest()
    a = int.from_bytes(h[:32], "little")
    a &= (1 << 254) - 8
    a |= 1 << 254
    return a, h[32:]


def public_key(secret):
    a, _ = secret_expand(secret)
    return point_compress(scalarmult(B, a))


def sign(secret, msg):
    a, prefix = secret_expand(secret)
    A = point_compress(scalarmult(B, a))
    r = sha512_mod_l(prefix + msg)
    R = scalarmult(B, r)
    Rs = point_compress(R)
    S = (r + sha512_mod_l(Rs + A + msg) * a) % L
    return Rs + int.to_bytes(S, 32, "little")


def verify(public, msg, signature):
    if len(public) != 32 or len(signature) != 64:
        return False
    A = point_decompress(public)
    if A is None:
        return False
    Rs = signature[:32]
    R = point_decompress(Rs)
    if R is None:
        return False
    S = int.from_bytes(signature[32:], "little")
    if S >= L:
        return False
    k = sha512_mod_l(Rs + public + msg)
    lhs = scalarmult(B, S)
    rhs = edwards_add(R, scalarmult(A, k))
    return point_compress(lhs) == point_compress(rhs)


def cmd_gen(out_dir, c_out):
    os.makedirs(out_dir, exist_ok=True)
    key_path = os.path.join(out_dir, "ed25519.key")
    pub_path = os.path.join(out_dir, "ed25519.pub")
    if os.path.exists(key_path) and os.path.exists(pub_path):
        # keypair exists: keep it (reproducible builds + already-signed
        # packages keep verifying); only refresh the C anchor
        with open(pub_path, "rb") as f:
            pub = f.read()
        print("[ed25519] keypair exists, reusing pub=%s..." % pub.hex()[:16])
    else:
        secret = secrets.token_bytes(32)
        pub = public_key(secret)
        with open(key_path, "wb") as f:
            f.write(secret)
        os.chmod(key_path, 0o600)
        with open(pub_path, "wb") as f:
            f.write(pub)
        print("[ed25519] keypair generated: pub=%s..." % pub.hex()[:16])
        print("[ed25519] private key: %s (keep OFF the repository)" % key_path)
    # the generated C source with the embedded public key
    os.makedirs(os.path.dirname(c_out), exist_ok=True)
    lines = []
    for row in range(0, 32, 8):
        elems = pub[row:row + 8]
        lines.append("    " + ", ".join("0x%02X" % b for b in elems)
                     + ("," if row + 8 < 32 else ""))
    csrc = (
        "/* Generated by tools/gen_ocire_ed25519.py - DO NOT EDIT.\n"
        " * The OCIRE Ed25519 update-verification public key (32 bytes,\n"
        " * little-endian, RFC 8032 encoding).  The matching private key\n"
        " * stays on the build host (build/ocire/keys/ed25519.key, never\n"
        " * committed) and signs official ocire-update.bin payloads. */\n"
        "#include \"../ocire_ed25519.h\"\n\n"
        "const u8 ocire_ed25519_pubkey[32] = {\n"
        + "\n".join(lines) + "\n};\n"
    )
    tmp = c_out + ".tmp"
    with open(tmp, "w") as f:
        f.write(csrc)
    if os.path.exists(c_out):
        with open(c_out) as f:
            old = f.read()
        if old == csrc:
            os.unlink(tmp)
            print("[ed25519] public key unchanged (%s)" % pub.hex()[:16] + "...")
            return
    os.replace(tmp, c_out)
    print("[ed25519] keypair generated: pub=%s..." % pub.hex()[:16])
    print("[ed25519] private key: %s (keep OFF the repository)" % key_path)
    print("[ed25519] C anchor:    %s" % c_out)


def cmd_sign(path, key_path):
    if not os.path.exists(key_path):
        sys.exit("[ed25519] FATAL: private key %s missing (run: gen)" % key_path)
    with open(key_path, "rb") as f:
        secret = f.read()
    with open(path, "rb") as f:
        msg = f.read()
    sig = sign(secret, msg)
    with open(path + ".sig", "wb") as f:
        f.write(sig)
    pub = public_key(secret)
    ok = verify(pub, msg, sig)
    print("[ed25519] signed %s (%d bytes) -> %s.sig" %
          (path, len(msg), path))
    print("[ed25519] self-verify: %s" % ("OK" if ok else "FAILED"))
    if not ok:
        sys.exit(1)


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        sys.exit(2)
    if args[0] == "gen":
        root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
        out_dir = os.environ.get("OCIRE_KEYDIR",
                                 os.path.join(root, "build/ocire/keys"))
        c_out = os.path.join(root, "ocire/generated/ocire_ed25519_pub.c")
        i = 1
        while i < len(args):
            if args[i] == "--out-dir" and i + 1 < len(args):
                out_dir = args[i + 1]
                i += 2
            elif args[i] == "--c-out" and i + 1 < len(args):
                c_out = args[i + 1]
                i += 2
            else:
                i += 1
        cmd_gen(out_dir, c_out)
    elif args[0] == "sign":
        path = args[1]
        key = args[2] if len(args) > 2 else None
        if key is None:
            root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
            key = os.path.join(root, "build/ocire/keys/ed25519.key")
        cmd_sign(path, key)
    else:
        print(__doc__)
        sys.exit(2)


if __name__ == "__main__":
    main()
