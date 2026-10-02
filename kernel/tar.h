/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10u
 * File: kernel/tar.h
 * Purpose: streaming ustar/tar archive reader (POSIX 1003.1-1988).
 *
 * Used by the WP-10u update installer to walk an (already gunzipped)
 * package: the caller feeds arbitrary-size chunks, and every file body
 * arrives at the callback as one or more data slices (never straddling
 * files).  Header checksums are verified; only regular files and
 * directories are reported (GNU long-name/link entries are skipped).
 *
 * Streaming model:
 *
 *     oc_tar_t *t = oc_tar_open(my_file_cb, ctx);
 *     while (read(chunk)) oc_tar_feed(t, chunk, n);
 *     int rc = oc_tar_finish(t);      (0 = archive walked cleanly)
 *     oc_tar_close(t);
 */
#ifndef OC_TAR_H
#define OC_TAR_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* tar entry types reported to the callback. */
#define OC_TAR_FILE 1
#define OC_TAR_DIR  2

/* Error codes (all negative). */
#define OC_TAR_ERR_ARGS    (-1)
#define OC_TAR_ERR_HEADER  (-2)  /* malformed / short header block     */
#define OC_TAR_ERR_CSUM    (-3)  /* header checksum mismatch           */
#define OC_TAR_ERR_SIZE    (-4)  /* file size field invalid            */
#define OC_TAR_ERR_SINK    (-5)  /* file callback failed               */
#define OC_TAR_ERR_STATE   (-6)  /* API misuse                         */
#define OC_TAR_ERR_TRUNC   (-7)  /* archive ended mid-entry            */

/* File data callback: called with successive slices of one file body.
 * name/path are NUL-terminated ASCII; type is OC_TAR_FILE/OC_TAR_DIR;
 * size is the whole file size; data==NULL for OC_TAR_DIR entries and
 * for the zero-length final slice of an empty file.  slice_len == 0
 * marks the end of the current file.  Return 0 to continue, non-zero
 * to abort with OC_TAR_ERR_SINK. */
typedef int (*oc_tar_file_fn)(void *ctx, const char *path, int type,
                              u64 size, const u8 *data, int slice_len);

typedef struct oc_tar oc_tar_t;

/* Create a reader. Returns NULL on bad arguments / out of memory. */
oc_tar_t *oc_tar_open(oc_tar_file_fn cb, void *ctx);

/* Feed the next chunk of (uncompressed) tar bytes.  Returns 0 when
 * more input is expected, <0 on error. */
int oc_tar_feed(oc_tar_t *t, const u8 *data, int len);

/* Finish: 0 = archive complete (or ended cleanly), <0 on error. */
int oc_tar_finish(oc_tar_t *t);

/* Free the reader. */
void oc_tar_close(oc_tar_t *t);

#ifdef __cplusplus
}
#endif

#endif /* OC_TAR_H */
