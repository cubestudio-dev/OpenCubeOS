#!/usr/bin/env python3
"""Run QEMU ISO boot and execute shell commands, capturing output.

Usage: python3 qemu_runner.py <iso_path> <command1> [command2 ...]
"""
import sys, os, re, time, signal
import pexpect

# Setup LD_LIBRARY_PATH + QEMU paths
OC_TOOLS = "/home/z/my-project/oc-tools-debs/extract"
LIB_PATHS = []
for d in sorted(os.listdir(OC_TOOLS)):
    libdir = f"{OC_TOOLS}/{d}/usr/lib/x86_64-linux-gnu"
    if os.path.isdir(libdir):
        LIB_PATHS.append(libdir)
LIB_PATHS.append(f"{OC_TOOLS}/qemu-system-x86/usr/lib/x86_64-linux-gnu")
ENV_LD = ":".join(LIB_PATHS)

QEMU_BIN = f"{OC_TOOLS}/qemu-system-x86/usr/bin/qemu-system-x86_64"
SEABIOS_DIR = f"{OC_TOOLS}/seabios/usr/share/seabios"

def strip_ansi(s):
    s = re.sub(r'\x1b\[[0-9;]*[a-zA-Z]', '', s)
    s = re.sub(r'\x1b\[\?\d+[a-zA-Z]', '', s)
    s = re.sub(r'c\x1b', '', s)
    return s

def run_qemu_commands(iso_path, commands, timeout_per_cmd=30):
    """Boot ISO, run commands, return all output."""
    cmd = [
        QEMU_BIN,
        "-m", "512M",
        "-cdrom", iso_path,
        "-boot", "d",
        "-no-reboot",
        "-L", SEABIOS_DIR,
        "-vga", "std", "-display", "none",
        "-serial", "mon:stdio",
        # User-mode networking + e1000 NIC (so the kernel e1000 driver finds it)
        "-netdev", "user,id=n1",
        "-device", "e1000,netdev=n1",
    ]
    env = os.environ.copy()
    env["LD_LIBRARY_PATH"] = ENV_LD

    print(f"[runner] starting QEMU: {os.path.basename(iso_path)}")
    print(f"[runner] will run {len(commands)} command(s): {commands}")
    
    child = pexpect.spawn(cmd[0], cmd[1:], env=env, timeout=60,
                          encoding='utf-8', codec_errors='replace')
    
    all_output = []
    try:
        # Wait for boot prompt
        child.expect(r"oc>\s*", timeout=30)
        boot_output = child.before
        all_output.append(("BOOT", strip_ansi(boot_output)))
        print(f"[runner] boot complete ({len(strip_ansi(boot_output))} chars)")
        
        for c in commands:
            print(f"[runner] sending: {c}")
            child.sendline(c)
            try:
                child.expect(r"oc>\s*", timeout=timeout_per_cmd)
                out = child.before
                # Strip the echoed command line at start
                lines = strip_ansi(out).split('\n', 1)
                if len(lines) > 1 and lines[0].strip() == c.strip():
                    out = lines[1]
                else:
                    out = strip_ansi(out)
                all_output.append((c, out))
                print(f"[runner] got {len(out)} chars for '{c}'")
            except pexpect.TIMEOUT:
                print(f"[runner] TIMEOUT for '{c}'")
                all_output.append((c, f"<<TIMEOUT after {timeout_per_cmd}s>>"))
            except pexpect.EOF:
                print(f"[runner] EOF for '{c}'")
                all_output.append((c, "<<EOF>>"))
                break
    finally:
        try:
            child.kill(signal.SIGKILL)
        except:
            pass
        child.close(force=True)
    
    return all_output

def main():
    if len(sys.argv) < 3:
        print(f"usage: {sys.argv[0]} <iso> <cmd1> [cmd2 ...]")
        sys.exit(1)
    iso = sys.argv[1]
    cmds = sys.argv[2:]
    
    out = run_qemu_commands(iso, cmds, timeout_per_cmd=120)
    
    print("\n" + "=" * 60)
    print("OUTPUT SUMMARY")
    print("=" * 60)
    for label, content in out:
        print(f"\n--- [{label}] ---")
        # Strip the last 'oc>' prompt line
        print(content.rstrip())

if __name__ == "__main__":
    main()
