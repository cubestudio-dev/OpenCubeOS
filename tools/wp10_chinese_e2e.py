#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - WP-10-chinese_support end-to-end tests.
#
# Usage: python3 tools/wp10_chinese_e2e.py <iso> <mode: bios|uefi> [outlog]
# Exit code: number of failed checks.
#
# Covers the full Chinese-support surface on a live boot:
#   chinese / chinesetest / font / font-test / encoding / encoding-test /
#   ime / ime-test / tui-cn (open hw app, quit, shell alive) /
#   cat of a UTF-8 file (full serial echo path) / help -w tag / uname.
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pexpect  # noqa: E402

OC_TOOLS = os.environ.get("OC_TOOLS", "/home/z/opt/extract")
QEMU = f"{OC_TOOLS}/usr/bin/qemu-system-x86_64"
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# ANSI stripping + whitespace normalization (same shape as wp10_tui_e2e).
ANSI_RE = re.compile(r"\x1b\[[0-9;?]*[a-zA-Z]|\x1b[()][A-B0-9]|\x1b[=>]")


def strip_ansi(s: str) -> str:
    return ANSI_RE.sub("", s)


def norm(s: str) -> str:
    s = strip_ansi(s)
    s = s.replace("\r\n", "\n").replace("\r", "\n")
    s = re.sub(r"[ \t]+", " ", s)
    s = re.sub(r"\n+", "\n", s)
    return s


def utf8(s: str) -> str:
    """The serial stream is latin-1-decoded by pexpect; CJK arrives as
    UTF-8 bytes - re-encode before substring checks (fix3)."""
    try:
        return s.encode("latin-1").decode("utf-8", "replace")
    except UnicodeDecodeError:
        return s


CHECKS = []


def check(name):
    CHECKS.append([name, False])
    return len(CHECKS) - 1


