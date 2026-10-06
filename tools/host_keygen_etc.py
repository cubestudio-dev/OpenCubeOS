#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""Generate a per-installation RSA-2048 sshd host key on the HOST and
place it into the FAT32 /etc volume image (etc.img), so the QEMU guest
does not have to run the minutes-long Miller-Rabin keygen under TCG.

File format (must match net_sshd_hostkey_ensure() in net/net_sshd.c):
    512 bytes = n_be[256] || d_be[256]   with e = 65537
"""
import os
import secrets
import sys

OUT = "build/ssh_host_key.bin"
IMG = "build/etc.img"


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


def main() -> int:
    print("[keygen] generating two 1024-bit primes (host, fast)...")
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
    os.makedirs("build", exist_ok=True)
    with open(OUT, "wb") as f:
        f.write(n_be + d_be)
    print(f"[keygen] wrote {OUT} (512 bytes)")

    if os.path.exists(IMG):
        mcopy = "/home/z/opt/extract/usr/bin/mcopy"
        r = os.system(f"{mcopy} -o -n -i {IMG} {OUT} ::/ssh_host_key")
        if r == 0:
            print(f"[keygen] injected into {IMG}:/ssh_host_key")
            # verify
            os.system(f"/home/z/opt/extract/usr/bin/mdir -i {IMG} ::/")
        else:
            print("[keygen] mcopy failed")
            return 1
    else:
        print(f"[keygen] {IMG} missing - skipped injection")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
