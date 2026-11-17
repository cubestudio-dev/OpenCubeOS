<!-- SPDX-License-Identifier: Apache-2.0 -->
# 应用字典 - 内核日志 (2)

### logcat

| 字段 | 值 |
|---|---|
| 名称 | logcat |
| 分类 | 内核日志 |
| 功能 | 带过滤的内核日志流 |
| 用法 | `logcat -l lvl -m mod -k key -t sec -n N [-f]` |
| 返回码 | 0, 1 |
| 来源 | `ocos/services/klog/src/svc_klog_l1.c` |

**实际执行示例**：

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

| 字段 | 值 |
|---|---|
| 名称 | logctl |
| 分类 | 内核日志 |
| 功能 | 内核日志级别控制 |
| 用法 | `logctl status|level|...; logctl enable|disable debug|info|warn|error|fatal ; logctl filter clear | filter [level] [module] [keyword] ; logctl status|level|enable|disable|clear|flush|rotate|threshold|export|filter|test ; logctl threshold BYTES (min 512, default 1048576` |
| 隐藏参数 | 隐藏参数，非公开接口，兼容性不保证: `-` |
| 返回码 | 0, 1 |
| 来源 | `ocos/services/klog/src/svc_klog_l1.c` |

**实际执行示例**：

```
ldd
usage: ldd <program>
ush> 
```
