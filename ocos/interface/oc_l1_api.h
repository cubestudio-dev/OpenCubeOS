/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS NAK - ocos/interface: the ring-3 (L1) API surface.
 *
 * This directory is the ONLY header set a ring-3 application may
 * include (enforced at build time: the L1 compile -I exports exactly
 * this directory - see tools/gen_userprogs.py CFLAGS_BASE).  Nothing
 * under ocos/kernel, ocos/drivers or ocos/services is ever on an L1
 * include path.
 *
 * Header map (the L1 interface classification):
 *   l1_lib.h      - the ring-3 runtime library (syscalls, console,
 *                   string helpers, heap, program spawn helpers)
 *   l1svc_ops.h   - the SYS_L1SVC category/op numbering (mirrored
 *                   verbatim from the kernel's
 *                   services/sysapi/interface/svc_sysapi_l1svc.h by
 *                   tools/gen_l1svc_ops.py)
 *   fs_lib.h      - the ring-3 file library (paths, glob, fs helpers)
 *   editor_core.h - the shared line/column editor core (vi/nano/edit)
 *   i18n.h        - the per-app bilingual tables (tr_() lookups)
 *   crt0.asm      - the shared ring-3 entry trampoline (_start ->
 *                   app_main(argc, argv); links before any prologue)
 */
#ifndef OC_L1_API_H
#define OC_L1_API_H

/* Umbrella: include the full ring-3 API surface.  Apps usually pick
 * the specific headers they need; this file is the classification
 * entry point (and the single place to audit what L1 can see). */
#include "l1_lib.h"
#include "l1svc_ops.h"
#include "fs_lib.h"
#include "editor_core.h"
#include "i18n.h"

#endif /* OC_L1_API_H */
