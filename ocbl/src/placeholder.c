/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS NAK - ocbl placeholder source (NAK-01-tree).
 *
 * Exists only so the component build has one real translation unit to
 * compile under the zero-warning gate. The bootloader itself arrives
 * in NAK-02; nothing here implements behaviour.
 */

const char *ocbl_component_name(void)
{
    return "ocbl";
}
