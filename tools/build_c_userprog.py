#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""
build_c_userprog.py: Compile a C user program and refresh its byte array
in kernel/core/userprogs_data.h.

Usage: python3 tools/build_c_userprog.py <name> <c_file> [mode]
Modes:
  (none)   ET_EXEC user program (userprogs/user.ld @ 0x400000) - default
  pie      ET_DYN PIE program with PT_INTERP=/lib/ld.so
  pie-foo  pie + DT_NEEDED libfoo.so (links against build/libfoo.so)
  ld_so    ld.so itself, linked with libs/ld_so.ld (base 0x10000000);
           kept for backward compatibility: a 3rd argument ending in
           ".ld" is still accepted as a linker-script override.

The mode argument exists because the six dynamic-link test programs
(dyn_hello, so_test, dlsym_test, pie_test, reloc_test, main_dyn) are
ET_DYN/PIE images: building them with the default ET_EXEC recipe
silently downgrades them and breaks the kernel ld.so loader path.
Each build verifies e_type (and PT_INTERP for pie modes) so a recipe
regression fails loudly instead of corrupting the embedded blob.
"""
import os
import re
import struct
import subprocess
import sys

ET_EXEC = 2
ET_DYN = 3
PT_INTERP = 3


def fail(msg):
    print(f"build_c_userprog: ERROR: {msg}", file=sys.stderr)
    sys.exit(1)


def verify_elf(elf_path, data, mode):
    """Fail loudly on any recipe downgrade (e_type / PT_INTERP gates)."""
    if len(data) < 64 or data[:4] != b"\x7fELF":
        fail(f"{elf_path} is not an ELF file")
    e_type = struct.unpack_from("<H", data, 16)[0]
    if mode in ("pie", "pie-foo"):
        if e_type != ET_DYN:
            fail(f"{mode} produced e_type={e_type}, expected ET_DYN (3) - "
                 "PIE recipe regression (silent ET_EXEC downgrade blocked)")
        e_phoff, = struct.unpack_from("<Q", data, 32)
        e_phentsize, e_phnum = struct.unpack_from("<HH", data, 54)
        interp = None
        for i in range(e_phnum):
            off = e_phoff + i * e_phentsize
            p_type, = struct.unpack_from("<I", data, off)
            if p_type == PT_INTERP:
                p_offset, = struct.unpack_from("<Q", data, off + 8)
                p_filesz, = struct.unpack_from("<Q", data, off + 32)
                interp = data[p_offset:p_offset + p_filesz].rstrip(b"\x00").decode()
        if interp != "/lib/ld.so":
            fail(f"{mode} produced PT_INTERP={interp!r}, expected '/lib/ld.so'")
    elif mode in ("exec", "ld_so"):
        if e_type != ET_EXEC:
            fail(f"{mode} produced e_type={e_type}, expected ET_EXEC (2)")


def main():
    if len(sys.argv) != 3 and len(sys.argv) != 4:
        print("usage: build_c_userprog.py <name> <c_file> [mode]",
              file=sys.stderr)
        print("  mode: (none)=exec | pie | pie-foo | ld_so (or a *.ld", file=sys.stderr)
        print("  linker-script path for backward compatibility)", file=sys.stderr)
        sys.exit(1)
    name = sys.argv[1]
    c_path = sys.argv[2]
    if not os.path.exists(c_path):
        fail(f"source not found: {c_path}")

    # PRIVACY FIX: derive project root from this script's location instead of
    # hardcoding a local sandbox path.
    _ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    build_dir = os.path.join(_ROOT, "build")
    os.makedirs(build_dir, exist_ok=True)
    elf_path = os.path.join(build_dir, f'{name}.elf')
    obj_path = os.path.join(build_dir, f'{name}_up.o')
    cc = os.environ.get('CC', '/usr/bin/gcc')
    user_ld = os.path.join(_ROOT, 'userprogs', 'user.ld')
    ld_so_ld = os.path.join(_ROOT, 'libs', 'ld_so.ld')

    # Mode resolution. A 3rd argument ending in ".ld" is the legacy
    # linker-script override (libs/ld_so.c MUST be linked with
    # libs/ld_so.ld, base 0x10000000 - the kernel maps ld.so at that
    # fixed address; userprogs/user.ld (0x400000) would be wrong).
    arg3 = sys.argv[3] if len(sys.argv) == 4 else None
    ld_script = None
    if arg3 is None:
        mode = "exec"
    elif arg3.endswith(".ld"):
        mode = "ld_so"
        ld_script = arg3
    elif arg3 in ("pie", "pie-foo"):
        mode = arg3
    elif arg3 == "ld_so":
        mode = "ld_so"
    else:
        fail(f"unknown mode {arg3!r} (use: pie, pie-foo, ld_so, or a *.ld path)")

    pie_cflags = ["-fPIE"]
    ld_args = []
    if mode == "exec":
        pie_cflags = ["-fno-pie", "-fno-pic"]
        ld_script = user_ld
        ld_args = ["ld", "-n", "-nostdlib", "-T", ld_script,
                   "-z", "max-page-size=0x1000", "-z", "noexecstack"]
    elif mode == "ld_so":
        ld_script = ld_script or ld_so_ld
        ld_args = ["ld", "-n", "-nostdlib", "-T", ld_script,
                   "-z", "max-page-size=0x1000", "-z", "noexecstack"]
    elif mode == "pie":
        # so_test.c-documented recipe (minus -lfoo): a PIE image with
        # PT_INTERP=/lib/ld.so; the kernel invokes ld.so for it.
        ld_args = [cc, "-nostdlib", "-pie", "-Wl,--no-as-needed",
                   "-Wl,--dynamic-linker=/lib/ld.so"]
    elif mode == "pie-foo":
        # pie + DT_NEEDED libfoo.so. Ensures the solib leg of the embed
        # chain produced build/libfoo.so first (Makefile source order is
        # alphabetical, so dlsym_test may run before libfoo).
        foo_so = os.path.join(build_dir, "libfoo.so")
        if not os.path.exists(foo_so):
            solib_py = os.path.join(_ROOT, "tools", "build_solib.py")
            print("build_c_userprog: build/libfoo.so missing - "
                  "running tools/build_solib.py first")
            r = subprocess.run([sys.executable, solib_py],
                               capture_output=True, text=True)
            print(r.stdout, end="")
            if r.returncode != 0:
                print(r.stderr, file=sys.stderr)
                fail("prerequisite solib build failed")
        ld_args = [cc, "-nostdlib", "-pie", "-Wl,--no-as-needed",
                   "-Wl,--dynamic-linker=/lib/ld.so",
                   "-L", build_dir, "-lfoo"]

    r = subprocess.run([cc, '-ffreestanding', '-fno-stack-protector',
                        *pie_cflags, '-mno-red-zone', '-mno-sse', '-mno-mmx',
                        '-mno-3dnow', '-fno-asynchronous-unwind-tables',
                        '-Wall', '-Wextra', '-O2', '-std=gnu11',
                        '-c', c_path, '-o', obj_path],
                       capture_output=True, text=True)
    if r.returncode != 0:
        fail(f"CC error:\n{r.stderr}")

    if mode in ("exec", "ld_so"):
        r = subprocess.run([*ld_args, '-o', elf_path, obj_path],
                           capture_output=True, text=True)
    else:
        r = subprocess.run([*ld_args, '-o', elf_path, obj_path],
                           capture_output=True, text=True)
    if r.returncode != 0:
        fail(f"LD error:\n{r.stderr}")
    subprocess.run(['strip', '--strip-debug', elf_path], check=False)

    with open(elf_path, 'rb') as f:
        data = f.read()
    verify_elf(elf_path, data, mode)
    print(f"build_c_userprog: {name} = {len(data)} bytes from {elf_path} "
          f"[{mode}]")

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

    header_path = os.path.join(_ROOT, 'kernel', 'core', 'userprogs_data.h')
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
        # Mirror tools/embed_userprog.py: a program with no existing block
        # is appended (new programs land in the header instead of failing
        # the whole embed chain).
        with open(header_path, 'a') as f:
            f.write("\n" + new_block)
        print(f"  appended userprog_{name} block to {header_path}")

if __name__ == '__main__':
    main()