def main():
    iso = sys.argv[1] if len(sys.argv) > 1 else "build/opencube.iso"
    mode = sys.argv[2] if len(sys.argv) > 2 else "bios"
    outlog = sys.argv[3] if len(sys.argv) > 3 else "/tmp/chinese_e2e.log"
    timeout = int(os.environ.get("OC_E2E_TIMEOUT", "240"))

    etc_img = os.path.join(ROOT, "build", "etc.img")
    # fix3: explicit bootindex (CD first) - SeaBIOS in QEMU 10.0.13 boots
    # the IDE hard disk first when one is attached regardless of -boot d,
    # and a config disk without GRUB code hangs there forever.
    drive = ["-drive", f"file={iso},media=cdrom,if=none,id=cd0",
             "-device", "ide-cd,drive=cd0,bootindex=0"]
    if os.path.exists(etc_img):
        drive += ["-drive", f"file={etc_img},format=raw,if=none,id=hd0",
                  "-device", "ide-hd,drive=hd0,bootindex=1"]

    args = ["-m", "512M", "-no-reboot",
            "-L", f"{OC_TOOLS}/usr/share/qemu",
            "-L", f"{OC_TOOLS}/usr/share/seabios",
            "-vga", "std", "-display", "none", "-serial", "mon:stdio",
            "-netdev", "user,id=n1", "-device", "e1000,netdev=n1"] + drive
    if mode == "uefi":
        # 4M pflash pair (CODE readonly + per-run VARS copy), the same
        # shape wp10_tui_e2e.py uses.
        ovmf_code = f"{OC_TOOLS}/usr/share/OVMF/OVMF_CODE_4M.fd"
        ovmf_vars = f"{OC_TOOLS}/usr/share/OVMF/OVMF_VARS_4M.fd"
        if not os.path.exists(ovmf_code):
            ovmf_code = f"{OC_TOOLS}/usr/share/ovmf/OVMF_CODE.fd"
            ovmf_vars = f"{OC_TOOLS}/usr/share/ovmf/OVMF_VARS.fd"
        if os.path.exists(ovmf_code):
            varscopy = "/tmp/oc_cn_efi_vars.fd"
            with open(ovmf_vars, "rb") as f:
                data = f.read()
            with open(varscopy, "wb") as f:
                f.write(data)
            args = ["-m", "512M", "-no-reboot",
                    "-drive", f"if=pflash,format=raw,readonly=on,file={ovmf_code}",
                    "-drive", f"if=pflash,format=raw,file={varscopy}",
                    
                    "-L", f"{OC_TOOLS}/usr/share/qemu",
                    "-L", f"{OC_TOOLS}/usr/share/seabios",
                    "-vga", "std", "-display", "none",
                    "-serial", "mon:stdio",
                    "-netdev", "user,id=n1", "-device", "e1000,netdev=n1"] + drive
        else:
            ovmf = f"{OC_TOOLS}/usr/share/ovmf/OVMF.fd"
            args = ["-m", "512M", "-no-reboot", "-bios", ovmf,
                    
                    "-L", f"{OC_TOOLS}/usr/share/qemu",
                    "-L", f"{OC_TOOLS}/usr/share/seabios",
                    "-vga", "std", "-display", "none",
                    "-serial", "mon:stdio",
                    "-netdev", "user,id=n1", "-device", "e1000,netdev=n1"] + drive

    i_codes = {
        "boot ready": check("boot ready"),
        "cmd chinese": check("chinese status (6763+682, embedded font)"),
        "cmd chinesetest": check("chinesetest ALL PASS"),
        "cmd font": check("font lists gb2312-16x16 + active renderer"),
        "cmd font-test": check("font-test sample row rendered"),
        "cmd encoding": check("encoding status line"),
        "cmd encoding-test": check("encoding-test PASS"),
        "cmd ime": check("ime status (396/3755)"),
        "cmd ime-test": check("ime-test PASS"),
        "cmd tui-cn": check("desktop Chinese Input pad renders and returns (shell ok)"),
        "fix1 ime -i": check("fix1: ime -i composes 你 over the console (real input)"),
        "fix1 tui-cn input": check("fix1: desktop IME pad composes 你 in the pad"),
        "fix1 tui-cn reenter": check("fix1: desktop IME enter/quit/re-enter still renders (pool reuse)"),
        "cat utf8": check("cat echoes real UTF-8 bytes (zhongwen shi)"),
        "help -w tag": check("help -w lists WP-10-chinese_support"),
        "cmd count 225": check("command count 224 (tui-cn removed by tui-unified)"),
        "fix3 lang zh": check("fix3: lang zh switches report/errors/help to Chinese"),
        "fix3 lang persist": check("fix3: shell.lang=zh persisted to /etc/opencube.conf"),
        "fix3 lang invalid": check("fix3: config set shell.lang fr rejected"),
        "fix3 lang en": check("fix3: lang en restores English output + persisted en"),
        "uname ok": check("uname after full flow"),

    }

    child = pexpect.spawn(QEMU, args, timeout=timeout, encoding="latin-1",
                          codec_errors="replace", env=dict(os.environ))
    transcript = []
    try:
        child.expect(r"oc>\s*", timeout=timeout)
        transcript.append(child.before or "")

        def run(cmd, wait_prompt=True):
            child.sendline(cmd)
            if wait_prompt:
                child.expect(r"oc>\s*", timeout=timeout)
            out = child.before or ""
            transcript.append(f"\n--- [{cmd}] ---\n{out}")
            return norm(out)

        i = i_codes["boot ready"]
        CHECKS[i][1] = "ready. Type 'help' for commands" in \
            norm(child.before or "")

        out = run("chinese")
        i = i_codes["cmd chinese"]
        CHECKS[i][1] = ("6763" in out and "682" in out and
                        "216416" in out and "embedded-unifont-16x16" in out)

        out = run("chinesetest")
        i = i_codes["cmd chinesetest"]
        CHECKS[i][1] = "ALL PASS" in out and "FAIL" not in out.replace(
            "FAILURES PRESENT", "")

        out = run("font")
        i = i_codes["cmd font"]
        CHECKS[i][1] = "gb2312-16x16" in out and "ascii-8x16" in out

        out = run("font-test")
        i = i_codes["cmd font-test"]
        CHECKS[i][1] = "font-test" in out and "16x16" in out

        out = run("encoding")
        i = i_codes["cmd encoding"]
        CHECKS[i][1] = "gb2312+utf8+ascii" in out

        out = run("encoding-test")
        i = i_codes["cmd encoding-test"]
        CHECKS[i][1] = "PASS" in out

        out = run("ime")
        i = i_codes["cmd ime"]
        CHECKS[i][1] = "396" in out and "3755" in out

        out = run("ime-test")
        i = i_codes["cmd ime-test"]
        CHECKS[i][1] = "PASS" in out

        # WP-10-tui-unified: the tui-cn launcher is GONE (225 -> 224
        # commands); the IME pad now lives on the unified desktop
        # (root -> "Chinese Input", the same tui_cn_input_app surface).
        # Single-byte sends (sendline would append \r\n and the extra
        # \r would re-commit the pad line); the serial ESC needs the
        # double-ESC trick, and a second Esc at the desktop root leaves
        # the desktop back to the shell.
        # NOTE: the serial backend must be configured or the desktop
        # renders to the (invisible) framebuffer; and the desktop root
        # selection persists across sessions, so clamp to the top first
        # (up x10, no wrap at the root) before moving down.
        child.sendline("echo tui.backend=serial >> /etc/opencube.conf")
        child.expect(r"oc>\s*", timeout=timeout)
        child.sendline("tui")
        time.sleep(3)
        # digit quick-open: '7' = root child 6 = Chinese Input (the
        # desktop opens the Nth entry directly - no arrow drift)
        child.send("7")
        time.sleep(3.5)               # pad init (font + repaint) under TCG
        child.expect([r"oc>\s*", pexpect.TIMEOUT], timeout=2)
        pad_open = norm(child.before or "")
        transcript.append(f"\n--- [desktop ime] ---\n{pad_open[-300:]}")
        try:
            raw = pad_open.encode("latin-1").decode("utf-8", "replace")
        except UnicodeDecodeError:
            raw = pad_open
        ime_rendered = "\u62fc\u97f3" in raw   # 拼音
        child.send("\x1b\x1b")        # Esc: pad -> desktop root
        time.sleep(1.5)
        child.send("\x1b\x1b")        # Esc: desktop root -> shell
        child.expect(r"oc>\s*", timeout=timeout)
        out = norm(child.before or "")
        transcript.append(f"\n--- [desktop ime exit] ---\n{out[-300:]}")
        i = i_codes["cmd tui-cn"]
        CHECKS[i][1] = ime_rendered

        # ---- fix1: ime -i interactive console composition (1a) ----
        # letters compose pinyin, a digit commits the candidate, Enter
        # ends the line; the console IME line redraws in place and the
        # summary carries the committed text through the serial tee.
        child.sendline("ime -i")
        time.sleep(1.5)
        for ch in "ni1":
            child.send(ch)
            time.sleep(0.4)
        child.send("\r")
        child.expect(r"oc>\s*", timeout=timeout)
        out = norm(child.before or "")
        transcript.append(f"\n--- [ime -i] ---\n{out}")
        i = i_codes["fix1 ime -i"]
        try:
            raw = out.encode("latin-1").decode("utf-8", "replace")
        except UnicodeDecodeError:
            raw = out
        CHECKS[i][1] = "composed 1 character(s)" in raw and "你" in raw

        # ---- fix1: the desktop IME pad input surface (1a, TUI) ----
        # same compose flow the tui-cn launcher entry 4 used to drive:
        # desktop root -> Chinese Input -> type ni -> commit candidate 1
        # -> Enter commits the line into the pad.
        child.sendline("echo tui.backend=serial >> /etc/opencube.conf")
        child.expect(r"oc>\s*", timeout=timeout)
        child.sendline("tui")
        time.sleep(3)
        child.send("7")               # quick-open: Chinese Input
        time.sleep(3.5)               # pad init (font + repaint) under TCG
        child.expect([r"oc>\s*", pexpect.TIMEOUT], timeout=2)
        menu_out = norm(child.before or "")
        for ch in "ni":
            child.send(ch)
            time.sleep(0.4)
        time.sleep(1)
        child.expect([r"oc>\s*", pexpect.TIMEOUT], timeout=2)
        cand_out = norm(child.before or "")
        child.send("1")               # commit the first candidate
        time.sleep(0.6)
        child.send("\r")              # commit the line to the pad
        time.sleep(1.2)
        child.expect([r"oc>\s*", pexpect.TIMEOUT], timeout=2)
        pad_out = norm(child.before or "")
        transcript.append(f"\n--- [desktop ime input pad] ---\n{menu_out[-300:]}\n{cand_out[-300:]}\n{pad_out[-300:]}")
        # leave the pad (Esc -> desktop root) and the desktop (Esc ->
        # shell); double-ESC on the serial bridge delivers one real Esc
        child.send("\x1b\x1b")
        time.sleep(1.0)
        child.send("\x1b\x1b")
        child.expect(r"oc>\s*", timeout=timeout)
        out = norm(child.before or "")
        transcript.append(f"\n--- [desktop ime pad exit] ---\n{out[-300:]}")
        i = i_codes["fix1 tui-cn input"]
        try:
            raw = (menu_out + cand_out + pad_out).encode("latin-1")\
                .decode("utf-8", "replace")
        except UnicodeDecodeError:
            raw = menu_out + cand_out + pad_out
        CHECKS[i][1] = ("拼音" in raw and "你" in raw)

        # ---- fix1: desktop -> IME enter/quit/re-enter (1c, pool reuse) ----
        ok2 = True
        for _round in range(2):
            child.sendline("tui")
            time.sleep(3)
            child.send("7")           # quick-open: Chinese Input
            time.sleep(3.5)           # pad init (font + repaint) under TCG
            child.expect([r"oc>\s*", pexpect.TIMEOUT], timeout=2)
            m_out = norm(child.before or "")
            try:
                # the serial stream is latin-1-decoded by pexpect; CJK
                # arrives as UTF-8 bytes - re-encode before substring
                # checks (same as the pad check above)
                m_raw = m_out.encode("latin-1").decode("utf-8", "replace")
            except UnicodeDecodeError:
                m_raw = m_out
            rendered = "\u62fc\u97f3" in m_raw   # 拼音 (pad banner)
            child.send("\x1b\x1b")   # pad -> desktop root
            time.sleep(1.2)
            child.send("\x1b\x1b")   # desktop root -> shell
            child.expect([r"oc>\s*", pexpect.TIMEOUT], timeout=6)
            e_out = norm(child.before or "")
            transcript.append(f"\n--- [desktop ime reenter] ---\n{m_out[-200:]}\n{e_out[-200:]}")
            if not rendered:
                ok2 = False
        i = i_codes["fix1 tui-cn reenter"]
        CHECKS[i][1] = ok2

        # cat the UTF-8 file written by chinesetest; the serial mirror
        # carries the raw UTF-8 bytes back to us.
        run("chinesetest")
        out = run("cat /tmp/cn.txt")
        i = i_codes["cat utf8"]
        try:
            raw = out.encode("latin-1").decode("utf-8")
        except UnicodeDecodeError:
            raw = out
        CHECKS[i][1] = "中文测试" in raw

        out = run("help -w")
        i = i_codes["help -w tag"]
        CHECKS[i][1] = "WP-10-chinese_support" in out

        out = run("help")
        i = i_codes["cmd count 225"]
        m = re.search(r"Total:\s*(\d+) commands", out)
        # WP-10-tui-unified removed the tui-cn launcher: 225 -> 224
        # (fix3: suspend/suspend_test hidden, 222 -> 220; `lang` added
        # 221 -> 225; fix4: +tui-time/tui-drv/tui-sys/tui-ext; unified:
        # -tui-cn -> 224).
        CHECKS[i][1] = bool(m) and m.group(1) == "224"
        CHECKS[i][1] = CHECKS[i][1] and "suspend" not in out.lower()

        # ---- fix3: user-configurable shell output language ----
        # default is en; `lang` (no args) reports, `lang en|zh` switches
        # AND persists via /etc/opencube.conf; the report line, the
        # unknown-command message and the help headers follow the live
        # language (UTF-8 over the serial mirror).
        out = run("lang")
        ok_lang = "shell output language: en" in out

        # switching to zh takes effect immediately - the confirmation
        # line is served in the NEW language (zh), then persisted
        out = run("lang zh")
        ok_lang = ok_lang and "已设为 zh" in utf8(out)

        out = run("config get shell.lang")
        i = i_codes["fix3 lang persist"]
        CHECKS[i][1] = ok_lang and out.strip().endswith("zh")

        # live Chinese output: report line, unknown command, help header
        out = run("lang")
        out_unknown = run("definitely_not_a_command")
        out_help = run("help")
        zh_raw = utf8(out) + utf8(out_unknown) + utf8(out_help)
        i = i_codes["fix3 lang zh"]
        CHECKS[i][1] = ("Shell 输出语言：zh" in zh_raw and
                        "未知命令：definitely_not_a_command" in zh_raw and
                        "可用命令（A-Z）" in zh_raw)

        # invalid values are rejected by the config writer
        out = run("config set shell.lang fr")
        i = i_codes["fix3 lang invalid"]
        CHECKS[i][1] = "rejected" in out

        # back to English via the runtime switch; the persisted value
        # must return to en so later boots (uname, regression) stay en
        out = run("lang en")
        ok_en = "shell output language set to en" in out
        out = run("definitely_not_a_command")
        ok_en = ok_en and "unknown command: definitely_not_a_command" in out
        out = run("config get shell.lang")
        ok_en = ok_en and out.strip().endswith("en")
        i = i_codes["fix3 lang en"]
        CHECKS[i][1] = ok_en

        out = run("uname -a")
        i = i_codes["uname ok"]
        CHECKS[i][1] = "Open Cube OS" in out
    except Exception as e:
        transcript.append(f"\n[EXCEPTION] {e}\n")
    finally:
        child.terminate(force=True)
        with open(outlog, "w", encoding="latin-1") as f:
            f.write("".join(transcript))

    passed = sum(1 for _, ok in CHECKS if ok)
    for name, ok in CHECKS:
        print(f"  {'PASS' if ok else 'FAIL'}  {name}")
    print(f"=== {passed}/{len(CHECKS)} chinese E2E checks passed "
          f"({mode}) ===")
    return len(CHECKS) - passed


if __name__ == "__main__":
    sys.exit(main())
