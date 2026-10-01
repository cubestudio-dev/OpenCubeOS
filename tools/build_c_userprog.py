#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""
build_c_userprog.py: Compile a C user program and refresh its byte array
in kernel/userprogs_data.h.

Usage: python3 tools/build_c_userprog.py <name> <c_file>
Example: python3 tools/build_c_userprog.py ush userprogs/ush.c
"""
import sys, os, subprocess, re

def main():
    if len(sys.argv) != 3:
        print("usage: build_c_userprog.py <name> <c_file>", file=sys.stderr)
        sys.exit(1)
    name = sys.argv[1]
    c_path = sys.argv[2]
    if not os.path.exists(c_path):
        print(f"source not found: {c_path}", file=sys.stderr); sys.exit(1)

    # PRIVACY FIX: derive project root from this script's location instead of
    # hardcoding a local sandbox path.
    _ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    build_dir = os.path.join(_ROOT, 'build')
    os.makedirs(build_dir, exist_ok=True)
    elf_path = os.path.join(build_dir, f'{name}.elf')
    obj_path = os.path.join(build_dir, f'{name}_up.o')
    cc = os.environ.get('CC', '/usr/bin/gcc')
    ld = os.environ.get('LD', '/usr/bin/ld')
    user_ld = os.path.join(_ROOT, 'userprogs', 'user.ld')

    r = subprocess.run([cc, '-ffreestanding', '-fno-stack-protector', '-fno-pie',
                        '-fno-pic', '-mno-red-zone', '-mno-sse', '-mno-mmx',
                        '-mno-3dnow', '-fno-asynchronous-unwind-tables',
                        '-Wall', '-Wextra', '-O2', '-std=gnu11',
                        '-c', c_path, '-o', obj_path],
                       capture_output=True, text=True)
    if r.returncode != 0:
        print(f"CC error:\n{r.stderr}", file=sys.stderr); sys.exit(1)

    r = subprocess.run([ld, '-n', '-nostdlib', '-T', user_ld,
                        '-z', 'max-page-size=0x1000', '-z', 'noexecstack',
                        '-o', elf_path, obj_path],
                       capture_output=True, text=True)
    if r.returncode != 0:
        print(f"LD error:\n{r.stderr}", file=sys.stderr); sys.exit(1)
    subprocess.run(['strip', '--strip-debug', elf_path], check=False)

    with open(elf_path, 'rb') as f:
        data = f.read()
    print(f"build_c_userprog: {name} = {len(data)} bytes from {elf_path}")

    lines = []
    lines.append(f"const u8 userprog_{name}[] = {{")
    for i in range(0, len(data), 16):
        chunk = data[i:i+16]
        hex_vals = ", ".join(f"0x{b:02x}" for b in chunk)
        comma = "," if i + 16 < len(data) else ""
        lines.append(f"    {hex_vals}{comma}")
    lines.append("};")
    lines.append(f"const u64 userprog_{name}_size = {len(data)};")
    new_block = "\n".join(lines) + "\n"

    header_path = os.path.join(_ROOT, 'kernel', 'userprogs_data.h')
    with open(header_path, 'r') as f:
        content = f.read()
    pattern = re.compile(
        r"const u8 userprog_" + re.escape(name) + r"\[\] = \{.*?\};\nconst u64 userprog_" +
        re.escape(name) + r"_size = \d+;\n",
        re.DOTALL)
    if pattern.search(content):
        content = pattern.sub(new_block, content, count=1)
        with open(header_path, 'w') as f:
            f.write(content)
        print(f"  replaced userprog_{name} block in {header_path}")
    else:
        print(f"  ERROR: userprog_{name} block not found in {header_path}", file=sys.stderr)
        sys.exit(1)

if __name__ == '__main__':
    main()
