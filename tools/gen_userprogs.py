#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""
gen_userprogs.py: full regeneration of src/L0/core/userprogs_data.h.

3.0.0-WP04: one command = one app = one folder (src/L1/<name>/).
Discovery (name resolution is mechanical - no hand-written tables):
  - folder apps : src/L1/<name>/ containing main.c or main.asm;
                 program name = folder name; every *.c in the folder is
                 compiled and linked together (multi-file apps are fine).
  - legacy flat: src/L1/*.c and src/L1/*.asm; program name = basename
                 (the v2.x embedded test programs, unchanged modes).
  - _shared/    : header-only library folder, never an app.
  - libfoo.c    : the embedded solib, handled by tools/build_solib.py
                 (solib_data.h, not this header).

Output: one byte array + size per program, plus the OC_USERPROGS[]
registry consumed by core_userprog_lookup() (services/sysapi/
core_syscall.c) - the WP-04 replacement for the old hardcoded if-else
name chain.  A full-file regeneration also purges orphaned blobs whose
sources no longer exist.
"""
import os
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
L1 = os.path.join(ROOT, "src", "L1")
BUILD = os.path.join(ROOT, "build")
HEADER = os.path.join(ROOT, "src", "L0", "core", "userprogs_data.h")
USER_LD = os.path.join(L1, "user.ld")
LD_SO_LD = os.path.join(L1, "ld_so.ld")

CC = os.environ.get("CC", "/usr/bin/gcc")
NASM = os.environ.get("NASM", "nasm")
LD = os.environ.get("LD", "/usr/bin/ld")

CFLAGS_BASE = [
    "-ffreestanding", "-fno-stack-protector", "-fno-pie", "-fno-pic",
    "-mno-red-zone", "-mno-sse", "-mno-mmx", "-mno-3dnow",
    "-fno-asynchronous-unwind-tables",
    "-Wall", "-Wextra", "-Werror", "-O2", "-std=gnu11",
    "-I", L1,
]

# Legacy flat-file build modes (carried over verbatim from the v2.x
# Makefile userprogs recipe so every existing program keeps its exact
# image type: pie/pie-foo are ET_DYN with PT_INTERP=/lib/ld.so and the
# kernel would reject a silent ET_EXEC downgrade).
FLAT_PIE = {"dyn_hello.c", "pie_test.c"}
FLAT_PIE_FOO = {"so_test.c", "dlsym_test.c", "reloc_test.c", "main_dyn.c"}

ET_EXEC = 2
ET_DYN = 3
PT_INTERP = 3


def ident(name):
    """C symbol safe form of a program name (mkfs.fat32 -> mkfs_fat32)."""
    import re as _re
    return _re.sub(r"\W", "_", name)


def fail(msg):
    print(f"gen_userprogs: ERROR: {msg}", file=sys.stderr)
    sys.exit(1)


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        print(" ".join(cmd))
        print(r.stdout)
        print(r.stderr, file=sys.stderr)
        fail("command failed (see above)")
    return r


def verify_elf(name, data, mode):
    if len(data) < 64 or data[:4] != b"\x7fELF":
        fail(f"{name}: not an ELF")
    e_type = struct.unpack_from("<H", data, 16)[0]
    if mode in ("pie", "pie-foo"):
        if e_type != ET_DYN:
            fail(f"{name}: e_type={e_type}, expected ET_DYN for {mode}")
    elif mode == "exec":
        if e_type != ET_EXEC:
            fail(f"{name}: e_type={e_type}, expected ET_EXEC")
    # ld_so is linked at 0x10000000 with its own script; e_type stays
    # ET_EXEC, verified by the kernel loader at runtime anyway.


def build_crt0():
    """Compile the shared ring-3 crt0 trampoline (_shared/crt0.asm).
    Every folder app links it: it reads the execve stack layout
    (argc at [rsp]) BEFORE any compiler prologue and tail-calls
    app_main(argc, argv) - a compiled C _start() cannot do that
    (the compiler may insert `sub $N,%rsp` before any inline asm;
    the WP04 G5 live-catch)."""
    obj = os.path.join(BUILD, "crt0.o")
    run([NASM, "-f", "elf64", "-F", "dwarf", "-g",
         os.path.join(L1, "_shared", "crt0.asm"), "-o", obj])
    return obj


def build_solib():
    solib_py = os.path.join(ROOT, "tools", "build_solib.py")
    r = subprocess.run([sys.executable, solib_py], capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stdout, end="")
        print(r.stderr, file=sys.stderr)
        fail("build_solib.py failed (pie-foo programs need build/libfoo.so)")
    return r.stdout


def compile_c(name, c_paths, mode, extra_cflags=None, extra_objs=None):
    """Compile every .c to an object; link per mode; return ELF bytes."""
    objs = []
    cflags = list(CFLAGS_BASE) + (extra_cflags or [])
    if mode == "exec":
        pass
    elif mode in ("pie", "pie-foo"):
        cflags = [f for f in cflags if f not in ("-fno-pie", "-fno-pic")]
        cflags += ["-fPIE"]
    elif mode == "ld_so":
        pass
    for i, c in enumerate(c_paths):
        obj = os.path.join(BUILD, f"{name}_up{i}.o")
        run([CC, *cflags, "-c", c, "-o", obj])
        objs.append(obj)
    elf = os.path.join(BUILD, f"{name}.elf")
    if mode == "exec":
        run([LD, "-n", "-nostdlib", "-T", USER_LD,
             "-z", "max-page-size=0x1000", "-z", "noexecstack",
             "-o", elf, *(extra_objs or []), *objs])
    elif mode == "ld_so":
        run([LD, "-n", "-nostdlib", "-T", LD_SO_LD,
             "-z", "max-page-size=0x1000", "-z", "noexecstack",
             "-o", elf, *objs])
    elif mode == "pie":
        run([CC, "-nostdlib", "-pie", "-Wl,--no-as-needed",
             "-Wl,--dynamic-linker=/lib/ld.so", "-o", elf, *objs])
    elif mode == "pie-foo":
        foo = os.path.join(BUILD, "libfoo.so")
        if not os.path.exists(foo):
            fail(f"{name}: build/libfoo.so missing")
        run([CC, "-nostdlib", "-pie", "-Wl,--no-as-needed",
             "-Wl,--dynamic-linker=/lib/ld.so",
             "-L", BUILD, "-lfoo", "-o", elf, *objs])
    subprocess.run(["strip", "--strip-debug", elf], check=False)
    with open(elf, "rb") as f:
        data = f.read()
    verify_elf(name, data, mode)
    return data


def compile_asm(name, asm_paths, ld_script=USER_LD, extra_objs=None):
    objs = []
    for i, a in enumerate(asm_paths):
        obj = os.path.join(BUILD, f"{name}_up{i}.o")
        run([NASM, "-f", "elf64", "-F", "dwarf", "-g", a, "-o", obj])
        objs.append(obj)
    elf = os.path.join(BUILD, f"{name}.elf")
    run([LD, "-n", "-nostdlib", "-T", ld_script,
         "-z", "max-page-size=0x1000", "-z", "noexecstack",
         "-o", elf, *(extra_objs or []), *objs])
    subprocess.run(["strip", "--strip-debug", elf], check=False)
    with open(elf, "rb") as f:
        data = f.read()
    verify_elf(name, data, "exec")
    return data


def discover():
    """-> ordered [(name, kind, paths)] ; kind in c/asm with build mode."""
    progs = []  # (name, task-dict)

    # folder apps: name = folder name (one command = one app = one folder)
    for entry in sorted(os.listdir(L1)):
        d = os.path.join(L1, entry)
        if not os.path.isdir(d) or entry == "_shared":
            continue
        if not os.path.exists(os.path.join(d, "main.c")) and \
           not os.path.exists(os.path.join(d, "main.asm")):
            fail(f"src/L1/{entry}/ has no main.c or main.asm - every app "
                 "folder needs exactly one entry point")
        cs = sorted(os.path.splitext(f)[0] and os.path.join(d, f)
                    for f in os.listdir(d) if f.endswith(".c"))
        asms = sorted(os.path.join(d, f)
                      for f in os.listdir(d) if f.endswith(".asm"))
        progs.append((entry, "folder", cs, asms))

    # legacy flat programs: name = basename
    flats = sorted(f for f in os.listdir(L1)
                   if os.path.isfile(os.path.join(L1, f))
                   and (f.endswith(".c") or f.endswith(".asm")))
    for f in flats:
        if f == "libfoo.c":
            continue  # solib, handled by build_solib.py
        name = os.path.splitext(f)[0]
        path = os.path.join(L1, f)
        mode = "exec"
        if f == "ld_so.c":
            mode = "ld_so"
        elif f in FLAT_PIE:
            mode = "pie"
        elif f in FLAT_PIE_FOO:
            mode = "pie-foo"
        progs.append((name, "flat", [path] if f.endswith(".c") else [],
                      [path] if f.endswith(".asm") else [], mode))
    return progs


def emit(progs, blobs):
    lines = []
    lines.append("/* SPDX-License-Identifier: Apache-2.0 */")
    lines.append("/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */")
    lines.append("/* Auto-generated by tools/gen_userprogs.py - DO NOT EDIT.")
    lines.append(" *")
    lines.append(" * 3.0.0-WP04: every ring-3 program embedded in the")
    lines.append(" * kernel image, one byte array per app, plus the")
    lines.append(" * OC_USERPROGS[] name registry (the execve surface -")
    lines.append(" * core_userprog_lookup() walks the registry, nothing")
    lines.append(" * is hand-registered). */")
    lines.append("")
    lines.append('#include "types.h"')
    lines.append("")
    for (name, kind, cs, asms, *rest) in progs:
        data = blobs[name]
        sym = ident(name)
        lines.append(f"const u8 userprog_{sym}[] = {{")
        for i in range(0, len(data), 16):
            chunk = data[i:i + 16]
            hex_vals = ", ".join(f"0x{b:02x}" for b in chunk)
            comma = "," if i + 16 < len(data) else ""
            lines.append(f"    {hex_vals}{comma}")
        lines.append("};")
        lines.append(f"const u64 userprog_{sym}_size = {len(data)};")
        lines.append("")
    # the registry
    lines.append("/* The execve name registry: name -> embedded ELF.")
    lines.append(" * Generated mechanically from src/L1/ discovery;")
    lines.append(" * consumed by core_userprog_lookup(). */")
    lines.append("const struct oc_userprog_decl OC_USERPROGS[] = {")
    for (name, *_rest) in progs:
        sym = ident(name)
        lines.append(f"    {{ \"{name}\", userprog_{sym}, "
                     f"&userprog_{sym}_size }},")
    lines.append("};")
    lines.append(f"const u64 OC_USERPROGS_COUNT = {len(progs)};")
    text = "\n".join(lines) + "\n"
    with open(HEADER, "w") as f:
        f.write(text)


