/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10-input_chipset
 * File: drivers/acpi/driver_acpi_aml.c
 * Purpose: Minimal AML interpreter (see driver_acpi_aml.h for scope).
 *
 * Layout:
 *   1. object model (values + namespace nodes)
 *   2. namespace helpers (paths, search, creation)
 *   3. expression evaluation (TermArg grammar)
 *   4. statement execution (TermList: named objects + control flow)
 *   5. method invocation + public evaluation API
 *   6. self-test with a compiled-in fixture table
 *
 * Every parse keeps explicit bounds; firmware tables are only half
 * trusted (same defensive-parsing class as arch_multiboot2).
 */
#include "driver_acpi_aml.h"
#include "driver_acpi.h"
#include "lib_string.h"
#include "lib_log.h"
#include "mem_heap.h"
#include "driver_pci.h"

#include <stdint.h>

/* ---- port I/O (self-contained) ---- */
static inline void aml_outb(u16 p, u8 v)  { __asm__ volatile("outb %0, %1" :: "a"(v), "Nd"(p)); }
static inline u8   aml_inb(u16 p)         { u8 v;  __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(p)); return v; }
static inline void aml_outw(u16 p, u16 v) { __asm__ volatile("outw %0, %1" :: "a"(v), "Nd"(p)); }
static inline u16  aml_inw(u16 p)         { u16 v; __asm__ volatile("inw %1, %0" : "=a"(v) : "Nd"(p)); return v; }
static inline void aml_outl(u16 p, u32 v) { __asm__ volatile("outl %0, %1" :: "a"(v), "Nd"(p)); }
static inline u32  aml_inl(u16 p)         { u32 v; __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"(p)); return v; }

/* ------------------------------------------------------------------ */
/* object model                                                        */
/* ------------------------------------------------------------------ */

#define AO_INTEGER     0x01
#define AO_STRING      0x02
#define AO_BUFFER      0x03
#define AO_PACKAGE     0x04
#define AO_METHOD      0x05
#define AO_OPREGION    0x06
#define AO_FIELD       0x07
#define AO_INDEXFIELD  0x08
#define AO_DEVICE      0x09
#define AO_SCOPE       0x0A
#define AO_PROCESSOR   0x0B
#define AO_THERMALZONE 0x0C
#define AO_POWERRES    0x0D
#define AO_MUTEX       0x0E
#define AO_EVENT       0x0F
#define AO_ALIAS       0x10

typedef struct acpi_value {
    u8  kind;
    u64 integer;
    u8  *buf;  u32 buf_len;          /* buffer (heap) */
    char *str;                        /* string (heap, NUL-terminated) */
    struct acpi_value *elems;        /* package (heap array) */
    u32 elem_count;
    struct acpi_node *node;          /* namespace reference */
} acpi_value_t;

#define AML_MAX_NODES 512
#define AML_NODE_POOL_BYTES (sizeof(acpi_node_t) * AML_MAX_NODES)
typedef struct acpi_node acpi_node_t;
struct acpi_node {
    char name[5];
    u8   kind;
    u8   method_arg_count;
    u8   used;
    acpi_node_t *parent, *first_child, *next_sibling;
    u64  integer;
    u8  *data;   u32 data_len;      /* buffer / string bytes */
    acpi_value_t *elems; u32 elem_count;   /* package */
    const u8 *aml; u32 aml_len;     /* method bytecode */
    /* opregion */
    u8  space; u64 region_base; u32 region_len;
    /* field unit */
    acpi_node_t *region;             /* opregion (NULL => buffer-backed) */
    u8  *buf_back; u32 buf_back_len; /* buffer-backed storage */
    u32 bit_offset; u32 bit_len;
    acpi_node_t *index_reg, *data_reg;
};

/* Node pool lives on the kernel heap (allocated lazily on the first
 * namespace build) instead of .bss - the kernel identity window ends
 * at 0x400000 and a static pool of this size would push the image
 * over that boundary (linker ASSERT). */
static acpi_node_t *g_nodes;
static u32 g_node_count;
static int g_loaded;
static u32 g_method_count;
static u32 g_undef_warn;

#define AML_OP_BUDGET 524288u
#define AML_MAX_DEPTH 8

typedef struct exec_ctx {
    acpi_value_t  args[8];
    u32           arg_count;
    acpi_value_t  locals[8];
    acpi_node_t  *scope;
    const u8     *start, *pc, *end;
    u32           op_count;
    int           has_return;
    acpi_value_t  ret;
    int           break_flag, continue_flag;
    int           depth;
    int           load_mode;
} exec_ctx_t;

static int   aml_exec_termlist(exec_ctx_t *ctx);
static int   aml_exec_statement(exec_ctx_t *ctx);
static int   aml_eval_termarg(exec_ctx_t *ctx, acpi_value_t *out);
static void  aml_val_clear(acpi_value_t *v);
static int   aml_val_copy(acpi_value_t *dst, const acpi_value_t *src);
static acpi_node_t *aml_lookup(acpi_node_t *scope, const char *name);
static acpi_node_t *aml_child_lookup(acpi_node_t *parent, const char *name);
static acpi_node_t *aml_create_node(acpi_node_t *parent, const char *name, u8 kind);
static void  aml_store_value(acpi_node_t *node, const acpi_value_t *v);
static int   aml_field_read(acpi_node_t *field, u64 *out, exec_ctx_t *ctx);
static int   aml_field_write(acpi_node_t *field, u64 value, exec_ctx_t *ctx);
static int   aml_invoke_method(acpi_node_t *m, acpi_value_t *args, u32 argc,
                               acpi_value_t *ret_out, int depth);
static void  aml_node_value(const acpi_node_t *n, acpi_value_t *out);
static acpi_node_t *aml_parse_and_resolve(exec_ctx_t *ctx, int create);
static void  aml_store_to_target(exec_ctx_t *ctx, u64 val);

/* ------------------------------------------------------------------ */
/* logging                                                             */
/* ------------------------------------------------------------------ */

static void aml_warn_op(u32 op, u32 off) {
    char line[96]; char n[16];
    strcpy(line, "AML: unsupported op 0x");
    u64_to_hex(op, n, 4); strcat(line, n);
    strcat(line, " @0x");
    u64_to_hex(off, n, 4); strcat(line, n);
    OC_LOG_FAIL(line);
}

static void aml_note(const char *msg) {
    char line[80];
    strcpy(line, "AML: ");
    strcat(line, msg);
    lib_log_info(line);
}

/* ------------------------------------------------------------------ */
/* 1. values                                                           */
/* ------------------------------------------------------------------ */

static void aml_val_clear(acpi_value_t *v) {
    if (!v) return;
    if (v->kind == AO_BUFFER && v->buf) kfree(v->buf);
    if (v->kind == AO_STRING && v->str) kfree(v->str);
    if (v->kind == AO_PACKAGE && v->elems) {
        for (u32 i = 0; i < v->elem_count; i++) aml_val_clear(&v->elems[i]);
        kfree(v->elems);
    }
    memset(v, 0, sizeof(*v));
    v->kind = AO_INTEGER;
}

static int aml_val_copy(acpi_value_t *dst, const acpi_value_t *src) {
    aml_val_clear(dst);
    dst->kind = src->kind;
    dst->integer = src->integer;
    dst->node = src->node;
    if (src->kind == AO_BUFFER && src->buf_len) {
        dst->buf = (u8*)kmalloc(src->buf_len);
        if (!dst->buf) return -1;
        memcpy(dst->buf, src->buf, src->buf_len);
        dst->buf_len = src->buf_len;
    }
    if (src->kind == AO_STRING && src->str) {
        u32 n = (u32)strlen(src->str) + 1;
        dst->str = (char*)kmalloc(n);
        if (!dst->str) return -1;
        memcpy(dst->str, src->str, n);
    }
    if (src->kind == AO_PACKAGE && src->elem_count) {
        dst->elems = (acpi_value_t*)kmalloc(sizeof(acpi_value_t) * src->elem_count);
        if (!dst->elems) return -1;
        memset(dst->elems, 0, sizeof(acpi_value_t) * src->elem_count);
        dst->elem_count = src->elem_count;
        for (u32 i = 0; i < src->elem_count; i++) {
            if (aml_val_copy(&dst->elems[i], &src->elems[i]) != 0) return -1;
        }
    }
    return 0;
}

/* Store into a namespace node (copy semantics for dynamic payloads). */
static void aml_store_value(acpi_node_t *node, const acpi_value_t *v) {
    if (!node || !v) return;
    switch (node->kind) {
    case AO_FIELD:
    case AO_INDEXFIELD: {
        (void)aml_field_write(node, v->integer, NULL);
        return;
    }
    case AO_METHOD:
        return;   /* methods are not storage */
    default:
        break;
    }
    /* take ownership of a copy */
    acpi_value_t copy;
    if (aml_val_copy(&copy, v) != 0) return;
    kfree(node->data);
    node->data = NULL;
    node->data_len = 0;
    if (node->elems) {
        for (u32 i = 0; i < node->elem_count; i++) aml_val_clear(&node->elems[i]);
        kfree(node->elems);
        node->elems = NULL;
    }
    node->elem_count = 0;
    node->kind    = copy.kind;
    node->integer = copy.integer;
    if (copy.kind == AO_BUFFER) {
        node->data = copy.buf;
        node->data_len = copy.buf_len;
    } else if (copy.kind == AO_STRING) {
        node->data = (u8*)copy.str;
        node->data_len = copy.str ? (u32)strlen(copy.str) + 1 : 0;
    } else if (copy.kind == AO_PACKAGE) {
        node->elems = copy.elems;
        node->elem_count = copy.elem_count;
    }
}

/* snapshot a node value into a fresh heap-owned value */
static void aml_node_value(const acpi_node_t *n, acpi_value_t *out) {
    memset(out, 0, sizeof(*out));
    if (!n) { out->kind = AO_INTEGER; return; }
    out->node = (acpi_node_t*)n;
    switch (n->kind) {
    case AO_INTEGER:
        out->kind = AO_INTEGER;
        out->integer = n->integer;
        break;
    case AO_STRING: {
        out->kind = AO_STRING;
        u32 len = (n->data && n->data_len) ? n->data_len : 1;
        out->str = (char*)kmalloc(len);
        if (out->str) {
            if (n->data) memcpy(out->str, n->data, len);
            out->str[len - 1] = 0;
        }
        break;
    }
    case AO_BUFFER: {
        out->kind = AO_BUFFER;
        out->buf = (u8*)kmalloc(n->data_len ? n->data_len : 1);
        if (out->buf) {
            if (n->data && n->data_len) memcpy(out->buf, n->data, n->data_len);
            out->buf_len = n->data_len;
        }
        break;
    }
    case AO_PACKAGE: {
        out->kind = AO_PACKAGE;
        u32 cnt = n->elem_count ? n->elem_count : 1;
        out->elems = (acpi_value_t*)kmalloc(sizeof(acpi_value_t) * cnt);
        if (out->elems) {
            memset(out->elems, 0, sizeof(acpi_value_t) * cnt);
            out->elem_count = n->elem_count;
            for (u32 i = 0; i < n->elem_count; i++)
                (void)aml_val_copy(&out->elems[i], &n->elems[i]);
        }
        break;
    }
    default:
        out->kind = AO_INTEGER;
        break;
    }
}

/* ------------------------------------------------------------------ */
/* 2. namespace                                                        */
/* ------------------------------------------------------------------ */

static acpi_node_t *aml_alloc_node(void) {
    if (!g_nodes) return NULL;
    if (g_node_count >= AML_MAX_NODES) return NULL;
    acpi_node_t *n = &g_nodes[g_node_count++];
    memset(n, 0, sizeof(*n));
    n->used = 1;
    return n;
}

static acpi_node_t *aml_root(void) { return &g_nodes[0]; }

static void aml_ns_reset(void) {
    if (!g_nodes) {
        g_nodes = (acpi_node_t*)kmalloc(AML_NODE_POOL_BYTES);
        if (!g_nodes) {
            aml_note("node pool allocation failed");
            return;
        }
    }
    memset(g_nodes, 0, AML_NODE_POOL_BYTES);
    g_node_count = 0;
    g_method_count = 0;
    g_undef_warn = 0;
    acpi_node_t *root = aml_alloc_node();
    if (root) {
        root->name[0] = '\\';
        root->kind = AO_SCOPE;
        (void)aml_create_node(root, "_GPE", AO_SCOPE);
        (void)aml_create_node(root, "_SB_", AO_SCOPE);
        (void)aml_create_node(root, "_SI_", AO_SCOPE);
        (void)aml_create_node(root, "_TZ_", AO_SCOPE);
    }
}

static acpi_node_t *aml_lookup(acpi_node_t *scope, const char *name) {
    for (acpi_node_t *s = scope; s; s = s->parent) {
        for (acpi_node_t *c = s->first_child; c; c = c->next_sibling) {
            if (strcmp(c->name, name) == 0) return c;
        }
    }
    return NULL;
}

static acpi_node_t *aml_create_node(acpi_node_t *parent, const char *name, u8 kind) {
    acpi_node_t *n = aml_alloc_node();
    if (!n) return NULL;
    for (int i = 0; i < 4 && name[i]; i++) n->name[i] = name[i];
    n->kind = kind;
    n->parent = parent;
    if (parent) {
        if (!parent->first_child) parent->first_child = n;
        else {
            acpi_node_t *c = parent->first_child;
            while (c->next_sibling) c = c->next_sibling;
            c->next_sibling = n;
        }
    }
    if (kind == AO_METHOD) g_method_count++;
    return n;
}

/* Parse a name path and resolve (or create) the final segment.
 * Handles '\', '^', DualNamePrefix (0x2E), MultiNamePrefix (0x2F) and
 * the bare-root forms Scope(\) / Method(\_): RootPrefix followed by
 * NullName (0x00) or by the next statement - such a path denotes the
 * scope itself. */
static acpi_node_t *aml_parse_and_resolve(exec_ctx_t *ctx, int create) {
    if (ctx->pc >= ctx->end) return NULL;
    int rooted = 0, parents = 0;
    if (*ctx->pc == '\\') { rooted = 1; ctx->pc++; }
    while (ctx->pc < ctx->end && *ctx->pc == '^') { parents++; ctx->pc++; }

    acpi_node_t *scope = ctx->scope;
    for (int i = 0; i < parents && scope && scope->parent; i++) scope = scope->parent;
    if (rooted) scope = aml_root();
    if (!scope) scope = aml_root();

    /* bare root: "\" + NullName (0x00) denotes the scope itself */
    if (ctx->pc < ctx->end && *ctx->pc == 0x00) {
        ctx->pc++;
        return scope;
    }

    u8 segs = 1;
    if (ctx->pc < ctx->end && (*ctx->pc == 0x2E || *ctx->pc == 0x2F)) {
        u8 form = *ctx->pc;
        ctx->pc++;
        if (form == 0x2E) segs = 2;
        else { if (ctx->pc >= ctx->end) return NULL; segs = *ctx->pc++; }
        if (segs < 1) return NULL;
    }

    char seg[5];
    acpi_node_t *n = NULL;
    for (u8 i = 0; i < segs; i++) {
        if (ctx->pc + 4 > ctx->end) return NULL;
        for (int k = 0; k < 4; k++) seg[k] = (char)ctx->pc[k];
        seg[4] = 0;
        ctx->pc += 4;
        int last = (i == segs - 1);
        /* DECLARE (create): the segment must live in the current scope
         * only - a Name(_HID) inside Device(KBD) defines KBD._HID, it
         * must NEVER rebind an ancestor's object (AML declaration
         * semantics = ACPICA's NO_PEER_SEARCH). References (create=0)
         * keep the standard parent-chain search. */
        n = create ? aml_child_lookup(scope, seg) : aml_lookup(scope, seg);
        if (!n) {
            if (!create) {
                if (g_undef_warn < 12) {
                    char line[64];
                    strcpy(line, "AML: undefined name ");
                    strcat(line, seg);
                    aml_note(line);
                    g_undef_warn++;
                }
                return NULL;
            }
            n = aml_create_node(scope, seg, last ? AO_INTEGER : AO_SCOPE);
            if (!n) return NULL;
        }
        if (!last) scope = n;
    }
    return n;
}

/* ------------------------------------------------------------------ */
/* 3. field access                                                     */
/* ------------------------------------------------------------------ */

static int aml_field_read(acpi_node_t *field, u64 *out, exec_ctx_t *ctx) {
    if (!field) return -1;
    if (!field->region) {
        if (field->buf_back) {
            u32 byte_off = field->bit_offset >> 3;
            int width = (int)((field->bit_len + 7) >> 3);
            if (byte_off + (u32)width > field->buf_back_len) return -1;
            u64 raw = 0;
            for (int i = 0; i < width; i++)
                raw |= (u64)field->buf_back[byte_off + i] << (8 * i);
            u32 sh = field->bit_offset & 7;
            u64 mask = (field->bit_len >= 64) ? ~0ull : ((1ull << field->bit_len) - 1);
            *out = (raw >> sh) & mask;
            return 0;
        }
        if (field->index_reg && field->data_reg) {
            exec_ctx_t tmp;
            memset(&tmp, 0, sizeof(tmp));
            tmp.scope = field->parent ? field->parent : aml_root();
            (void)aml_field_write(field->index_reg, field->bit_offset >> 3, &tmp);
            return aml_field_read(field->data_reg, out, &tmp);
        }
        (void)ctx;
        return -1;
    }
    acpi_node_t *r = field->region;
    u32 byte_off = field->bit_offset >> 3;
    int width = (int)((field->bit_len + 7) >> 3);
    if (width > 8) return -1;
    u64 raw = 0;
    switch (r->space) {
    case 0x00: {
        volatile const u8 *p = (volatile const u8*)(uintptr_t)(r->region_base + byte_off);
        for (int i = 0; i < width; i++) raw |= (u64)p[i] << (8 * i);
        break;
    }
    case 0x01: {
        u16 port = (u16)(r->region_base + byte_off);
        switch (width) {
        case 1: raw = aml_inb(port); break;
        case 2: raw = aml_inw(port); break;
        default: raw = aml_inl(port); break;
        }
        break;
    }
    case 0x02: {
        u32 base = (u32)r->region_base;
        u8 bus = (u8)((base >> 16) & 0xFF);
        u8 dev = (u8)((base >> 11) & 0x1F);
        u8 fn  = (u8)((base >> 8)  & 0x07);
        u32 cfg_off = (base & 0xFF) + byte_off;
        u32 d = driver_pci_read_config(bus, dev, fn, (u8)(cfg_off & ~3u));
        u32 sh = (cfg_off & 3) * 8;
        if (width >= 3) raw = d;
        else if (width == 2) raw = (d >> sh) & 0xFFFF;
        else raw = (d >> sh) & 0xFF;
        break;
    }
    default:
        return -1;
    }
    u32 sh = field->bit_offset & 7;
    u64 mask = (field->bit_len >= 64) ? ~0ull : ((1ull << field->bit_len) - 1);
    *out = (raw >> sh) & mask;
    return 0;
}

static int aml_field_write(acpi_node_t *field, u64 value, exec_ctx_t *ctx) {
    (void)ctx;
    if (!field) return -1;
    if (!field->region) {
        if (field->buf_back) {
            u32 byte_off = field->bit_offset >> 3;
            int width = (int)((field->bit_len + 7) >> 3);
            if (byte_off + (u32)width > field->buf_back_len) return -1;
            u32 sh = field->bit_offset & 7;
            u64 mask = (field->bit_len >= 64) ? ~0ull : ((1ull << field->bit_len) - 1);
            u64 raw = 0;
            for (int i = 0; i < width; i++)
                raw |= (u64)field->buf_back[byte_off + i] << (8 * i);
            raw = (raw & ~(mask << sh)) | ((value & mask) << sh);
            for (int i = 0; i < width; i++)
                field->buf_back[byte_off + i] = (u8)(raw >> (8 * i));
            return 0;
        }
        if (field->index_reg && field->data_reg) {
            exec_ctx_t tmp;
            memset(&tmp, 0, sizeof(tmp));
            tmp.scope = field->parent ? field->parent : aml_root();
            (void)aml_field_write(field->index_reg, field->bit_offset >> 3, &tmp);
            return aml_field_write(field->data_reg, value, &tmp);
        }
        return -1;
    }
    acpi_node_t *r = field->region;
    u32 byte_off = field->bit_offset >> 3;
    int width = (int)((field->bit_len + 7) >> 3);
    if (width > 8) return -1;
    u32 sh = field->bit_offset & 7;
    u64 mask = (field->bit_len >= 64) ? ~0ull : ((1ull << field->bit_len) - 1);
    u64 all = mask << sh;
    u64 data = (value & mask) << sh;
    switch (r->space) {
    case 0x00: {
        volatile u8 *p = (volatile u8*)(uintptr_t)(r->region_base + byte_off);
        u64 raw = 0;
        for (int i = 0; i < width; i++) raw |= (u64)p[i] << (8 * i);
        raw = (raw & ~all) | data;
        for (int i = 0; i < width; i++) p[i] = (u8)(raw >> (8 * i));
        return 0;
    }
    case 0x01: {
        u16 port = (u16)(r->region_base + byte_off);
        switch (width) {
        case 1: { u8 cur = aml_inb(port); aml_outb(port, (u8)((cur & ~all) | data)); break; }
        case 2: { u16 cur = aml_inw(port); aml_outw(port, (u16)((cur & ~all) | data)); break; }
        default: { u32 cur = aml_inl(port); aml_outl(port, (u32)((cur & ~all) | data)); break; }
        }
        return 0;
    }
    case 0x02: {
        u32 base = (u32)r->region_base;
        u8 bus = (u8)((base >> 16) & 0xFF);
        u8 dev = (u8)((base >> 11) & 0x1F);
        u8 fn  = (u8)((base >> 8)  & 0x07);
        u32 cfg_off = (base & 0xFF) + byte_off;
        u8 aligned = (u8)(cfg_off & ~3u);
        u32 cur = driver_pci_read_config(bus, dev, fn, aligned);
        u32 sh32 = (cfg_off & 3) * 8;
        u32 merged;
        if (width >= 3)      merged = (u32)(((cur & ~(u32)all) | (u32)data));
        else if (width == 2) merged = (cur & ~(0xFFFFu << sh32)) | ((u32)data << sh32);
        else                 merged = (cur & ~(0xFFu << sh32))   | ((u32)data << sh32);
        driver_pci_write_config(bus, dev, fn, aligned, merged);
        return 0;
    }
    default:
        return -1;
    }
}

/* ------------------------------------------------------------------ */
/* 4. expression evaluation                                            */
/* ------------------------------------------------------------------ */

static u64 aml_read_le(exec_ctx_t *ctx, int bytes) {
    u64 v = 0;
    for (int i = 0; i < bytes && ctx->pc < ctx->end; i++)
        v |= (u64)(*ctx->pc++) << (8 * i);
    return v;
}

/* Parse a PkgLength starting at ctx->pc. Returns the total length
 * (length bytes + body). Leaves pc past the length encoding.
 *
 * Encoding per ACPICA AcpiPsGetNextPackageLength (ACPI spec 20.2.4):
 * lead byte bits [7:6] = follower count n; when n == 0 the length is
 * lead bits [5:0]; when n > 0 the lead byte contributes only bits
 * [3:0] and follower byte i contributes bits [(i*8)-4 +: 8]
 * (byte1<<4, byte2<<12, byte3<<20). The value INCLUDES the length
 * bytes themselves. */
static u32 aml_pkg_length(exec_ctx_t *ctx) {
    if (ctx->pc >= ctx->end) return 0;
    u8 b0 = *ctx->pc++;
    u8 n = b0 >> 6;
    u32 len;
    if (n == 0) {
        len = b0 & 0x3F;
    } else {
        len = 0;
        for (u8 i = n; i >= 1; i--) {
            if (ctx->pc + (i - 1) < ctx->end)
                len |= (u32)ctx->pc[i - 1] << ((i << 3) - 4);
        }
        ctx->pc += n;
        len |= b0 & 0x0F;
    }
    return len;
}

/* Open a package body: body_end = position_of_length_byte + total. */
static int aml_open_pkg(exec_ctx_t *ctx, const u8 **body_end) {
    const u8 *p0 = ctx->pc;
    u32 len = aml_pkg_length(ctx);
    if (len == 0 || p0 + len > ctx->end) return -1;
    *body_end = p0 + len;
    return 0;
}

/* Store an integer into a Target (LocalX/ArgX/NullName/Debug/name). */
static void aml_store_to_target(exec_ctx_t *ctx, u64 val) {
    if (ctx->pc >= ctx->end) return;
    u8 t = *ctx->pc;
    if (t == 0x00) { ctx->pc++; return; }             /* NullName */
    if (t == 0x5B && ctx->pc + 1 < ctx->end && ctx->pc[1] == 0x31) {
        ctx->pc += 2;                                  /* DebugObj */
        return;
    }
    if (t >= 0x60 && t <= 0x67) {
        ctx->pc++;
        int i = t - 0x60;
        aml_val_clear(&ctx->locals[i]);
        ctx->locals[i].kind = AO_INTEGER;
        ctx->locals[i].integer = val;
        return;
    }
    if (t >= 0x68 && t <= 0x6E) {
        ctx->pc++;
        int i = t - 0x68;
        if (i < (int)ctx->arg_count) {
            aml_val_clear(&ctx->args[i]);
            ctx->args[i].kind = AO_INTEGER;
            ctx->args[i].integer = val;
        }
        return;
    }
    acpi_node_t *n = aml_parse_and_resolve(ctx, 0);
    if (n) {
        acpi_value_t v;
        v.kind = AO_INTEGER;
        v.integer = val;
        aml_store_value(n, &v);
    }
}

/* Skip a Target (used where we ignore the store target). */
static void aml_skip_target(exec_ctx_t *ctx) {
    if (ctx->pc >= ctx->end) return;
    u8 t = *ctx->pc;
    if (t == 0x00) { ctx->pc++; return; }
    if (t == 0x5B && ctx->pc + 1 < ctx->end && ctx->pc[1] == 0x31) { ctx->pc += 2; return; }
    if ((t >= 0x60 && t <= 0x6E) || t == '\\' || t == '^' ||
        t == 0x2E || t == 0x2F || (t >= 'A' && t <= 'Z')) {
        (void)aml_parse_and_resolve(ctx, 0);
    }
}

static int aml_invoke_method(acpi_node_t *m, acpi_value_t *args, u32 argc,
                             acpi_value_t *ret_out, int depth) {
    if (!m || m->kind != AO_METHOD || !m->aml) return -1;
    if (depth >= AML_MAX_DEPTH) {
        aml_note("method recursion limit");
        return -1;
    }
    exec_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.depth = depth + 1;
    ctx.arg_count = argc;
    for (u32 i = 0; i < argc && i < 7; i++)
        (void)aml_val_copy(&ctx.args[i], &args[i]);
    ctx.scope = m->parent ? m->parent : aml_root();
    ctx.start = m->aml;
    ctx.pc    = m->aml;
    ctx.end   = m->aml + m->aml_len;
    ctx.load_mode = 0;

    aml_exec_termlist(&ctx);

    for (u32 i = 0; i < 8; i++) aml_val_clear(&ctx.locals[i]);
    for (u32 i = 0; i < 8; i++) aml_val_clear(&ctx.args[i]);

    if (ret_out) {
        if (ctx.has_return) (void)aml_val_copy(ret_out, &ctx.ret);
        else { ret_out->kind = AO_INTEGER; ret_out->integer = 0; }
    }
    aml_val_clear(&ctx.ret);
    return 0;
}

static int aml_eval_termarg(exec_ctx_t *ctx, acpi_value_t *out) {
    if (ctx->op_count++ > AML_OP_BUDGET) { aml_note("op budget exceeded"); return -1; }
    if (ctx->pc >= ctx->end) return -1;
    u8 op = *ctx->pc++;
    memset(out, 0, sizeof(*out));
    out->kind = AO_INTEGER;

    switch (op) {
    case 0x00: out->integer = 0; return 0;
    case 0x01: out->integer = 1; return 0;
    case 0xFF: out->integer = ~0ull; return 0;
    case 0x0A: out->integer = aml_read_le(ctx, 1); return 0;
    case 0x0B: out->integer = aml_read_le(ctx, 2); return 0;
    case 0x0C: out->integer = aml_read_le(ctx, 4); return 0;
    case 0x0E: out->integer = aml_read_le(ctx, 8); return 0;
    case 0x0D: {
        const u8 *s = ctx->pc;
        while (ctx->pc < ctx->end && *ctx->pc != 0) ctx->pc++;
        if (ctx->pc >= ctx->end) return -1;
        ctx->pc++;
        u32 n = (u32)(ctx->pc - s);
        out->kind = AO_STRING;
        out->str = (char*)kmalloc(n + 1);
        if (!out->str) return -1;
        memcpy(out->str, s, n);
        out->str[n] = 0;
        return 0;
    }
    case 0x60: case 0x61: case 0x62: case 0x63:
    case 0x64: case 0x65: case 0x66: case 0x67:
        return aml_val_copy(out, &ctx->locals[op - 0x60]);
    case 0x68: case 0x69: case 0x6A: case 0x6B:
    case 0x6C: case 0x6D: case 0x6E: case 0x6F: {
        int i = op - 0x68;
        if (i >= (int)ctx->arg_count) { out->integer = 0; return 0; }
        return aml_val_copy(out, &ctx->args[i]);
    }
    default:
        break;
    }

    if (op == 0x5B) {
        if (ctx->pc >= ctx->end) return -1;
        u8 op2 = *ctx->pc++;
        switch (op2) {
        case 0x30: out->integer = 2; return 0;             /* RevisionOp */
        case 0x31: out->integer = 0; return 0;             /* TimerOp (no usec tick) */
        case 0x12: {                                       /* CondRefOf (Src, Tgt) */
            acpi_node_t *n = aml_parse_and_resolve(ctx, 0);
            aml_store_to_target(ctx, n ? 1 : 0);
            out->integer = n ? 1 : 0;
            return 0;
        }
        case 0x13: {                                       /* CreateField (Src, Idx, Len, Name) */
            acpi_value_t src, idx, len;
            if (aml_eval_termarg(ctx, &src) != 0) return -1;
            if (aml_eval_termarg(ctx, &idx) != 0) { aml_val_clear(&src); return -1; }
            if (aml_eval_termarg(ctx, &len) != 0) { aml_val_clear(&src); aml_val_clear(&idx); return -1; }
            acpi_node_t *n = aml_parse_and_resolve(ctx, 1);
            if (n) {
                n->kind = AO_FIELD;
                n->bit_offset = (u32)idx.integer;
                n->bit_len    = (u32)len.integer;
                if (src.node && src.node->kind == AO_BUFFER) {
                    n->buf_back     = src.node->data;
                    n->buf_back_len = src.node->data_len;
                } else if (src.kind == AO_BUFFER) {
                    n->buf_back     = src.buf;
                    n->buf_back_len = src.buf_len;
                }
            }
            aml_val_clear(&src); aml_val_clear(&idx); aml_val_clear(&len);
            return 0;
        }
        case 0x23: {                                       /* Acquire (Mutex, Timeout) */
            (void)aml_parse_and_resolve(ctx, 0);
            if (ctx->pc + 2 <= ctx->end) ctx->pc += 2;
            out->integer = 0;
            return 0;
        }
        case 0x25: case 0x27: {                            /* Wait / Release */
            (void)aml_parse_and_resolve(ctx, 0);
            if (op2 == 0x25) {
                acpi_value_t t;
                if (aml_eval_termarg(ctx, &t) != 0) return -1;
                aml_val_clear(&t);
            }
            out->integer = 0;
            return 0;
        }
        default:
            ctx->pc -= 2;
            aml_warn_op(0x5B00 | op2, (u32)(ctx->pc - ctx->start));
            ctx->pc = ctx->end;
            return -1;
        }
    }

    if ((op >= 'A' && op <= 'Z') || op == '\\' || op == '^' ||
        op == 0x2E || op == 0x2F) {
        ctx->pc--;
        acpi_node_t *n = aml_parse_and_resolve(ctx, 0);
        if (!n) return -1;
        switch (n->kind) {
        case AO_METHOD: {
            acpi_value_t args[8];
            memset(args, 0, sizeof(args));
            u32 argc = 0;
            for (u32 i = 0; i < n->method_arg_count; i++) {
                if (aml_eval_termarg(ctx, &args[argc]) != 0) return -1;
                argc++;
            }
            int rc = aml_invoke_method(n, args, argc, out, ctx->depth);
            for (u32 i = 0; i < argc; i++) aml_val_clear(&args[i]);
            return rc;
        }
        case AO_FIELD:
        case AO_INDEXFIELD: {
            u64 v = 0;
            if (aml_field_read(n, &v, ctx) != 0) return -1;
            out->integer = v;
            return 0;
        }
        default:
            aml_node_value(n, out);
            return 0;
        }
    }

    switch (op) {
    case 0x70: { /* Store (Src, Dst) */
        acpi_value_t src;
        if (aml_eval_termarg(ctx, &src) != 0) return -1;
        if (ctx->pc >= ctx->end) { aml_val_clear(&src); return -1; }
        u8 t = *ctx->pc;
        if (t >= 0x60 && t <= 0x67) {
            ctx->pc++;
            aml_val_copy(&ctx->locals[t - 0x60], &src);
            (void)aml_val_copy(out, &src);
            aml_val_clear(&src);
            return 0;
        }
        if (t >= 0x68 && t <= 0x6E) {
            ctx->pc++;
            int i = t - 0x68;
            if (i < (int)ctx->arg_count) aml_val_copy(&ctx->args[i], &src);
            (void)aml_val_copy(out, &src);
            aml_val_clear(&src);
            return 0;
        }
        acpi_node_t *tn = aml_parse_and_resolve(ctx, 0);
        if (tn) aml_store_value(tn, &src);
        (void)aml_val_copy(out, &src);
        aml_val_clear(&src);
        return 0;
    }
    case 0x71: { /* RefOf */
        acpi_node_t *n = aml_parse_and_resolve(ctx, 0);
        out->node = n;
        return 0;
    }
    case 0x72: case 0x74: { /* Add / Subtract */
        acpi_value_t a, b;
        if (aml_eval_termarg(ctx, &a) != 0) return -1;
        if (aml_eval_termarg(ctx, &b) != 0) { aml_val_clear(&a); return -1; }
        u64 r = (op == 0x72) ? a.integer + b.integer : a.integer - b.integer;
        aml_val_clear(&a); aml_val_clear(&b);
        aml_store_to_target(ctx, r);
        out->integer = r;
        return 0;
    }
    case 0x75: case 0x76: { /* Increment / Decrement */
        acpi_node_t *n = aml_parse_and_resolve(ctx, 0);
        if (!n) return -1;
        if (n->kind == AO_FIELD || n->kind == AO_INDEXFIELD) {
            u64 v = 0;
            if (aml_field_read(n, &v, ctx) != 0) return -1;
            v = (op == 0x75) ? v + 1 : v - 1;
            (void)aml_field_write(n, v, ctx);
            out->integer = v;
            return 0;
        }
        n->integer = (op == 0x75) ? n->integer + 1 : n->integer - 1;
        out->integer = n->integer;
        return 0;
    }
    case 0x77: { /* Multiply */
        acpi_value_t a, b;
        if (aml_eval_termarg(ctx, &a) != 0) return -1;
        if (aml_eval_termarg(ctx, &b) != 0) { aml_val_clear(&a); return -1; }
        u64 r = a.integer * b.integer;
        aml_val_clear(&a); aml_val_clear(&b);
        aml_store_to_target(ctx, r);
        out->integer = r;
        return 0;
    }
    case 0x78: { /* Divide (Divd, Divs, RemTgt, QuotTgt) */
        acpi_value_t a, b;
        if (aml_eval_termarg(ctx, &a) != 0) return -1;
        if (aml_eval_termarg(ctx, &b) != 0) { aml_val_clear(&a); return -1; }
        u64 quot = b.integer ? (a.integer / b.integer) : 0;
        u64 rem  = b.integer ? (a.integer % b.integer) : 0;
        aml_val_clear(&a); aml_val_clear(&b);
        aml_store_to_target(ctx, rem);
        aml_store_to_target(ctx, quot);
        out->integer = quot;
        return 0;
    }
    case 0x79: case 0x7A: { /* ShiftLeft / ShiftRight */
        acpi_value_t a, b;
        if (aml_eval_termarg(ctx, &a) != 0) return -1;
        if (aml_eval_termarg(ctx, &b) != 0) { aml_val_clear(&a); return -1; }
        u64 sh = b.integer & 63;
        u64 r = (op == 0x79) ? (a.integer << sh) : (a.integer >> sh);
        aml_val_clear(&a); aml_val_clear(&b);
        aml_store_to_target(ctx, r);
        out->integer = r;
        return 0;
    }
    case 0x7B: case 0x7C: case 0x7D: case 0x7E: case 0x7F: {
        acpi_value_t a, b;
        if (aml_eval_termarg(ctx, &a) != 0) return -1;
        if (aml_eval_termarg(ctx, &b) != 0) { aml_val_clear(&a); return -1; }
        u64 r = 0;
        switch (op) {
        case 0x7B: r = a.integer & b.integer; break;
        case 0x7C: r = ~(a.integer & b.integer); break;
        case 0x7D: r = a.integer | b.integer; break;
        case 0x7E: r = ~(a.integer | b.integer); break;
        case 0x7F: r = a.integer ^ b.integer; break;
        }
        aml_val_clear(&a); aml_val_clear(&b);
        aml_store_to_target(ctx, r);
        out->integer = r;
        return 0;
    }
    case 0x80: { /* Not */
        acpi_value_t a;
        if (aml_eval_termarg(ctx, &a) != 0) return -1;
        u64 r = ~a.integer;
        aml_val_clear(&a);
        aml_store_to_target(ctx, r);
        out->integer = r;
        return 0;
    }
    case 0x85: { /* Mod */
        acpi_value_t a, b;
        if (aml_eval_termarg(ctx, &a) != 0) return -1;
        if (aml_eval_termarg(ctx, &b) != 0) { aml_val_clear(&a); return -1; }
        u64 r = b.integer ? (a.integer % b.integer) : 0;
        aml_val_clear(&a); aml_val_clear(&b);
        aml_store_to_target(ctx, r);
        out->integer = r;
        return 0;
    }
    case 0x81: case 0x82: { /* FindSetLeftBit / FindSetRightBit */
        acpi_value_t a;
        if (aml_eval_termarg(ctx, &a) != 0) return -1;
        u64 r = 0;
        if (op == 0x81) {
            for (int i = 63; i >= 0; i--) if ((a.integer >> i) & 1) { r = (u64)(i + 1); break; }
        } else {
            for (u32 i = 0; i < 64; i++) if ((a.integer >> i) & 1) { r = i + 1; break; }
        }
        aml_val_clear(&a);
        aml_store_to_target(ctx, r);
        out->integer = r;
        return 0;
    }
    case 0x83: { /* DerefOf */
        acpi_value_t a;
        if (aml_eval_termarg(ctx, &a) != 0) return -1;
        if (a.node) {
            acpi_node_t *n = a.node;
            aml_val_clear(&a);
            switch (n->kind) {
            case AO_METHOD:
                return aml_invoke_method(n, NULL, 0, out, ctx->depth);
            case AO_FIELD:
            case AO_INDEXFIELD: {
                u64 v = 0;
                if (aml_field_read(n, &v, ctx) != 0) return -1;
                out->integer = v;
                return 0;
            }
            default:
                aml_node_value(n, out);
                return 0;
            }
        }
        out->integer = a.integer;
        aml_val_clear(&a);
        return 0;
    }
    case 0x86: { /* Notify - logged no-op */
        acpi_value_t a, b;
        if (aml_eval_termarg(ctx, &a) != 0) return -1;
        if (aml_eval_termarg(ctx, &b) != 0) { aml_val_clear(&a); return -1; }
        aml_val_clear(&a); aml_val_clear(&b);
        return 0;
    }
    case 0x87: { /* SizeOf */
        acpi_node_t *n = aml_parse_and_resolve(ctx, 0);
        if (!n) return -1;
        switch (n->kind) {
        case AO_BUFFER:  out->integer = n->data_len; break;
        case AO_STRING:  out->integer = n->data ? strlen((char*)n->data) : 0; break;
        case AO_PACKAGE: out->integer = n->elem_count; break;
        default:         out->integer = 0; break;
        }
        return 0;
    }
    case 0x88: { /* Index (Buf/Str/Pkg, Idx, Target) */
        acpi_value_t a, b;
        if (aml_eval_termarg(ctx, &a) != 0) return -1;
        if (aml_eval_termarg(ctx, &b) != 0) { aml_val_clear(&a); return -1; }
        u64 idx = b.integer;
        aml_val_clear(&b);
        if (a.kind == AO_PACKAGE && a.elems && idx < a.elem_count) {
            (void)aml_val_copy(out, &a.elems[idx]);
            aml_skip_target(ctx);
            aml_val_clear(&a);
            return 0;
        }
        if (a.kind == AO_BUFFER && a.buf && idx < a.buf_len) {
            out->integer = a.buf[idx];
            aml_skip_target(ctx);
            aml_val_clear(&a);
            return 0;
        }
        if (a.kind == AO_STRING && a.str && idx < strlen(a.str)) {
            out->integer = (u8)a.str[idx];
            aml_skip_target(ctx);
            aml_val_clear(&a);
            return 0;
        }
        out->integer = 0;
        aml_skip_target(ctx);
        aml_val_clear(&a);
        return 0;
    }
    case 0x89: { /* Match (Pkg, Op1, V1, Op2, V2, Start) */
        acpi_value_t pkg, ops[5];
        if (aml_eval_termarg(ctx, &pkg) != 0) return -1;
        for (int i = 0; i < 5; i++) {
            if (aml_eval_termarg(ctx, &ops[i]) != 0) { aml_val_clear(&pkg); return -1; }
        }
        out->integer = ~0ull;
        if (pkg.kind == AO_PACKAGE && pkg.elems) {
            u64 start = ops[4].integer;
            for (u32 i = (u32)(start < pkg.elem_count ? start : 0); i < pkg.elem_count; i++) {
                u64 v = pkg.elems[i].integer;
                u64 o1 = ops[0].integer, v1 = ops[1].integer;
                u64 o2 = ops[2].integer, v2 = ops[3].integer;
                int m1 = (o1 == 0) ? v == v1 : (o1 == 1) ? v < v1 : v > v1;
                int m2 = (o2 == 0) ? v == v2 : (o2 == 1) ? v < v2 : v > v2;
                if (m1 && (o2 >= 5 || m2)) { out->integer = i; break; }
            }
        }
        aml_val_clear(&pkg);
        for (int i = 0; i < 5; i++) aml_val_clear(&ops[i]);
        return 0;
    }
    case 0x90: case 0x91: case 0x93: case 0x94: case 0x95: {
        acpi_value_t a, b;
        if (aml_eval_termarg(ctx, &a) != 0) return -1;
        if (aml_eval_termarg(ctx, &b) != 0) { aml_val_clear(&a); return -1; }
        switch (op) {
        case 0x90: out->integer = (a.integer && b.integer); break;
        case 0x91: out->integer = (a.integer || b.integer); break;
        case 0x93: out->integer = (a.integer == b.integer); break;
        case 0x94: out->integer = (a.integer >  b.integer); break;
        case 0x95: out->integer = (a.integer <  b.integer); break;
        }
        aml_val_clear(&a); aml_val_clear(&b);
        return 0;
    }
    case 0x92: { /* LNot */
        acpi_value_t a;
        if (aml_eval_termarg(ctx, &a) != 0) return -1;
        out->integer = a.integer ? 0 : 1;
        aml_val_clear(&a);
        return 0;
    }
    case 0x96: case 0x97: case 0x98: case 0x99: case 0x9C: {
        acpi_value_t a;
        if (aml_eval_termarg(ctx, &a) != 0) return -1;
        if (op == 0x99) { /* ToInteger */
            out->integer = a.integer;
            if (a.kind == AO_STRING && a.str) {
                u64 v = 0;
                const char *s = a.str;
                if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
                    s += 2;
                    while (*s) {
                        int d = (*s >= '0' && *s <= '9') ? *s - '0'
                              : (*s >= 'a' && *s <= 'f') ? *s - 'a' + 10
                              : (*s >= 'A' && *s <= 'F') ? *s - 'A' + 10 : -1;
                        if (d < 0) break;
                        v = v * 16 + (u64)d; s++;
                    }
                } else {
                    while (*s >= '0' && *s <= '9') { v = v * 10 + (u64)(*s - '0'); s++; }
                }
                out->integer = v;
            }
        } else if (op == 0x96) { /* ToBuffer */
            out->kind = AO_BUFFER;
            out->buf = (u8*)kmalloc(8);
            if (out->buf) {
                out->buf_len = 8;
                for (int i = 0; i < 8; i++) out->buf[i] = (u8)(a.integer >> (8 * i));
            }
        } else {
            char tmp[24];
            if (op == 0x98 || op == 0x9C) u64_to_hex(a.integer, tmp, 1);
            else                          u64_to_str(a.integer, tmp);
            u32 n = (u32)strlen(tmp);
            out->kind = AO_STRING;
            out->str = (char*)kmalloc(n + 1);
            if (out->str) memcpy(out->str, tmp, n + 1);
        }
        aml_val_clear(&a);
        aml_skip_target(ctx);
        return 0;
    }
    case 0x9D: { /* CopyObject */
        acpi_value_t a;
        if (aml_eval_termarg(ctx, &a) != 0) return -1;
        acpi_node_t *tn = aml_parse_and_resolve(ctx, 0);
        if (tn) aml_store_value(tn, &a);
        (void)aml_val_copy(out, &a);
        aml_val_clear(&a);
        return 0;
    }
    case 0x9E: { /* Mid */
        acpi_value_t a, b, c;
        if (aml_eval_termarg(ctx, &a) != 0) return -1;
        if (aml_eval_termarg(ctx, &b) != 0) { aml_val_clear(&a); return -1; }
        if (aml_eval_termarg(ctx, &c) != 0) { aml_val_clear(&a); aml_val_clear(&b); return -1; }
        u64 start = b.integer, len = c.integer;
        aml_val_clear(&b); aml_val_clear(&c);
        if (a.kind == AO_BUFFER && a.buf) {
            if (start > a.buf_len) start = a.buf_len;
            if (start + len > a.buf_len) len = a.buf_len - start;
            out->kind = AO_BUFFER;
            out->buf = (u8*)kmalloc(len ? (usize)len : 1);
            if (out->buf) { memcpy(out->buf, a.buf + start, len); out->buf_len = (u32)len; }
        } else if (a.kind == AO_STRING && a.str) {
            usize sl = strlen(a.str);
            if (start > sl) start = sl;
            if (start + len > sl) len = sl - start;
            out->kind = AO_STRING;
            out->str = (char*)kmalloc(len + 1);
            if (out->str) { memcpy(out->str, a.str + start, len); out->str[len] = 0; }
        }
        aml_val_clear(&a);
        aml_skip_target(ctx);
        return 0;
    }
    case 0x73: { /* Concatenate */
        acpi_value_t a, b;
        if (aml_eval_termarg(ctx, &a) != 0) return -1;
        if (aml_eval_termarg(ctx, &b) != 0) { aml_val_clear(&a); return -1; }
        if (a.kind == AO_STRING || b.kind == AO_STRING) {
            const char *sa = (a.kind == AO_STRING && a.str) ? a.str : "";
            const char *sb = (b.kind == AO_STRING && b.str) ? b.str : "";
            u32 la = (u32)strlen(sa), lb = (u32)strlen(sb);
            out->kind = AO_STRING;
            out->str = (char*)kmalloc(la + lb + 1);
            if (out->str) {
                memcpy(out->str, sa, la);
                memcpy(out->str + la, sb, lb);
                out->str[la + lb] = 0;
            }
        } else {
            out->integer = a.integer + b.integer;
        }
        aml_val_clear(&a); aml_val_clear(&b);
        aml_skip_target(ctx);
        return 0;
    }
    case 0x84: { /* ConcatenateResTemplate */
        acpi_value_t a, b;
        if (aml_eval_termarg(ctx, &a) != 0) return -1;
        if (aml_eval_termarg(ctx, &b) != 0) { aml_val_clear(&a); return -1; }
        u32 la = (a.kind == AO_BUFFER) ? a.buf_len : 8;
        u32 lb = (b.kind == AO_BUFFER) ? b.buf_len : 8;
        if (la >= 2 && a.buf) la -= 2;
        if (lb >= 2 && b.buf) lb -= 2;
        out->kind = AO_BUFFER;
        out->buf = (u8*)kmalloc(la + lb + 2);
        if (out->buf) {
            if (la && a.buf) memcpy(out->buf, a.buf, la);
            if (lb && b.buf) memcpy(out->buf + la, b.buf, lb);
            out->buf[la + lb]     = 0x79;
            out->buf[la + lb + 1] = 0x00;
            out->buf_len = la + lb + 2;
        }
        aml_val_clear(&a); aml_val_clear(&b);
        aml_skip_target(ctx);
        return 0;
    }
    case 0x11: { /* Buffer (PkgLength BufferSize ByteList) */
        const u8 *body_end;
        if (aml_open_pkg(ctx, &body_end) != 0) return -1;
        const u8 *saved_end = ctx->end;
        ctx->end = body_end;
        acpi_value_t sz;
        int rc = aml_eval_termarg(ctx, &sz);
        u64 size = (rc == 0) ? sz.integer : 0;
        aml_val_clear(&sz);
        if (size > 65536) size = 65536;
        out->kind = AO_BUFFER;
        out->buf = (u8*)kmalloc(size ? (usize)size : 1);
        if (out->buf) {
            memset(out->buf, 0, size);
            out->buf_len = (u32)size;
            u32 filled = 0;
            while (filled < out->buf_len && ctx->pc < ctx->end && *ctx->pc != 0x00) {
                out->buf[filled++] = *ctx->pc++;
            }
        }
        ctx->pc = body_end;
        ctx->end = saved_end;
        return 0;
    }
    case 0x12: { /* Package (PkgLength NumElements ElementList) */
        const u8 *body_end;
        if (aml_open_pkg(ctx, &body_end) != 0) return -1;
        if (ctx->pc >= ctx->end) return -1;
        u32 count = *ctx->pc++;
        const u8 *saved_end = ctx->end;
        ctx->end = body_end;
        out->kind = AO_PACKAGE;
        u32 alloc = count ? count : 1;
        out->elems = (acpi_value_t*)kmalloc(sizeof(acpi_value_t) * alloc);
        if (!out->elems) { ctx->end = saved_end; return -1; }
        memset(out->elems, 0, sizeof(acpi_value_t) * alloc);
        out->elem_count = 0;
        for (u32 i = 0; i < count && ctx->pc < ctx->end; i++) {
            acpi_value_t el;
            if (aml_eval_termarg(ctx, &el) != 0) break;
            out->elems[out->elem_count++] = el;
        }
        ctx->pc = body_end;
        ctx->end = saved_end;
        return 0;
    }
    default:
        ctx->pc--;
        aml_warn_op(op, (u32)(ctx->pc - ctx->start));
        return -1;
    }
}

