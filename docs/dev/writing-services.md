<!-- SPDX-License-Identifier: Apache-2.0 -->
# Writing a service (and a driver)

How to add a background service to the L0 kernel: the
`OC_SERVICE_DECL` self-registration framework, the `.oc_svcs`
linker section, kmain collection, dispatch routing - as a complete
worked example (a minimal echo service), plus the driver-side
`OC_DRIVER_DECL` story proven by `drv_misc_placeholder.c`. For the
existing surface's numbering see
[interface-dictionary.md](interface-dictionary.md); for the ring-3
app side see [../l1/writing-apps.md](../l1/writing-apps.md).

## 1. What a "service" is

There are **no service processes**. A service is a folder under
`ocos/services/<category>/` that owns (a) a set of mechanisms and
(b) an optional ring-3 surface reached through one syscall
(`SYS_L1SVC`). Services are link-time-registered kernel modules;
the only processes spawned at boot are ush (the default L1 entry)
and, on supervised boots, the OCIRE log agent.

## 2. The framework (verbatim)

From `ocos/services/svc_framework.h`:

```c
typedef void (*oc_service_init_fn_t)(void);
typedef int  (*oc_service_late_fn_t)(void);   /* 0 = probed something */
typedef int  (*oc_service_l1_fn_t)(u32 op, const char *arg, char *out, int cap);

typedef struct {
    const char         *category;   /* == services/<category> folder */
    const char         *desc;
    oc_service_init_fn_t init;      /* early bring-up, may be NULL */
    oc_service_late_fn_t late_init; /* probe pass, may be NULL */
    oc_service_l1_fn_t  l1_dispatch; /* SYS_L1SVC entry, may be NULL */
} oc_service_decl_t;

#define OC_SERVICE_DECL(cat, dsc, ini, late) \
    static const oc_service_decl_t OC_DECL_UID(__ocsv_) \
    __attribute__((used, section(".oc_svcs"))) = { (cat), (dsc), (ini), (late), (oc_service_l1_fn_t)0 }

#define OC_SERVICE_DECL_L1(cat, dsc, ini, late, disp) \
    static const oc_service_decl_t OC_DECL_UID(__ocsv_) \
    __attribute__((used, section(".oc_svcs"))) = { (cat), (dsc), (ini), (late), (disp) }
```

Collection + dispatch (`ocos/services/svc_framework.c`):

```c
int svc_framework_dispatch(const char *cat, u32 op,
                            const char *arg, char *out, int cap)
{
    if (!cat || !out || cap <= 0) return -1;
    for (int i = 0; i < s_nsvcs; i++) {
        const oc_service_decl_t *s = s_svcs[i];
        const char *a = s->category, *b = cat;
        while (*a && *b && *a == *b) { a++; b++; }
        if (*a == 0 && *b == 0) {
            if (!s->l1_dispatch) return -1;   /* kernel-only service */
            return s->l1_dispatch(op, arg, out, cap);
        }
    }
    return -1;
}
```

Rules from the header:

- the category string equals the `ocos/services/<category>/`
  folder name, one-to-one, with ONE declared exception: category
  `"ocire"` (the frozen ABI name of the OCIRE log-agent contract)
  is implemented by `services/ocembed/`;
- boot ORDER stays the explicit kmain composition-root sequence
  (the hand-curated dependency chain); the collected table
  (`s_svcs[64]`) is only the identity/dispatch surface;
- the NAK rule: "everything is extensible by adding a file".

## 3. Worked example: a minimal echo service

Goal: one new category `echo` with two ops - `hello` (op 1, greets
by name) and `shout` (op 2, uppercases the arg). New op numbers
are **appended only** (existing numbers are frozen ABI).

### 3.1 The op numbers (the ABI step)

Add to `ocos/services/sysapi/interface/svc_sysapi_l1svc.h`:

```c
#define L1SVC_CAT_ECHO      21   /* services/echo     */
#define L1SVC_ECHO_HELLO    1    /* hello [name]       */
#define L1SVC_ECHO_SHOUT    2    /* shout <text>       */
```