def main():
    global CRT0
    os.makedirs(BUILD, exist_ok=True)
    CRT0 = build_crt0()
    # Mirror the kernel L1SVC numbering into the app library FIRST so
    # wrapper apps always compile against the current contract.
    r = subprocess.run([sys.executable,
                        os.path.join(ROOT, "tools", "gen_l1svc_ops.py")],
                       capture_output=True, text=True)
    print(r.stdout, end="")
    if r.returncode != 0:
        print(r.stderr, file=sys.stderr)
        fail("gen_l1svc_ops.py failed")

    out = build_solib()
    print(out, end="")

    progs = discover()
    names = [p[0] for p in progs]
    if len(set(names)) != len(names):
        dup = [n for n in names if names.count(n) > 1]
        fail(f"duplicate program names (folder vs flat?): {dup}")

    blobs = {}
    for (name, kind, cs, asms, *rest) in progs:
        mode = rest[0] if rest else "exec"
        if kind == "flat":
            if cs:
                data = compile_c(name, cs, mode)
            else:
                data = compile_asm(name, asms,
                                  LD_SO_LD if mode == "ld_so" else USER_LD)
        else:
            # folder app: C sources + optional asm, always plain exec.
            # _shared/crt0.o provides _start (the app defines app_main);
            # a folder program that still defines its own _start (ush)
            # links exactly as before - no crt0, no duplicate entry.
            main_c = cs[0] if cs else ""
            crt = []
            if main_c and os.path.exists(main_c) and \
               "int app_main(" in open(main_c).read():
                crt = [CRT0]
            if cs and asms:
                # link both kinds: compile asm first, then ld everything
                objs = []
                for i, a in enumerate(asms):
                    obj = os.path.join(BUILD, f"{name}_xsm{i}.o")
                    run([NASM, "-f", "elf64", "-F", "dwarf", "-g", a, "-o", obj])
                    objs.append(obj)
                for i, c in enumerate(cs):
                    obj = os.path.join(BUILD, f"{name}_x{i}.o")
                    run([CC, *CFLAGS_BASE, "-c", c, "-o", obj])
                    objs.append(obj)
                elf = os.path.join(BUILD, f"{name}.elf")
                run([LD, "-n", "-nostdlib", "-T", USER_LD,
                     "-z", "max-page-size=0x1000", "-z", "noexecstack",
                     "-o", elf, *objs])
                subprocess.run(["strip", "--strip-debug", elf], check=False)
                with open(elf, "rb") as f:
                    data = f.read()
                verify_elf(name, data, "exec")
            elif cs:
                data = compile_c(name, cs, "exec", extra_objs=crt)
            else:
                data = compile_asm(name, asms, extra_objs=crt)
        blobs[name] = data
        print(f"gen_userprogs: {name} = {len(data)} bytes [{mode}]")

    emit(progs, blobs)
    total = sum(len(b) for b in blobs.values())
    print(f"gen_userprogs: {len(progs)} programs, {total} bytes total "
          f"-> {HEADER}")


if __name__ == "__main__":
    main()
