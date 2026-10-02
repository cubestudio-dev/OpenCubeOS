#!/usr/bin/env python3
"""Generate gzip/deflate test vectors for kernel/update_test_cmds.c."""
import zlib, struct, sys

def gzip_wrap(deflated: bytes, raw: bytes, flg: int = 0, extra: bytes = b"",
              name: bytes = b"") -> bytes:
    out = b"\x1f\x8b\x08" + bytes([flg]) + b"\x00\x00\x00\x00\x00\x03"
    if flg & 4:
        out += struct.pack("<H", len(extra)) + extra
    if flg & 8:
        out += name + b"\x00"
    out += deflated
    out += struct.pack("<II", zlib.crc32(raw) & 0xFFFFFFFF, len(raw) & 0xFFFFFFFF)
    return out

payload1 = b"Hello, Open Cube OS! This is the WP-10u update package payload. " * 20
payload2 = b"ABCDEF"   # tiny, no matches

def deflate_fixed(raw: bytes) -> bytes:
    co = zlib.compressobj(6, zlib.DEFLATED, -15, 9, zlib.Z_FIXED)
    out = co.compress(raw)
    out += co.flush(zlib.Z_FINISH)
    return out

cases = {
    "stored":  (gzip_wrap(zlib.compress(payload1, 0)[2:-4], payload1), payload1),
    "fixed":   (gzip_wrap(deflate_fixed(payload1), payload1), payload1),
    "dynamic": (gzip_wrap(zlib.compress(payload1, 9)[2:-4], payload1), payload1),
    "tiny":    (gzip_wrap(zlib.compress(payload2, 9)[2:-4], payload2), payload2),
    "fname":   (gzip_wrap(zlib.compress(payload1, 6)[2:-4], payload1, flg=8, name=b"pkg.tar"), payload1),
    "fextra":  (gzip_wrap(zlib.compress(payload1, 6)[2:-4], payload1, flg=4, extra=b"AB\x02\x00xy"), payload1),
    "both":    (gzip_wrap(zlib.compress(payload1, 6)[2:-4], payload1, flg=4|8, extra=b"AB\x02\x00xy", name=b"pkg.tar"), payload1),
}

def c_array(name: str, data: bytes) -> str:
    lines = []
    for i in range(0, len(data), 16):
        chunk = data[i:i+16]
        lines.append("    " + " ".join(f"0x{b:02X}," for b in chunk))
    return f"static const u8 {name}[] = {{\n" + "\n".join(lines) + "\n};"

def c_str(name: str, data: bytes) -> str:
    esc = data.decode("ascii").replace("\\", "\\\\").replace('"', '\\"')
    return f'static const char *{name} = "{esc}";'

print("/* ---- generated gzip test vectors (tools/gen_gzip_vectors.py) ---- */")
print()
print(c_str("GZV_PAYLOAD_EXPECTED", payload1))
print()
for k, (gz, raw) in cases.items():
    print(f"/* {k}: {len(gz)} compressed bytes -> {len(raw)} raw bytes */")
    print(c_array(f"GZV_{k}_GZ", gz))
    print()

# corrupt CRC variant: flip last byte of the trailer of "dynamic"
dyn_gz = bytearray(cases["dynamic"][0])
dyn_gz[-5] ^= 0xFF
print("/* dynamic with a corrupted CRC32 trailer */")
print(c_array("GZV_BCRC_GZ", bytes(dyn_gz)))
print()

# truncated stream (header + first bytes only)
trunc = bytes(cases["dynamic"][0][:24])
print("/* truncated dynamic stream (24 bytes) */")
print(c_array("GZV_TRUNC_GZ", trunc))
print()

print(f"/* expected raw sizes: dynamic={len(payload1)} tiny={len(payload2)} */")
