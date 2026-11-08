/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS NAK - ocse placeholder source (NAK-01-tree).
 *
 * Exists only so the component build has one real translation unit to
 * compile under the zero-warning gate. The install environment itself
 * arrives in NAK-24; nothing here implements behaviour.
 */

const char *ocse_component_name(void)
{
    return "ocse";
}