/* ------------------------------------------------------------------ */
/* 5. statements                                                       */
/* ------------------------------------------------------------------ */

static void aml_process_fieldlist(exec_ctx_t *ctx, acpi_node_t *parent,
                                  acpi_node_t *region,
                                  acpi_node_t *index_reg, acpi_node_t *data_reg) {
    u32 bit_pos = 0;
    while (ctx->pc < ctx->end) {
        u8 tag = *ctx->pc;
        if (tag == 0x00) {                       /* ReservedField */
            ctx->pc++;
            u32 bits = aml_pkg_length(ctx);
            bit_pos += bits;
            continue;
        }
        if (tag == 0x01) {                       /* AccessField */
            if (ctx->pc + 3 > ctx->end) break;
            ctx->pc += 3;
            continue;
        }
        if (tag == 0x02) {                       /* ConnectionField */
            ctx->pc++;
            const u8 *body_end;
            if (aml_open_pkg(ctx, &body_end) != 0) break;
            ctx->pc = body_end;                  /* connection data skipped */
            continue;
        }
        if (tag == 0x03) {                       /* ExtendedAccessField */
            if (ctx->pc + 4 > ctx->end) break;
            ctx->pc += 4;
            continue;
        }
        if (ctx->pc + 4 > ctx->end) break;
        char seg[5];
        for (int k = 0; k < 4; k++) seg[k] = (char)ctx->pc[k];
        seg[4] = 0;
        ctx->pc += 4;
        u32 bits = aml_pkg_length(ctx);
        acpi_node_t *f = aml_create_node(parent, seg, AO_FIELD);
        if (f) {
            f->bit_offset = bit_pos;
            f->bit_len    = bits;
            if (region) f->region = region;
            if (index_reg && data_reg) {
                f->kind = AO_INDEXFIELD;
                f->index_reg = index_reg;
                f->data_reg  = data_reg;
            }
        }
        bit_pos += bits;
    }
}

