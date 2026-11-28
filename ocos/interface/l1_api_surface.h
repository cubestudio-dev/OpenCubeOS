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
 * Header map (the L1 interface classification, NAK l1_<class>_<role>):
 *   l1_api_syscall.h  - api/syscall: the ring-3 runtime library (syscalls,
 *                       console, string helpers, heap, spawn helpers)
 *   l1_svc_ops.h      - svc/ops: the SYS_L1SVC category/op numbering
 *                       (mirrored verbatim from the kernel's
 *                       services/sysapi/interface/svc_sysapi_l1svc.h by
 *                       tools/gen_l1svc_ops.py)
 *   l1_fs_api.h       - fs/api: the ring-3 file library (paths, glob,
 *                       fs helpers)
 *   l1_i18n_api.h     - i18n/api: the per-app bilingual tables (tr_())
 *   l1_i18n_res.h     - i18n/res: the shared MSG_ID message numbering
 *   l1_wizard_api.h   - wizard/api: the interactive wizard primitives
 *   l1_editor_core.h  - editor/core: the shared line/column editor core
 *                       (vi/nano/edit)
 *   l1_diag_cmds.h    - diag/cmds: the shared dev/drv diag command layer
 *   l1_crt_entry.asm  - crt/entry: the shared ring-3 entry trampoline
 *                       (_start -> app_main(argc, argv); links before any
 *                       compiler prologue)
 */
#ifndef OC_L1_API_H
#define OC_L1_API_H

/* Umbrella: include the full ring-3 API surface.  Apps usually pick
 * the specific headers they need; this file is the classification
 * entry point (and the single place to audit what L1 can see). */
#include "l1_api_syscall.h"
#include "l1_svc_ops.h"
#include "l1_fs_api.h"
#include "l1_editor_core.h"
#include "l1_i18n_api.h"

#endif /* OC_L1_API_H */
