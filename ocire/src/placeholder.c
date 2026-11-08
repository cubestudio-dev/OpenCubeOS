/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS NAK - ocire placeholder source (WP-01-tree).
 *
 * Exists only so the component build has one real translation unit to
 * compile under the zero-warning gate. The recovery kernel itself
 * arrives in NAK-25; nothing here implements behaviour.
 */

const char *ocire_component_name(void)
{
    return "ocire";
}
