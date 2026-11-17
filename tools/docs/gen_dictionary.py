#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""Application-dictionary generator (WP-docs-cleanup).

Builds the per-command application dictionary pages (EN + zh) from
three FACT sources - nothing is authored from memory:

  1. the enum fact list   (tools/docs/enum_apps.sh --format json)
  2. the captured examples (tools/docs/cap_examples.py JSONL - every
     example is a real executed session transcript)
  3. the per-app i18n _hint (ocos/l1apps/<app>/i18n/{en,zh}.json -
     the authoritative one-line description both languages)

Entry shape per command (the work-package contract): name, category,
function, usage, parameters (incl. hidden ones, each marked with the
compatibility disclaimer), return codes, executed example.

Output: docs/manual/dictionary/*.md + docs/zh/manual/dictionary/*.md
Usage:
  python3 tools/docs/gen_dictionary.py --facts F.json --examples E.jsonl
"""
import argparse
import json
import os
import re
import sys

HIDDEN_DISCLAIMER_EN = ("hidden parameter, not a public interface; "
                        "compatibility is not guaranteed")
HIDDEN_DISCLAIMER_ZH = ("隐藏参数，非公开接口，兼容性不保证")

CATS_EN = {
    "time": "Time and clocks", "block": "Block devices and disk tools",
    "nic": "Network cards", "snd": "Audio", "power": "Power management",
    "ota": "OTA updates", "fs": "File systems", "net": "Networking",
    "health": "Health monitoring", "klog": "Kernel log",
    "watchdog": "Watchdog", "crypto": "Cryptography",
    "l1": "L1 runtime", "display": "Display", "input": "Input",
    "camera": "Cameras", "usb": "USB", "ocembed": "OCIRE embed channel",
    "diag": "Diagnostics", "tui": "Full-screen TUI family",
}
CATS_ZH = {
    "time": "时间与时钟", "block": "块设备与磁盘工具", "nic": "网卡",
    "snd": "音频", "power": "电源管理", "ota": "OTA 更新",
    "fs": "文件系统", "net": "网络", "health": "健康监控",
    "klog": "内核日志", "watchdog": "看门狗", "crypto": "密码学",
    "l1": "L1 运行时", "display": "显示", "input": "输入",
    "camera": "摄像头", "usb": "USB", "ocembed": "OCIRE 内嵌通道",
    "diag": "诊断", "tui": "全屏 TUI 家族",
}

# self-contained app grouping (apps whose logic lives in their own
# main.c rather than an L0 service)
SELF_GROUPS = {
    "tools-file": ["awk", "cat", "chmod", "chown", "cp", "df", "du",
                   "grep", "head", "ln", "ls", "mkdir", "mv", "rm",
                   "rmdir", "sort", "stat", "tail", "touch", "uniq",
                   "wc", "files"],
    "tools-system": ["clear", "crashlog", "dmesg", "echo", "free",
                     "help", "kill", "ps", "pwd", "run", "sleep",
                     "spawn", "top", "write", "sysdiag", "hwinfo"],
    "tools-net": ["ifconfig", "netstat", "ping", "wget", "dhcp",
                  "net-tool"],
    "editors": ["edit", "nano", "vi"],
    "ime": ["chinese", "chinesetest"],
    "mega": ["update", "disk-tool", "net-tool", "sound-tool", "files",
             "sysdiag", "hwinfo", "power"],
}
SELF_GROUP_NAMES_EN = {
    "tools-file": "File and text tools", "tools-system": "System tools",
    "tools-net": "Network tools", "editors": "Editors",
    "ime": "Chinese console and IME", "mega": "The 8 merged apps",
}
SELF_GROUP_NAMES_ZH = {
    "tools-file": "文件与文本工具", "tools-system": "系统工具",
    "tools-net": "网络工具", "editors": "编辑器",
    "ime": "中文控制台与输入法", "mega": "八大融合应用",
}

FLAT_GROUP_EN = "Kernel-era test programs (flat sources)"
FLAT_GROUP_ZH = "内核时代测试程序（散装源码）"
BUILTIN_GROUP_EN = "ush built-in commands"
BUILTIN_GROUP_ZH = "ush 内建命令"


def load_hints(l1dir):
    en, zh = {}, {}
    for d in os.listdir(l1dir):
        p = os.path.join(l1dir, d, "i18n")
        if not os.path.isdir(p):
            continue
        for lang, store in (("en", en), ("zh", zh)):
            fp = os.path.join(p, lang + ".json")
            if os.path.exists(fp):
                try:
                    j = json.load(open(fp, encoding="utf-8"))
                    if j.get("_hint"):
                        store[d] = j["_hint"]
                except Exception:
                    pass
    return en, zh


def group_of(c):
    if c["kind"] == "builtin":
        return ("builtin", BUILTIN_GROUP_EN, BUILTIN_GROUP_ZH)
    if c["kind"].startswith("flat"):
        return ("flat", FLAT_GROUP_EN, FLAT_GROUP_ZH)
    if c["name"] in SELF_GROUPS["mega"]:
        return ("mega", SELF_GROUP_NAMES_EN["mega"],
                SELF_GROUP_NAMES_ZH["mega"])
    if c.get("l1svc") and c.get("category") in CATS_EN:
        k = c["category"]
        return (k, CATS_EN[k], CATS_ZH[k])
    for g, names in SELF_GROUPS.items():
        if c["name"] in SELF_GROUPS[g]:
            return (g, SELF_GROUP_NAMES_EN[g], SELF_GROUP_NAMES_ZH[g])
    return ("tools-system", SELF_GROUP_NAMES_EN["tools-system"],
            SELF_GROUP_NAMES_ZH["tools-system"])


def clean_example(out):
    """Verbatim modulo line-ending normalization."""
    s = out.replace("\r\r\n", "\n").replace("\r\n", "\n").replace("\r", "\n")
    s = re.sub(r"\n{3,}", "\n\n", s)
    return s.strip("\n")


def clean_usage_line(s):
    """One clean usage line from a raw captured string."""
    s = s.split("\\n")[0]
    s = s.rstrip(" \t")
    s = s.rstrip(');"')
    s = s.replace("\\n", " ").strip()
    return s


def params_from_usage(usage, usage_strings):
    blob = " ".join([usage or ""] + (usage_strings or []))
    ms = re.findall(r"<(\w+)>", blob)
    seen, out = set(), []
    for m in ms:
        if m not in seen:
            seen.add(m)
            out.append(m)
    return out


def entry_md(c, ex, hints, lang):
    zh = (lang == "zh")
    name = c["name"]
    gkey, gen, gzh = group_of(c)
    gname = gzh if zh else gen
    func = hints.get(name, "") or c.get("src_header") or ""
    usage = clean_usage_line((c.get("usage") or "").strip())
    usages = [clean_usage_line(u) for u in (c.get("usage_strings") or [])]
    usages = [u for u in usages if u and not u.startswith("usage();")]
    # dedupe against the primary usage line and strip the prefix
    def _strip_prefix(u):
        return u[7:].strip() if u.startswith("usage: ") else u
    usage = _strip_prefix(usage)
    usages = [_strip_prefix(u) for u in usages]
    usages = [u for u in usages if u and u != usage
              and not u.startswith("(")]
    params = params_from_usage(usage, usages)
    hidden = c.get("hidden_args") or []
    rets = c.get("return_codes") or []
    L = []
    L.append("### %s" % name)
    L.append("")
    L.append("| %s | %s |" % ("字段" if zh else "Field",
                             "值" if zh else "Value"))
    L.append("|---|---|")
    L.append("| %s | %s |" % ("名称" if zh else "Name", name))
    L.append("| %s | %s |" % ("分类" if zh else "Category", gname))
    if func:
        L.append("| %s | %s |" % ("功能" if zh else "Function", func))
    if usage or usages:
        u = usage + ("; " + " ; ".join(usages) if usages else "")
        L.append("| %s | `%s` |" % ("用法" if zh else "Usage", u))
    if params:
        L.append("| %s | %s |" % ("参数" if zh else "Parameters",
                                  ", ".join("`%s`" % p for p in params)))
    if hidden:
        L.append("| %s | %s: %s |" % (
            "隐藏参数" if zh else "Hidden parameters",
            HIDDEN_DISCLAIMER_ZH if zh else HIDDEN_DISCLAIMER_EN,
            ", ".join("`%s`" % h for h in hidden)))
    if c.get("mega") and c.get("ops"):
        L.append("| %s | %s |" % (
            "操作表" if zh else "Op table",
            ", ".join(o["op"] for o in c["ops"])))
    if rets:
        L.append("| %s | %s |" % (
            "返回码" if zh else "Return codes",
            ", ".join(str(r) for r in rets)))
    if c.get("handler_file"):
        L.append("| %s | `%s` |" % ("来源" if zh else "Source",
                                    c["handler_file"]))
    L.append("")
    ex_out = ex.get("output") if ex else ""
    if ex_out and ex.get("note", "") and not ex["note"].startswith(
            ("retry OK", "power-real")):
        pass
    if ex_out:
        L.append("**%s**：" % ("实际执行示例" if zh else
                               "Executed example")
                 if zh else
                 "**%s**:" % "Executed example")
        L.append("")
        L.append("```")
        L.append(clean_example(ex_out))
        L.append("```")
        note = ex.get("note", "")
        if note:
            L.append("")
            L.append("(%s: %s)" % ("备注" if zh else "Note", note))
    elif ex:
        L.append("**%s**" % ("（本次采集会话未捕获到串行输出——"
                             "该命令的输出呈现在 VGA 屏幕上或无串行回显）"
                             if zh else
                             "(no serial output captured in the session "
                             "- this command renders on the VGA screen "
                             "or produces no serial echo)"))
    L.append("")
    return "\n".join(L)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--facts", required=True)
    ap.add_argument("--examples", required=True)
    ap.add_argument("--out-en", default="docs/manual/dictionary")
    ap.add_argument("--out-zh", default="docs/zh/manual/dictionary")
    args = ap.parse_args()

    facts = json.load(open(args.facts))
    examples = {}
    for line in open(args.examples):
        r = json.loads(line)
        examples[r["name"]] = r

    l1dir = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                         "..", "..", "ocos", "l1apps")
    hints_en, hints_zh = load_hints(os.path.abspath(l1dir))

    for lang, outdir, hints, idx_title, idx_intro in (
        ("en", args.out_en, hints_en,
         "Application dictionary",
         "Every user command of the running system, one entry each: "
         "name, category, function, parameters (hidden ones marked), "
         "return codes and a REAL executed example (captured from a "
         "live boot of the release ISO; outputs are pasted verbatim "
         "from the serial session)."),
        ("zh", args.out_zh, hints_zh,
         "应用字典",
         "运行系统的全部用户命令，逐条登记：名称、分类、功能、参数"
         "（隐藏参数特别标注）、返回码，以及一条真实执行过的示例"
         "（在发布 ISO 的实机引导中采集；示例输出为串行会话原样"
         "贴入，未作改写）。")):
        os.makedirs(outdir, exist_ok=True)
        groups = {}
        for c in facts["commands"]:
            gkey, gen, gzh = group_of(c)
            groups.setdefault((gkey, gen, gzh), []).append(c)
        n = len(facts["commands"])
        idx = []
        idx.append("<!-- SPDX-License-Identifier: Apache-2.0 -->")
        idx.append("# %s (%d %s)" % (idx_title, n,
                                     "条命令" if lang == "zh"
                                     else "commands"))
        idx.append("")
        idx.append(idx_intro)
        idx.append("")
        idx.append("| %s | %s |" % ("分类" if lang == "zh" else
                                    "Category",
                                    "命令数" if lang == "zh" else
                                    "Commands"))
        idx.append("|---|---|")
        for (gkey, gen, gzh), cs in sorted(groups.items(),
                                           key=lambda x: x[0][0]):
            fn = gkey + ".md"
            idx.append("| [%s](%s) | %d |" % (gzh if lang == "zh"
                                              else gen, fn, len(cs)))
            page = []
            page.append("<!-- SPDX-License-Identifier: Apache-2.0 -->")
            page.append("# %s - %s (%d)" % (
                idx_title, gzh if lang == "zh" else gen, len(cs)))
            page.append("")
            for c in sorted(cs, key=lambda x: x["name"]):
                page.append(entry_md(c, examples.get(c["name"]),
                                     hints, lang))
            with open(os.path.join(outdir, fn), "w",
                      encoding="utf-8") as f:
                f.write("\n".join(page))
        with open(os.path.join(outdir, "index.md"), "w",
                  encoding="utf-8") as f:
            f.write("\n".join(idx) + "\n")
        print("[gen] %s: %d entries, %d category pages" %
              (lang, n, len(groups)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
