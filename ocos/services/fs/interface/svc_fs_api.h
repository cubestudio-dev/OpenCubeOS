/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* NAK service API umbrella: fs.
 * Ring-3 surface: the L1SVC "fs" category (op numbering in
 * ocos/interface/l1svc_ops.h).  Kernel-side public headers:
 * every interface/ resident below. */
#ifndef SVC_FS_API_H
#define SVC_FS_API_H

#include "svc_fs_exfat.h"
#include "svc_fs_ext4.h"
#include "svc_fs_fat32.h"
#include "svc_fs_path.h"
#include "svc_fs_ramfs.h"
#include "svc_fs_vfs.h"

#endif /* SVC_FS_API_H */
