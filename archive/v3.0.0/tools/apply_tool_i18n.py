#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 -*-
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""
tools/apply_tool_i18n.py - route the file-tool apps' user-facing
sentences through tr_() (the gettext model: the key IS the English
sentence - en.json maps it to itself, zh.json carries the Chinese).
Column headers / state words / separators stay English on purpose
(the zh_CN locale convention: translated sentences, English column
skeleton - CJK width would break the alignment).

The ZH keys carry REAL newlines (\n in this source is the escape);
the first version emitted literal backslash-n keys that could never
match the compiled C literals - caught live by the zh spot battery.
"""
import json
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

ZH = {
 "cat": {"cat: cannot open ": "cat：无法打开 "},
 "cp": {"usage: cp <src> <dst>\n": "用法：cp <源> <目标>\n",
        "cp: cannot open '": "cp：无法打开 '"},
 "mv": {"usage: mv <src> <dst>\n": "用法：mv <源> <目标>\n",
        "mv: cannot open '": "mv：无法打开 '"},
 "rm": {"usage: rm <file>\n": "用法：rm <文件>\n",
        "rm: cannot remove '": "rm：无法删除 '"},
 "mkdir": {"usage: mkdir <path>\n": "用法：mkdir <路径>\n",
           "mkdir: cannot create '": "mkdir：无法创建 '"},
 "rmdir": {"usage: rmdir <path>\n": "用法：rmdir <路径>\n",
           "rmdir: cannot remove '": "rmdir：无法删除 '"},
 "df": {" files (kernel df for full mount table)\n":
        " 个文件（完整挂载表见内核 df）\n"},
 "grep": {"usage: grep <pattern>\n": "用法：grep <模式>\n"},
 "head": {"head: cannot open ": "head：无法打开 "},
 "tail": {"tail: cannot open ": "tail：无法打开 "},
 "wc": {"wc: cannot open ": "wc：无法打开 "},
 "sort": {"sort: cannot open ": "sort：无法打开 "},
 "uniq": {"uniq: cannot open ": "uniq：无法打开 "},
 "touch": {"usage: touch <file>\n": "用法：touch <文件>\n"},
 "ln": {"ln: cannot create symlink (target must be an absolute path)\n":
        "ln：无法创建符号链接（目标必须是绝对路径）\n",
        "usage: ln <old> <new> | ln -s <abs-target> <linkpath>\n":
        "用法：ln <旧> <新> | ln -s <绝对目标> <链接路径>\n",
        "ln: hard link failed (rc=": "ln：硬链接失败（rc="},
 "chmod": {"usage: chmod <mode> <path>   (mode in octal, e.g. 644)\n":
           "用法：chmod <模式> <路径>（模式为八进制，如 644）\n",
           "chmod: cannot change mode\n": "chmod：无法修改模式\n"},
 "chown": {"usage: chown <uid>[:<gid>] <path>\n":
           "用法：chown <uid>[:<gid>] <路径>\n",
           "chown: cannot change owner\n": "chown：无法修改属主\n"},
 "stat": {"usage: stat <path>\n": "用法：stat <路径>\n",
          "stat: cannot stat '": "stat：无法获取状态 '",
          "  File: ": "  文件：",
          "  Type: ": "  类型：",
          "directory": "目录",
          "device": "设备",
          "regular file": "普通文件",
          "  Size: ": "  大小：",
          " bytes\n": " 字节\n",
          "  Mode: ": "  模式：",
          "  Uid:  ": "  Uid：",
          "  Gid: ": "  Gid：",
          "  Links: ": "  链接数："},
 "free": {"free: SYS_MEMINFO unavailable (kernel too old)\n":
          "free：SYS_MEMINFO 不可用（内核过旧）\n"},
 "ps": {"ps: SYS_PS unavailable (kernel too old)\n":
        "ps：SYS_PS 不可用（内核过旧）\n"},
 "date": {"Open Cube OS uptime: ": "Open Cube OS 运行时长：",
          " ms since boot (no RTC driver)\n": " 毫秒（自启动起，无 RTC 驱动）\n"},
}


def main():
    """Apply the tr_() routing to every tool main.c + (re)write the
    json resources.  Idempotent: already-routed literals are skipped
    (the needle no longer matches)."""
    changed = 0
    for app, table in ZH.items():
        src_path = os.path.join(ROOT, "src", "L1", app, "main.c")
        if not os.path.exists(src_path):
            print(f"SKIP {app}: no main.c")
            continue
        src = open(src_path, encoding="utf-8").read()
        orig = src
        for en in table:
            needle = 'puts_("%s")' % en
            repl = 'puts_(tr_("%s"))' % en
            if needle in src:
                src = src.replace(needle, repl)
        if src != orig:
            open(src_path, "w", encoding="utf-8").write(src)
            changed += 1
        for lang, mapping in (("en", {k: k for k in table}),
                              ("zh", table)):
            jp = os.path.join(ROOT, "src", "L1", app, "i18n",
                              f"{lang}.json")
            with open(jp, encoding="utf-8") as f:
                d = json.load(f)
            d.update(mapping)
            with open(jp, "w", encoding="utf-8") as f:
                json.dump(d, f, ensure_ascii=False, indent=2)
                f.write("\n")
    print(f"tools rewritten: {changed}")


if __name__ == "__main__":
    main()