static int aml_exec_statement(exec_ctx_t *ctx) {
    if (ctx->op_count++ > AML_OP_BUDGET) { aml_note("op budget exceeded"); return -1; }
    if (ctx->pc >= ctx->end) return 1;
    u8 op = *ctx->pc++;

    switch (op) {
    case 0x08: { /* Name */
        acpi_node_t *n = aml_parse_and_resolve(ctx, 1);
        if (!n) return -1;
        acpi_value_t v;
        if (aml_eval_termarg(ctx, &v) != 0) return -1;
        aml_store_value(n, &v);
        aml_val_clear(&v);
        return 0;
    }
    case 0x10: { /* Scope */
        const u8 *body_end;
        if (aml_open_pkg(ctx, &body_end) != 0) return -1;
        acpi_node_t *n = aml_parse_and_resolve(ctx, 1);
        if (!n) { ctx->pc = body_end; return 0; }
        if (n->kind == AO_INTEGER) n->kind = AO_SCOPE;
        acpi_node_t *saved_scope = ctx->scope;
        const u8 *saved_end = ctx->end;
        ctx->scope = n;
        ctx->end = body_end;
        int rc = aml_exec_termlist(ctx);
        ctx->scope = saved_scope;
        ctx->end = saved_end;
        ctx->pc = body_end;
        return rc;
    }
    case 0x14: { /* Method */
        const u8 *body_end;
        if (aml_open_pkg(ctx, &body_end) != 0) return -1;
        acpi_node_t *n = aml_parse_and_resolve(ctx, 1);
        if (!n) { ctx->pc = body_end; return 0; }
        u8 flags = (ctx->pc < ctx->end) ? *ctx->pc++ : 0;
        n->kind = AO_METHOD;
        n->method_arg_count = flags & 0x07;
        n->aml = ctx->pc;
        n->aml_len = (u32)(body_end - ctx->pc);
        g_method_count++;   /* node was created as AO_INTEGER placeholder */
        ctx->pc = body_end;
        return 0;
    }
    case 0x15: { /* External (NameSeg, ObjectType, ArgCount) */
        if (ctx->pc + 4 <= ctx->end) ctx->pc += 4;
        if (ctx->pc < ctx->end) ctx->pc += 2;
        return 0;
    }
    case 0x5B: {
        if (ctx->pc >= ctx->end) return -1;
        u8 op2 = *ctx->pc++;
        switch (op2) {
        case 0x80: { /* OpRegion */
            acpi_node_t *n = aml_parse_and_resolve(ctx, 1);
            if (!n) return -1;
            n->kind = AO_OPREGION;
            u8 space = (ctx->pc < ctx->end) ? *ctx->pc++ : 0;
            acpi_value_t off, ln;
            if (aml_eval_termarg(ctx, &off) != 0) return -1;
            if (aml_eval_termarg(ctx, &ln) != 0) { aml_val_clear(&off); return -1; }
            n->space = space;
            n->region_base = off.integer;
            n->region_len  = (u32)ln.integer;
            aml_val_clear(&off); aml_val_clear(&ln);
            return 0;
        }
        case 0x81: { /* Field */
            const u8 *body_end;
            if (aml_open_pkg(ctx, &body_end) != 0) return -1;
            acpi_node_t *region = aml_parse_and_resolve(ctx, 0);
            acpi_node_t *parent = region ? region->parent : ctx->scope;
            const u8 *saved_end = ctx->end;
            ctx->end = body_end;
            aml_process_fieldlist(ctx, parent, region, NULL, NULL);
            ctx->pc = body_end;
            ctx->end = saved_end;
            return 0;
        }
        case 0x86: { /* IndexField */
            const u8 *body_end;
            if (aml_open_pkg(ctx, &body_end) != 0) return -1;
            acpi_node_t *ireg = aml_parse_and_resolve(ctx, 0);
            acpi_node_t *dreg = aml_parse_and_resolve(ctx, 0);
            acpi_node_t *parent = ireg ? ireg->parent : ctx->scope;
            const u8 *saved_end = ctx->end;
            ctx->end = body_end;
            aml_process_fieldlist(ctx, parent, NULL, ireg, dreg);
            ctx->pc = body_end;
            ctx->end = saved_end;
            return 0;
        }
        case 0x82: { /* Device */
            const u8 *body_end;
            if (aml_open_pkg(ctx, &body_end) != 0) return -1;
            acpi_node_t *n = aml_parse_and_resolve(ctx, 1);
            if (!n) { ctx->pc = body_end; return 0; }
            n->kind = AO_DEVICE;
            acpi_node_t *saved_scope = ctx->scope;
            const u8 *saved_end = ctx->end;
            ctx->scope = n;
            ctx->end = body_end;
            int rc = aml_exec_termlist(ctx);
            ctx->scope = saved_scope;
            ctx->end = saved_end;
            ctx->pc = body_end;
            return rc;
        }
        case 0x83: case 0x84: case 0x85: { /* Processor / PowerRes / ThermalZone */
            const u8 *body_end;
            if (aml_open_pkg(ctx, &body_end) != 0) return -1;
            acpi_node_t *n = aml_parse_and_resolve(ctx, 1);
            if (!n) { ctx->pc = body_end; return 0; }
            n->kind = (op2 == 0x83) ? AO_PROCESSOR
                    : (op2 == 0x84) ? AO_POWERRES : AO_THERMALZONE;
            acpi_node_t *saved_scope = ctx->scope;
            const u8 *saved_end = ctx->end;
            ctx->scope = n;
            ctx->end = body_end;
            int rc = aml_exec_termlist(ctx);
            ctx->scope = saved_scope;
            ctx->end = saved_end;
            ctx->pc = body_end;
            return rc;
        }
        case 0x01: { /* Mutex */
            (void)aml_parse_and_resolve(ctx, 1);
            if (ctx->pc < ctx->end) ctx->pc++;
            return 0;
        }
        case 0x02: { /* Event */
            (void)aml_parse_and_resolve(ctx, 1);
            return 0;
        }
        case 0x06: { /* Alias */
            acpi_node_t *src = aml_parse_and_resolve(ctx, 0);
            acpi_node_t *dst = aml_parse_and_resolve(ctx, 1);
            if (src && dst) {
                dst->kind = src->kind;
                dst->integer = src->integer;
                dst->data = src->data; dst->data_len = src->data_len;
                dst->elems = src->elems; dst->elem_count = src->elem_count;
                dst->aml = src->aml; dst->aml_len = src->aml_len;
                dst->region = src->region;
                dst->bit_offset = src->bit_offset; dst->bit_len = src->bit_len;
                dst->buf_back = src->buf_back; dst->buf_back_len = src->buf_back_len;
                dst->index_reg = src->index_reg; dst->data_reg = src->data_reg;
            }
            return 0;
        }
        case 0x21: case 0x22: { /* Stall / Sleep - bounded no-op */
            acpi_value_t t;
            if (aml_eval_termarg(ctx, &t) != 0) return -1;
            aml_val_clear(&t);
            return 0;
        }
        case 0x87: case 0x88: { /* DataRegion + reserved */
            (void)aml_parse_and_resolve(ctx, 1);
            return 0;
        }
        default:
            /* expression-style extended op (Acquire / CondRefOf / Wait /
             * Release as a statement): rewind and evaluate as TermArg */
            ctx->pc -= 2;
            {
                acpi_value_t v;
                int rc = aml_eval_termarg(ctx, &v);
                aml_val_clear(&v);
                return rc;
            }
        }
    }
    case 0x06: { /* Alias (spec opcode) */
        acpi_node_t *src = aml_parse_and_resolve(ctx, 0);
        acpi_node_t *dst = aml_parse_and_resolve(ctx, 1);
        if (src && dst) {
            dst->kind = src->kind;
            dst->integer = src->integer;
            dst->data = src->data; dst->data_len = src->data_len;
            dst->elems = src->elems; dst->elem_count = src->elem_count;
            dst->aml = src->aml; dst->aml_len = src->aml_len;
            dst->region = src->region;
            dst->bit_offset = src->bit_offset; dst->bit_len = src->bit_len;
        }
        return 0;
    }
    case 0xA0: { /* If */
        const u8 *body_end;
        if (aml_open_pkg(ctx, &body_end) != 0) return -1;
        const u8 *saved_end = ctx->end;
        ctx->end = body_end;
        acpi_value_t pred;
        int rc = aml_eval_termarg(ctx, &pred);
        int taken = (rc == 0) && pred.integer;
        aml_val_clear(&pred);
        if (rc != 0) { ctx->pc = body_end; ctx->end = saved_end; return 0; }
        if (taken) {
            int rc2 = aml_exec_termlist(ctx);
            if (rc2 != 0) { ctx->pc = body_end; ctx->end = saved_end; return rc2; }
        }
        ctx->pc = body_end;
        ctx->end = saved_end;
        return 0;
    }
    case 0xA1: { /* Else */
        const u8 *body_end;
        if (aml_open_pkg(ctx, &body_end) != 0) return -1;
        const u8 *saved_end = ctx->end;
        ctx->end = body_end;
        int rc = aml_exec_termlist(ctx);
        ctx->pc = body_end;
        ctx->end = saved_end;
        return rc;
    }
    case 0xA2: { /* While */
        const u8 *body_end;
        if (aml_open_pkg(ctx, &body_end) != 0) return -1;
        const u8 *body_start = ctx->pc;
        const u8 *saved_end = ctx->end;
        ctx->end = body_end;
        int rc = 0;
        u32 guard = 0;
        while (1) {
            if (guard++ > 100000) { aml_note("while-loop guard tripped"); break; }
            ctx->pc = body_start;
            acpi_value_t pred;
            rc = aml_eval_termarg(ctx, &pred);
            if (rc != 0) break;
            int go = pred.integer;
            aml_val_clear(&pred);
            if (!go) break;
            ctx->break_flag = 0;
            ctx->continue_flag = 0;
            rc = aml_exec_termlist(ctx);
            if (rc != 0) break;
            if (ctx->break_flag) { ctx->break_flag = 0; break; }
            ctx->continue_flag = 0;
            if (ctx->has_return) break;
        }
        ctx->pc = body_end;
        ctx->end = saved_end;
        return rc;
    }
    case 0xA3: return 0;                          /* Noop */
    case 0xA4: { /* Return */
        acpi_value_t v;
        if (aml_eval_termarg(ctx, &v) != 0) return -1;
        aml_val_clear(&ctx->ret);
        ctx->ret = v;
        ctx->has_return = 1;
        return 0;
    }
    case 0xA5: ctx->break_flag = 1; return 0;     /* Break */
    case 0x9F: ctx->continue_flag = 1; return 0;  /* Continue */
    case 0xCC: return 0;                          /* BreakPoint */
    default:
        break;
    }

    /* expression statement */
    ctx->pc--;
    acpi_value_t v;
    int rc = aml_eval_termarg(ctx, &v);
    aml_val_clear(&v);
    return rc;
}

