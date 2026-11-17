<!-- SPDX-License-Identifier: Apache-2.0 -->
# Application dictionary - Kernel log (2)

### logcat

| Field | Value |
|---|---|
| Name | logcat |
| Category | Kernel log |
| Function | filtered kernel log stream |
| Usage | `logcat -l lvl -m mod -k key -t sec -n N [-f]` |
| Return codes | 0, 1 |
| Source | `ocos/services/klog/src/svc_klog_l1.c` |

**Executed example**:

```
ld_so

*** EXCEPTION 14: #PF Page Fault ***
  error_code=0x0000000000000005  rip=0x000000001000192D  cs=0x0000000000000033  rflags=0x0000000000010212
  rsp=0x000000003FFFFEA8  ss=0x000000000000003B
  cr2=0x0000000008000018  (faulting address)
*** KILLING user process (pid=79) due to exception 14, kernel continues ***
[user] process killed by exception (vector=14), kernel continues
ush> 
```

### logctl

| Field | Value |
|---|---|
| Name | logctl |
| Category | Kernel log |
| Function | kernel log level control |
| Usage | `logctl status\|level\|...; logctl enable\|disable debug\|info\|warn\|error\|fatal ; logctl filter clear \| filter [level] [module] [keyword] ; logctl status\|level\|enable\|disable\|clear\|flush\|rotate\|threshold\|export\|filter\|test ; logctl threshold BYTES (min 512, default 1048576` |
| Hidden parameters | hidden parameter, not a public interface; compatibility is not guaranteed: `-` |
| Return codes | 0, 1 |
| Source | `ocos/services/klog/src/svc_klog_l1.c` |

**Executed example**:

```
ldd
usage: ldd <program>
ush> 
```
