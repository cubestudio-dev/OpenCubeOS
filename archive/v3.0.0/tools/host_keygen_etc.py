#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""Generate per-installation RSA-2048 keys on the HOST and place them
into the FAT32 /etc volume image (etc.img), so the QEMU guest does not
have to run the minutes-long Miller-Rabin keygen under TCG.

  ssh_host_key        sshd host key        (net_sshd_hostkey_ensure)
  ssh_client_key      ssh client identity  (net_ssh publickey auth)
  ssh_authorized_keys one 512-hex-digit modulus per line - the sshd
                      trusts exactly these keys for publickey auth
                      (net_sshd_do_userauth)

File format (512 bytes = n_be[256] || d_be[256], e = 65537) must match
net/net_sshd.c.
"""
import os
import secrets
import sys

OUT = "build/ssh_host_key.bin"
CLIENT_OUT = "build/ssh_client_key.bin"
IMG = "build/etc.img"
MCOPY = "/home/z/opt/extract/usr/bin/mcopy"
MDIR = "/home/z/opt/extract/usr/bin/mdir"


def is_probable_prime(n: int, rounds: int = 32) -> bool:
    if n < 2:
        return False
    for p in (2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37):
        if n % p == 0:
            return n == p
    d = n - 1
    s = 0
    while d % 2 == 0:
        d //= 2
        s += 1
    for _ in range(rounds):
        a = secrets.randbelow(n - 3) + 2
        x = pow(a, d, n)
        if x in (1, n - 1):
            continue
        for _ in range(s - 1):
            x = pow(x, 2, n)
            if x == n - 1:
                break
        else:
            return False
    return True


def gen_prime(bits: int) -> int:
    while True:
        c = secrets.randbits(bits) | (1 << (bits - 1)) | (1 << (bits - 2)) | 1
        if is_probable_prime(c):
            return c


def gen_rsa2048(label: str):
    e = 65537
    while True:
        p = gen_prime(1024)
        q = gen_prime(1024)
        if p == q:
            continue
        n = p * q
        phi = (p - 1) * (q - 1)
        if phi % e == 0:
            continue
        d = pow(e, -1, phi)
        break
    n_be = n.to_bytes(256, "big")
    d_be = d.to_bytes(256, "big")
    print(f"[keygen] {label}: n={n_be[:8].hex()}... e={e}")
    return n_be + d_be, n


def main() -> int:
    os.makedirs("build", exist_ok=True)

    print("[keygen] generating host key (two 1024-bit primes, host, fast)...")
    host_blob, _ = gen_rsa2048("host")
    with open(OUT, "wb") as f:
        f.write(host_blob)
    print(f"[keygen] wrote {OUT} (512 bytes)")

    print("[keygen] generating client identity key (independent pair)...")
    client_blob, client_n = gen_rsa2048("client")
    with open(CLIENT_OUT, "wb") as f:
        f.write(client_blob)
    print(f"[keygen] wrote {CLIENT_OUT} (512 bytes)")

    if not os.path.exists(IMG):
        print(f"[keygen] {IMG} missing - skipped injection")
        return 1

    authorized = client_n.to_bytes(256, "big").hex().upper().encode() + b"\n"
    auth_path = "build/ssh_authorized_keys.tmp"
    with open(auth_path, "wb") as f:
        f.write(authorized)

    ok = True
    for src, dst in ((OUT, "::/ssh_host_key"),
                     (CLIENT_OUT, "::/ssh_client_key"),
                     (auth_path, "::/ssh_authorized_keys")):
        r = os.system(f"{MCOPY} -o -n -i {IMG} {src} {dst}")
        if r != 0:
            print(f"[keygen] mcopy failed for {dst}")
            ok = False
    if ok:
        print(f"[keygen] injected host/client/authorized_keys into {IMG}")
        os.system(f"{MDIR} -i {IMG} ::/")
    os.unlink(auth_path)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