static int aml_exec_termlist(exec_ctx_t *ctx) {
    while (ctx->pc < ctx->end) {
        int rc = aml_exec_statement(ctx);
        if (rc != 0) return rc;
        if (ctx->has_return || ctx->break_flag || ctx->continue_flag) return 0;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* 6. public API                                                       */
/* ------------------------------------------------------------------ */

int driver_acpi_aml_load_table(u64 table_phys) {
    if (!table_phys) return -1;
    const acpi_sdt_hdr_t *h = (const acpi_sdt_hdr_t*)(uintptr_t)table_phys;
    if (h->length < sizeof(acpi_sdt_hdr_t) || h->length > (1u << 22)) {
        aml_note("table length out of range");
        return -1;
    }
    if (!driver_acpi_checksum_ok((const u8*)h, h->length)) {
        aml_note("table checksum bad");
        return -1;
    }
    /* The FIRST table (the DSDT) starts a fresh namespace; SSDTs
     * extend it (ACPI load semantics). */
    if (!g_loaded) aml_ns_reset();
    exec_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.scope = aml_root();
    ctx.start = (const u8*)h + sizeof(acpi_sdt_hdr_t);
    ctx.pc    = ctx.start;
    ctx.end   = (const u8*)h + h->length;
    ctx.load_mode = 1;
    (void)aml_exec_termlist(&ctx);
    g_loaded = 1;
    return 0;
}

int driver_acpi_aml_loaded(void) { return g_loaded; }
u32 driver_acpi_aml_node_count(void) { return g_node_count; }
u32 driver_acpi_aml_method_count(void) { return g_method_count; }

/* ---- device enumeration ---- */

static void aml_eisa_to_string(u32 raw, char *out, int outcap);

static int aml_extend_path(char *path, usize cap, const char *name) {
    usize l = strlen(path);
    /* Root-relative paths start at the namespace root: an empty base
     * path gets the '\' prefix first (so the first segment renders as
     * \_SB_ instead of _SB_, matching the ACPI canonical form). */
    if (l == 0) { path[l++] = '\\'; }
    else if (path[l - 1] != '\\') { path[l] = '.'; l++; }
    if (l + 5 >= cap) return 0;
    for (int k = 0; k < 4; k++) path[l + k] = name[k];
    path[l + 4] = 0;
    return 1;
}

static int aml_collect_devices(acpi_node_t *n, char *path) {
    int count = 0;
    for (acpi_node_t *c = n->first_child; c; c = c->next_sibling) {
        if (c->kind == AO_DEVICE || c->kind == AO_PROCESSOR ||
            c->kind == AO_THERMALZONE || c->kind == AO_POWERRES) {
            count++;
        }
        if (c->kind == AO_SCOPE || c->kind == AO_DEVICE ||
            c->kind == AO_PROCESSOR || c->kind == AO_THERMALZONE ||
            c->kind == AO_POWERRES) {
            char tmp[96];
            strcpy(tmp, path);
            if (aml_extend_path(tmp, sizeof(tmp), c->name))
                count += aml_collect_devices(c, tmp);
        }
    }
    return count;
}

/* Direct-children-only lookup (namespace member access, NOT the
 * parent-searching reference lookup): _HID/_STA/_ADR of a device are
 * its own children, never inherited. */
static acpi_node_t *aml_child_lookup(acpi_node_t *parent, const char *name) {
    if (!parent) return NULL;
    for (acpi_node_t *c = parent->first_child; c; c = c->next_sibling)
        if (strcmp(c->name, name) == 0) return c;
    return NULL;
}

static int aml_walk_devices(acpi_node_t *n, char *path, int *idx, int target,
                            acpi_aml_dev_info_t *out) {
    for (acpi_node_t *c = n->first_child; c; c = c->next_sibling) {
        int is_dev = (c->kind == AO_DEVICE || c->kind == AO_PROCESSOR ||
                      c->kind == AO_THERMALZONE || c->kind == AO_POWERRES);
        if (is_dev) {
            if (out && *idx == target) {
                strcpy(out->path, path);
                (void)aml_extend_path(out->path, sizeof(out->path), c->name);
                out->is_device = (c->kind == AO_DEVICE);
                out->hid[0] = 0; out->cid[0] = 0;
                out->adr = 0;
                out->sta = -1;
                acpi_value_t v;
                acpi_node_t *hn = aml_child_lookup(c, "_HID");
                if (hn && hn->kind == AO_METHOD) {
                    if (aml_invoke_method(hn, NULL, 0, &v, 0) == 0) {
                        if (v.kind == AO_STRING && v.str) {
                            for (int k = 0; k < 11 && v.str[k]; k++) out->hid[k] = v.str[k];
                            out->hid[11] = 0;
                        } else if (v.kind == AO_INTEGER) {
                            aml_eisa_to_string((u32)v.integer, out->hid, 12);
                        }
                    }
                    aml_val_clear(&v);
                } else if (hn && (hn->kind == AO_STRING || hn->kind == AO_INTEGER)) {
                    if (hn->kind == AO_STRING) {
                        for (int k = 0; k < 11 && hn->data && hn->data[k]; k++) out->hid[k] = (char)hn->data[k];
                        out->hid[11] = 0;
                    } else {
                        aml_eisa_to_string((u32)hn->integer, out->hid, 12);
                    }
                }
                acpi_node_t *cn = aml_child_lookup(c, "_CID");
                if (cn && cn->kind == AO_METHOD) {
                    if (aml_invoke_method(cn, NULL, 0, &v, 0) == 0) {
                        const char *s = NULL;
                        if (v.kind == AO_STRING && v.str) s = v.str;
                        else if (v.kind == AO_PACKAGE && v.elem_count &&
                                 v.elems[0].kind == AO_STRING && v.elems[0].str)
                            s = v.elems[0].str;
                        if (s) {
                            for (int k = 0; k < 11 && s[k]; k++) out->cid[k] = s[k];
                            out->cid[11] = 0;
                        }
                    }
                    aml_val_clear(&v);
                }
                acpi_node_t *an = aml_child_lookup(c, "_ADR");
                if (an && an->kind == AO_METHOD) {
                    if (aml_invoke_method(an, NULL, 0, &v, 0) == 0)
                        out->adr = (u32)v.integer;
                    aml_val_clear(&v);
                } else if (an) {
                    out->adr = (u32)an->integer;
                }
                acpi_node_t *sn = aml_child_lookup(c, "_STA");
                if (sn && sn->kind == AO_METHOD) {
                    if (aml_invoke_method(sn, NULL, 0, &v, 0) == 0)
                        out->sta = (int)(v.integer & 0xFF);
                    aml_val_clear(&v);
                } else if (sn && sn->kind == AO_INTEGER) {
                    out->sta = (int)(sn->integer & 0xFF);
                }
                (*idx)++;
                return 1;
            }
            (*idx)++;
        }
        if (c->kind == AO_SCOPE || c->kind == AO_DEVICE ||
            c->kind == AO_PROCESSOR || c->kind == AO_THERMALZONE ||
            c->kind == AO_POWERRES) {
            char tmp[96];
            strcpy(tmp, path);
            if (aml_extend_path(tmp, sizeof(tmp), c->name)) {
                if (aml_walk_devices(c, tmp, idx, target, out)) return 1;
            }
        }
    }
    return 0;
}

int driver_acpi_aml_device_count(void) {
    return aml_collect_devices(aml_root(), "");
}

int driver_acpi_aml_device_info(int idx, acpi_aml_dev_info_t *out) {
    if (idx < 0 || !out) return -1;
    int cursor = 0;
    return aml_walk_devices(aml_root(), "", &cursor, idx, out) ? 0 : -1;
}

/* ---- evaluation helpers ---- */

static acpi_node_t *aml_find_by_path(const char *path) {
    if (!path || !path[0]) return NULL;
    if (path[0] == '\\' && !path[1]) return aml_root();
    acpi_node_t *cur = aml_root();
    const char *p = path;
    if (*p == '\\') p++;
    while (*p) {
        char seg[5];
        int k = 0;
        while (*p && *p != '.' && k < 4) seg[k++] = *p++;
        /* pad short segments with '_' (display form "\_SB" == "_SB_") */
        while (k < 4) seg[k++] = '_';
        seg[4] = 0;
        while (*p == '.') p++;
        if (cur) cur = aml_lookup(cur, seg);
        if (!cur) return NULL;
    }
    return cur;
}

int driver_acpi_aml_eval_integer(const char *path, u64 *out) {
    acpi_node_t *n = aml_find_by_path(path);
    if (!n) return -1;
    acpi_value_t v;
    if (n->kind == AO_METHOD) {
        if (aml_invoke_method(n, NULL, 0, &v, 0) != 0) return -1;
    } else if (n->kind == AO_FIELD || n->kind == AO_INDEXFIELD) {
        u64 x = 0;
        if (aml_field_read(n, &x, NULL) != 0) return -1;
        if (out) *out = x;
        return 0;
    } else {
        aml_node_value(n, &v);
    }
    if (v.kind != AO_INTEGER) { aml_val_clear(&v); return -1; }
    if (out) *out = v.integer;
    aml_val_clear(&v);
    return 0;
}

int driver_acpi_aml_package_elements(const char *path, int *count) {
    acpi_node_t *n = aml_find_by_path(path);
    if (!n) return -1;
    acpi_value_t v;
    if (n->kind == AO_METHOD) {
        if (aml_invoke_method(n, NULL, 0, &v, 0) != 0) return -1;
    } else {
        aml_node_value(n, &v);
    }
    if (v.kind != AO_PACKAGE) { aml_val_clear(&v); return -1; }
    if (count) *count = (int)v.elem_count;
    aml_val_clear(&v);
    return 0;
}

int driver_acpi_aml_eval_package_integer(const char *path, int idx, u64 *out) {
    acpi_node_t *n = aml_find_by_path(path);
    if (!n) return -1;
    acpi_value_t v;
    if (n->kind == AO_METHOD) {
        if (aml_invoke_method(n, NULL, 0, &v, 0) != 0) return -1;
    } else {
        aml_node_value(n, &v);
    }
    if (v.kind != AO_PACKAGE || idx < 0 || (u32)idx >= v.elem_count) {
        aml_val_clear(&v);
        return -1;
    }
    if (out) *out = v.elems[idx].integer;
    aml_val_clear(&v);
    return 0;
}

/* EisaId integers are stored byte-swapped (the 4 bytes on the wire
 * read back as the ID with reversed byte order); swap before the
 * standard char mapping (ACPICA AcpiExEisaIdToString). */
static u32 aml_eisa_swap(u32 v) {
    return ((v & 0xFF) << 24) | ((v & 0xFF00) << 8) |
           ((v >> 8) & 0xFF00) | ((v >> 24) & 0xFF);
}

static void aml_eisa_to_string(u32 raw, char *out, int outcap) {
    static const char enc[] = "@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_";
    u32 x = aml_eisa_swap(raw);
    char hex[8];
    if (outcap < 8) return;
    out[0] = enc[(x >> 26) & 0x1F];
    out[1] = enc[(x >> 21) & 0x1F];
    out[2] = enc[(x >> 16) & 0x1F];
    u64_to_hex((u64)((x >> 12) & 0xF), hex, 1); out[3] = hex[0];
    u64_to_hex((u64)((x >> 8) & 0xF), hex, 1);  out[4] = hex[0];
    u64_to_hex((u64)((x >> 4) & 0xF), hex, 1);  out[5] = hex[0];
    u64_to_hex((u64)(x & 0xF), hex, 1);         out[6] = hex[0];
    out[7] = 0;
}

int driver_acpi_aml_eval_hid(const char *path, char *out, int outlen) {
    acpi_node_t *n = aml_find_by_path(path);
    if (!n) return -1;
    acpi_node_t *hn = aml_lookup(n, "_HID");
    if (!hn) return -1;
    acpi_value_t v;
    if (hn->kind == AO_METHOD) {
        if (aml_invoke_method(hn, NULL, 0, &v, 0) != 0) return -1;
    } else {
        aml_node_value(hn, &v);
    }
    int rc = -1;
    if (out && outlen > 0) {
        if (v.kind == AO_STRING && v.str) {
            for (int k = 0; k < outlen - 1 && v.str[k]; k++) out[k] = v.str[k];
            out[outlen - 1] = 0;
            rc = 0;
        } else if (v.kind == AO_INTEGER) {
            aml_eisa_to_string((u32)v.integer, out, outlen);
            rc = 0;
        }
    }
    aml_val_clear(&v);
    return rc;
}

int driver_acpi_aml_eval_cid(const char *path, char *out, int outlen) {
    acpi_node_t *n = aml_find_by_path(path);
    if (!n) return -1;
    acpi_node_t *cn = aml_lookup(n, "_CID");
    if (!cn) return -1;
    acpi_value_t v;
    if (cn->kind == AO_METHOD) {
        if (aml_invoke_method(cn, NULL, 0, &v, 0) != 0) return -1;
    } else {
        aml_node_value(cn, &v);
    }
    int rc = -1;
    const char *s = NULL;
    if (v.kind == AO_STRING && v.str) s = v.str;
    else if (v.kind == AO_PACKAGE && v.elem_count &&
             v.elems[0].kind == AO_STRING && v.elems[0].str) s = v.elems[0].str;
    if (s && out && outlen > 0) {
        for (int k = 0; k < outlen - 1 && s[k]; k++) out[k] = s[k];
        out[outlen - 1] = 0;
        rc = 0;
    }
    aml_val_clear(&v);
    return rc;
}

int driver_acpi_aml_eval_buffer(const char *path, u8 *out, int outcap, int *outlen) {
    acpi_node_t *n = aml_find_by_path(path);
    if (!n) return -1;
    acpi_value_t v;
    if (n->kind == AO_METHOD) {
        if (aml_invoke_method(n, NULL, 0, &v, 0) != 0) return -1;
    } else {
        aml_node_value(n, &v);
    }
    if (v.kind != AO_BUFFER || !v.buf) { aml_val_clear(&v); return -1; }
    if (out && v.buf_len > (u32)outcap) { aml_val_clear(&v); return -2; }
    if (out) memcpy(out, v.buf, v.buf_len);
    if (outlen) *outlen = (int)v.buf_len;
    aml_val_clear(&v);
    return 0;
}

/* ---- GPE method dispatch ---- */

static void aml_gpe_name(u8 gpe_number, int level_triggered, char out[5]) {
    char hex[8];
    out[0] = '_';
    out[1] = level_triggered ? 'L' : 'E';
    u64_to_hex((u64)(gpe_number >> 4), hex, 1);
    out[2] = (hex[0] >= 'a' && hex[0] <= 'f') ? (char)(hex[0] - 'a' + 'A') : hex[0];
    u64_to_hex((u64)(gpe_number & 0xF), hex, 1);
    out[3] = (hex[0] >= 'a' && hex[0] <= 'f') ? (char)(hex[0] - 'a' + 'A') : hex[0];
    out[4] = 0;
}

int driver_acpi_aml_gpe_method_exists(u8 gpe_number) {
    acpi_node_t *gpe = aml_lookup(aml_root(), "_GPE");
    if (!gpe) return 0;
    char name[5];
    aml_gpe_name(gpe_number, 1, name);
    if (aml_lookup(gpe, name)) return 1;
    aml_gpe_name(gpe_number, 0, name);
    return aml_lookup(gpe, name) ? 2 : 0;
}

int driver_acpi_aml_run_gpe_method(u8 gpe_number, int level_triggered) {
    acpi_node_t *gpe = aml_lookup(aml_root(), "_GPE");
    if (!gpe) return -1;
    char name[5];
    aml_gpe_name(gpe_number, level_triggered, name);
    acpi_node_t *m = aml_lookup(gpe, name);
    if (!m || m->kind != AO_METHOD) return -1;
    acpi_value_t v;
    if (aml_invoke_method(m, NULL, 0, &v, 0) != 0) return -1;
    aml_val_clear(&v);
    return 0;
}

void driver_acpi_aml_notify_power_button(void) {
    acpi_node_t *pwr = aml_lookup(aml_root(), "_SB_");
    if (pwr) pwr = aml_lookup(pwr, "PWRB");
    if (!pwr) {
        aml_note("power button: no \\_SB.PWRB in namespace (event counted)");
        return;
    }
    acpi_node_t *evt = aml_lookup(pwr, "_EVT");
    if (evt && evt->kind == AO_METHOD) {
        acpi_value_t v;
        if (aml_invoke_method(evt, NULL, 0, &v, 0) == 0) aml_val_clear(&v);
        return;
    }
    aml_note("power button event (no _EVT method)");
}

/* ------------------------------------------------------------------ */
/* 7. self-test (compiled-in fixture table, no hardware)               */
/* ------------------------------------------------------------------ */

/*
 * Fixture AML (hand-assembled, matches the parser's grammar):
 *   Scope(\_SB) {
 *     Device(TPAD) {
 *       Name(_HID, "PNP0C50")
 *       Name(_ADR, 1)
 *       Method(_STA) { Return(0x0F) }
 *     }
 *     Name(SPKG, Package(3) { 1, 2, 0x40 })
 *     Method(TEST) { Store(Add(Multiply(5, 3, Zero), 2, Zero), Local0)
 *                    Return(Local0) }
 *   }
 */
static const u8 aml_fixture[] = {
    /* Scope(\_SB) {  pkg = (0x05<<4)|(0x40&0xF) = 80 =
     * 2(len enc) + 5(name) + 38(Device) + 14(Name) + 21(Method) */
    0x10, 0x40, 0x05, 0x5C, 0x5F, 0x53, 0x42, 0x5F,
        /* Device(TPAD) {  pkg = 1 + 4(name) + 14 + 7 + 10 = 36 */
        0x5B, 0x82, 0x24, 0x54, 0x50, 0x41, 0x44,
            /* Name(_HID, "PNP0C50") */
            0x08, 0x5F, 0x48, 0x49, 0x44, 0x0D,
            'P', 'N', 'P', '0', 'C', '5', '0', 0x00,
            /* Name(_ADR, 1) */
            0x08, 0x5F, 0x41, 0x44, 0x52, 0x0A, 0x01,
            /* Method(_STA) { Return(0x0F) } : 0x14 09 _STA 00 A4 0A 0F */
            0x14, 0x09, 0x5F, 0x53, 0x54, 0x41, 0x00, 0xA4, 0x0A, 0x0F,
        /* } end Device: pkg = 1(len byte) + 4(name) + 14 + 7 + 10 = 36 = 0x24 */
        /* Name(SPKG, Package(3){1,2,0x40}) */
        0x08, 0x53, 0x50, 0x4B, 0x47, 0x12, 0x08, 0x03,
            0x0A, 0x01, 0x0A, 0x02, 0x0A, 0x40,
        /* Method(TEST) { Store(Add(Multiply(5,3,0),2,0), Local0); Return(Local0) } */
        0x14, 0x14, 0x54, 0x45, 0x53, 0x54, 0x00,
            0x70, 0x72, 0x77, 0x0A, 0x05, 0x0A, 0x03, 0x00,
                 0x0A, 0x02, 0x00, 0x60,
            0xA4, 0x60,
    /* } end Scope */
};

static u32 g_selftest_result;

u32 driver_acpi_aml_selftest_result(void) { return g_selftest_result; }

int driver_acpi_aml_selftest(void) {
    /* Wrap the fixture into a minimal SDT (36-byte header + body). */
    u8 buf[512];
    memset(buf, 0, sizeof(buf));
    u32 body = sizeof(aml_fixture);
    u32 total = 36 + body;
    memcpy(buf, "FIXT", 4);
    buf[4] = (u8)(total);
    buf[5] = (u8)(total >> 8);
    buf[6] = (u8)(total >> 16);
    buf[7] = (u8)(total >> 24);
    buf[8] = 1;                 /* revision */
    /* checksum filled below */
    memcpy(buf + 36, aml_fixture, body);
    u8 sum = 0;
    for (u32 i = 0; i < total; i++) sum = (u8)(sum + buf[i]);
    buf[9] = (u8)(0x100 - sum);

    /* Remember live namespace stats (the fixture reloads the namespace). */
    u32 live_nodes = g_node_count;
    int rc = driver_acpi_aml_load_table((u64)(uintptr_t)buf);
    if (rc != 0) return -1;

    int fails = 0;
    u64 v = 0;
    char hid[16];

    if (driver_acpi_aml_eval_integer("\\_SB_.TPAD._STA", &v) != 0 || v != 0x0F) fails++;
    if (driver_acpi_aml_eval_integer("\\_SB_.TPAD._ADR", &v) != 0 || v != 1) fails++;
    if (driver_acpi_aml_eval_hid("\\_SB_.TPAD", hid, sizeof(hid)) != 0 ||
        strcmp(hid, "PNP0C50") != 0) fails++;
    if (driver_acpi_aml_eval_integer("\\_SB_.TEST", &v) != 0 || v != 17) fails++;
    if (driver_acpi_aml_eval_package_integer("\\_SB_.SPKG", 2, &v) != 0 || v != 0x40) fails++;
    if (driver_acpi_aml_device_count() < 1) fails++;
    if (g_node_count < live_nodes) fails++;   /* namespace was reset+rebuilt */

    /* hand the clean namespace back: the fixture must not leak into
     * the live DSDT/SSDT namespace that the caller loads next */
    aml_ns_reset();
    g_loaded = 0;
    g_selftest_result = (u32)fails;
    return fails == 0 ? 0 : -1;
}