Run `python3 tools/gen_l1svc_ops.py` - it mirrors the single-line
`#define L1SVC_*` block verbatim into
`ocos/interface/l1_svc_ops.h` (the only header set a ring-3 app may
include; the `L1SVC_CAT_NAME` function macro is skipped). The
ush-side constant table carries a verbatim copy; the whole
numbering is append-only so no existing consumer ever shifts.

### 3.2 The service implementation

`ocos/services/echo/src/svc_echo_l1.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* The echo service: the smallest OC_SERVICE_DECL_L1 example.
 * Mechanisms: none. Ring-3 surface: two buffer-render ops. */
#include "types.h"
#include "core_lib_string.h"
#include "svc_sysapi_l1svc.h"

static int echo_put(char *out, int cap, int off, const char *s)
{
    while (*s && off < cap - 1)
        out[off++] = *s++;
    out[off] = 0;                 /* ALWAYS terminate (WP06g2 bug) */
    return off;
}

static int echo_l1_dispatch(u32 op, const char *arg, char *out, int cap)
{
    int off = 0;
    switch (op) {
    case L1SVC_ECHO_HELLO:
        off = echo_put(out, cap, off, "hello, ");
        off = echo_put(out, cap, off, (arg && *arg) ? arg : "world");
        off = echo_put(out, cap, off, "\n");
        return 0;
    case L1SVC_ECHO_SHOUT: {
        if (!arg || !*arg) {
            echo_put(out, cap, 0, "usage: shout <text>\n");
            return 0;             /* failure text, successful call */
        }
        for (const char *p = arg; *p && off < cap - 1; p++)
            out[off++] = (char)((*p >= 'a' && *p <= 'z') ? *p - 32 : *p);
        out[off++] = '\n';
        out[off] = 0;
        return 0;
    }
    default:
        echo_put(out, cap, 0, "echo: unknown op\n");
        return -1;
    }
}

OC_SERVICE_DECL_L1("echo", "echo service (greeting + uppercase + ring-3 surface)",
                   0, 0, echo_l1_dispatch);
```

Notes on the contract this example demonstrates:

- **Buffer-render style**: the dispatcher writes into `out` with
  its own helper and ALWAYS NUL-terminates (the WP06g2 live-caught
  bug: `put()` that never terminated made `strlen(kout)` read heap
  garbage).
- **Failure text vs routing failure**: a bad argument prints
  `usage:` into `out` and returns 0 - the syscall still succeeds
  (only an unrouteable cat/op returns -1).
- **Direct vs captured**: this service renders directly. If your
  op body is an old command body that prints via
  `screen_console_puts`, wrap it in `core_kcap_run_fn(fn, arg, out, cap)` instead; if it owns a live session or parks (sshd, TUI,
  `power sleep`), route direct with `out[0] = 0; return fn(arg);`
  - running a live session captured swallows console output (see
  the kcap trap in [debug-handbook.md](debug-handbook.md) 4.3).
- Long ops may sleep: `sys_l1svc` wraps dispatch in sti/cli, so
  tick-driven waits are legal.

### 3.3 Registering the category name

`L1SVC_CAT_NAME(n)` in the sysapi header maps the number to the
folder string; add `case 21: return "echo";`. The string must
equal the folder name exactly.

### 3.4 Makefile integration

There is **no per-service Makefile and no hand-edited source
list**. The top-level Makefile's `KERNEL_DIRS` wildcards
`ocos/services/*/src`, so dropping
`ocos/services/echo/src/svc_echo_l1.c` into the tree is the whole
integration; the multi-`-I` plain-basename include strategy
(`services/*/interface` is on the path) finds
`svc_sysapi_l1svc.h`. Boot order needs nothing: nothing calls your
service's init from kmain (use the `init`/`late_init` slots only
if you need a probe pass - see `snd`/`nic`/`block`, which use
`snd_svc_init` + `snd_svc_probe_all` style helpers).

After `make kernel`, the boot line
`registry: drv=N svc=N app=N` counts your service, and
`sys_l1svc(21, ...)` routes to it.

### 3.5 The ring-3 wrapper app

`ocos/l1apps/echo/main.c`:

