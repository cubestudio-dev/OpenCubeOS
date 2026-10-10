#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""
embed_userprog.py: Compile a user .asm program and append its bytes
as a new entry to kernel/generated/userprogs_data.h.

Usage: python3 tools/embed_userprog.py <name> <asm_file>
"""
import sys, os, subprocess

def main():
    if len(sys.argv) != 3:
        print("usage: embed_userprog.py <name> <asm_file>", file=sys.stderr)
        sys.exit(1)
    name = sys.argv[1]
    asm_path = sys.argv[2]
    if not os.path.exists(asm_path):
        print(f"asm not found: {asm_path}", file=sys.stderr); sys.exit(1)

    # 1. Compile asm to ELF binary
    # PRIVACY FIX: derive project root from this script's location instead of
    # hardcoding a local sandbox path.
    _ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    build_dir = os.path.join(_ROOT, 'build')
    os.makedirs(build_dir, exist_ok=True)
    elf_path = os.path.join(build_dir, f'{name}.elf')
    nasm = os.environ.get('NASM', 'nasm')
    ld = os.environ.get('LD', '/usr/bin/ld')
    user_ld = os.path.join(_ROOT, 'userprogs', 'user.ld')
    obj_path = os.path.join(build_dir, f'{name}.o')
    r = subprocess.run([nasm, '-f', 'elf64', '-F', 'dwarf', '-g', asm_path, '-o', obj_path],
                       capture_output=True, text=True)
    if r.returncode != 0:
        print(f"NASM error:\n{r.stderr}", file=sys.stderr); sys.exit(1)
    r = subprocess.run([ld, '-n', '-nostdlib', '-T', user_ld, '-z', 'max-page-size=0x1000',
                       '-z', 'noexecstack', '-o', elf_path, obj_path],
                       capture_output=True, text=True)
    if r.returncode != 0:
        print(f"LD error:\n{r.stderr}", file=sys.stderr); sys.exit(1)
    subprocess.run(['strip', '--strip-debug', elf_path], check=False)

    # 2. Read bytes
    with open(elf_path, 'rb') as f:
        data = f.read()
    print(f"embed_userprog: {name} = {len(data)} bytes from {elf_path}")

    # 3. Generate C array text
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

    # 4. Insert or REPLACE the program's block in userprogs_data.h.
    # WP-10-AUDIT_P2-fix3 G6 fix: the old code skipped the write whenever
    # the program already had an entry, so a rebuilt (and CHANGED) .asm
    # program kept running as its STALE embedded blob forever - the g6_test
    # suite executed an older T2 than the source showed and its behaviour
    # could not be reproduced from the sources. The block is now located
    # by its two-part marker (array + size) and replaced in place.
    header_path = os.path.join(_ROOT, 'kernel', 'core', 'userprogs_data.h')
    with open(header_path, 'r') as f:
        content = f.read()
    start_marker = f"const u8 userprog_{name}[] = {{"
    end_marker = f"const u64 userprog_{name}_size = "
    start = content.find(start_marker)
    if start >= 0:
        # Block runs to the end of its size line (one line after "};").
        brace_end = content.find("};", start)
        if brace_end < 0:
            print(f"  {name}: malformed block (no closing brace), aborting",
                  file=sys.stderr)
            sys.exit(1)
        size_line_end = content.find("\n", content.find(end_marker, brace_end))
        if size_line_end < 0:
            size_line_end = len(content)
        content = content[:start] + new_block + content[size_line_end + 1:]
        with open(header_path, 'w') as f:
            f.write(content)
        print(f"  replaced userprog_{name} block in {header_path}")
    else:
        with open(header_path, 'a') as f:
            f.write(new_block)
        print(f"  appended to {header_path}")

if __name__ == '__main__':
    main()
