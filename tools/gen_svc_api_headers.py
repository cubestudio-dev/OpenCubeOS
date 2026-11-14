#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""
gen_svc_api_headers.py: emit one svc_<name>_api.h umbrella header per
NAK service (ocos/services/<name>/interface/).

The umbrella is the service's single canonical include point for
outside consumers: it pulls in every public header the service exports
(the interface/ residents) and documents the ring-3 surface category.
Idempotent: an existing umbrella is preserved verbatim (tui's
hand-migrated svc_tui_api.h stays untouched).
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SVC_ROOT = os.path.join(ROOT, "ocos", "services")

services = sorted(d for d in os.listdir(SVC_ROOT)
                  if os.path.isdir(os.path.join(SVC_ROOT, d)))

for svc in services:
    iface = os.path.join(SVC_ROOT, svc, "interface")
    os.makedirs(iface, exist_ok=True)   # every NAK service HAS an interface/
    api = os.path.join(iface, "svc_%s_api.h" % svc)
    if os.path.exists(api):
        print("keep existing:", os.path.relpath(api, ROOT))
        continue
    publics = sorted(f for f in os.listdir(iface)
                     if f.endswith(".h") and f != "svc_%s_api.h" % svc)
    guard = "SVC_%s_API_H" % svc.upper()
    lines = [
        "/* SPDX-License-Identifier: Apache-2.0 */",
        "/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */",
        "/* NAK service API umbrella: %s." % svc,
        " * Ring-3 surface: the L1SVC \"%s\" category (op numbering in" % svc,
        " * ocos/interface/l1svc_ops.h).  Kernel-side public headers:",
        " * every interface/ resident below. */",
        "#ifndef %s" % guard,
        "#define %s" % guard,
        "",
    ]
    for h in publics:
        lines.append('#include "%s"' % h)
    if not publics:
        lines.append("/* This service has no kernel-side public headers yet;")
        lines.append(" * its surface is the ring-3 L1SVC category only. */")
    lines += ["", "#endif /* %s */" % guard, ""]
    open(api, "w", encoding="utf-8").write("\n".join(lines))
    print("generated:", os.path.relpath(api, ROOT))
print("done: %d services" % len(services))