```c
#include "l1_api_syscall.h"
#include "l1_svc_ops.h"
int app_main(int argc, char **argv)
{ return l1svc_main(argc, argv, L1SVC_CAT_ECHO, L1SVC_ECHO_HELLO); }
```

`l1svc_main` (in `ocos/interface/l1_api_syscall.h`) joins argv into a
256-byte arg line, calls `sys_l1svc(cat, op, arg, out, 32768)`
with a static 32 KiB BSS buffer, prints `out` plus the missing
newline. `tools/gen_userprogs.py` picks the folder up
automatically on the next `make userprogs` (a new folder IS a new
command - nothing is hand-registered). Ship `i18n/{en,zh}.json`
with at least the reserved `_hint` pair, or the build fails
("a program without translations is a delivery violation").

## 4. Adding a driver (the OC_DRIVER_DECL side)

From `ocos/drivers/drv_framework.h`:

```c
typedef struct {
    const char *family;
    const void *ops;        /* const drv_*_ops_t* (typed per family) */
    const char *name;
    const char *desc;
} oc_driver_decl_t;

#define OC_DRIVER_DECL(fam, nm, opsp, dsc) \
    static const oc_driver_decl_t OC_DECL_UID(__ocdv_) \
    __attribute__((used, section(".oc_drvs"))) = { (fam), (opsp), (nm), (dsc) }
```

Minimal steps:

1. Create `ocos/drivers/<class>/drv_<class>_<name>.c`.
2. Fill a family ops table (e.g. `blk_drv_ops_t` - a probe
   callback that scans and adds devices via `blk_dev_add()`; or
   `driver_block_ops_t` read/write/flush for the device itself).
3. Declare identity with one `OC_DRIVER_DECL("block", "mydrv", &g_probe, "description")` line.
4. Done: `drv_framework_collect()` (kmain) walks `.oc_drvs`
   (boundaries from linker.ld); the owning family manager
   (block/nic/snd/usb/wireless/sdio) walks the table via
   `drv_framework_at(i)` during its probe pass and calls your
   probe when the family string matches.

The proof that this is really "add one file", verbatim from
`ocos/drivers/misc/drv_misc_placeholder.c`:

```c
/* Minimal ops table: a marker constant.  Family managers match on
 * the family string ("block"/"nic"/"snd"/"usb"/"wireless"/"sdio");
 * "placeholder" owns no manager, so this descriptor is collected and
 * reported but never probed. */
static const int drv_placeholder_ops_marker = 0x504C4143; /* "PLAC" */

OC_DRIVER_DECL("placeholder", "placeholder",
               &drv_placeholder_ops_marker,
               "NAK registration-mechanism test driver (collected, never probed)");
```

The file's header states the acceptance item: adding a driver must
require NOTHING but one .c file in `ocos/drivers/` + one
OC_DRIVER_DECL line - no global table edit, no registration call
site, no core file change. The placeholder is collected at boot
(the drivers count in the boot registry line includes it) and
nothing else changes: no probe runs, no device appears.

## 5. Discipline checklist

- **Numbers append only** - never renumber, never reuse; cat 18's
  ops are frozen against the OCIRE agent ABI.
- **Category string == folder name** (the "ocire" exception is
  declared and frozen).
- **Output is the ABI**: plain ASCII English, stable anchors; ring
  3 formats nothing; wizards grep your sentences.
- **Always NUL-terminate** `out`; keep `cap` discipline; big
  buffers are static/BSS on the ring-3 side (32 KiB on the stack
  is a bug), kmalloc on the kernel side (the image is pinned below
  0x400000 - move large buffers out of .bss/.data).
- **Captured bodies must not sleep or own live sessions** - route
  direct instead.
- **Zero warnings**: `-Wall -Wextra -Werror` is the project lint;
  every file carries the SPDX header.
- **Dependency direction**: kernel must not include
  `drivers/`/`services/` headers (exception: the kmain composition
  root); drivers must not include services; services may include
  each other's *mechanism* interfaces, never each other's
  `l1_dispatch` (checked by `tools/check_deps.py`).
- **Test hooks belong in the surface**: the L1 face IS the test
  face (see the ~93 stubs in [debug-interfaces.md](debug-interfaces.md));
  an honest SKIP is better than a fake PASS.
