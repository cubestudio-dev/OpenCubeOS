<!-- SPDX-License-Identifier: Apache-2.0 -->
# 应用字典 - 内核时代测试程序（散装源码） (33)

### badapp

| 字段 | 值 |
|---|---|
| 名称 | badapp |
| 分类 | 内核时代测试程序（散装源码） |

**实际执行示例**：

```
badapp
badapp: trying kernel access

*** EXCEPTION 14: #PF Page Fault ***
  error_code=0x0000000000000005  rip=0x000000000040001D  cs=0x0000000000000033  rflags=0x0000000000010202
  rsp=0x000000003FFFFEE0  ss=0x000000000000003B
  cr2=0x00000000FD000000  (faulting address)
*** KILLING user process (pid=18) due to exception 14, kernel continues ***
[user] process killed by exception (vector=14), kernel continues
ush> 
```

### dlsym_test

| 字段 | 值 |
|---|---|
| 名称 | dlsym_test |
| 分类 | 内核时代测试程序（散装源码） |
| 功能 | SPDX-License-Identifier: Apache-2 |
| 返回码 | 1 |

**实际执行示例**：

```
dlsym_test
dlsym_test: foo_add(7,8)=15
ush> 
```

### dyn_hello

| 字段 | 值 |
|---|---|
| 名称 | dyn_hello |
| 分类 | 内核时代测试程序（散装源码） |
| 功能 | SPDX-License-Identifier: Apache-2 |
| 返回码 | 1 |

**实际执行示例**：

```
dyn_hello
hello from dynamic program
ush> 
```

### exec_test

| 字段 | 值 |
|---|---|
| 名称 | exec_test |
| 分类 | 内核时代测试程序（散装源码） |
| 功能 | WP-08a exec test: fork, child execs hello, parent waits |

**实际执行示例**：

```
exc
Running exception self-test (#DE, #UD, #PF)...
passed 3 exceptions, kernel alive
ush> 
```

### fdref_test

| 字段 | 值 |
|---|---|
| 名称 | fdref_test |
| 分类 | 内核时代测试程序（散装源码） |

**实际执行示例**：

```
DICTVAR=1
ush> 
```

### fork_test

| 字段 | 值 |
|---|---|
| 名称 | fork_test |
| 分类 | 内核时代测试程序（散装源码） |
| 功能 | WP-08a fork test: TRUE POSIX fork |

**实际执行示例**：

```
firewall
Firewall rules:
  (no rules)
policies: input=ACCEPT output=ACCEPT forward=ACCEPT
conntrack: 0 flows (firewall ct for details)
ush> 
```

### g6_exec_a

| 字段 | 值 |
|---|---|
| 名称 | g6_exec_a |
| 分类 | 内核时代测试程序（散装源码） |
| 功能 | WP-10-AUDIT_P2-fix3 G6 regression (BUG-0277a): exec must reset the |

**实际执行示例**：

```
fs_mount_test
usage: fs_mount_test <device>  (formats the device!)
  explicit argument required - never touches a device implicitly
ush> 
```

### g6_exec_b

| 字段 | 值 |
|---|---|
| 名称 | g6_exec_b |
| 分类 | 内核时代测试程序（散装源码） |
| 功能 | WP-10-AUDIT_P2-fix3 G6 regression (BUG-0277a): exec must reset the |

**实际执行示例**：

```
fsck
fsck: checking mounted FAT32 filesystems
usage: fsck <device>
  e.g. fsck hda
ush> 
```

### g6_oom

| 字段 | 值 |
|---|---|
| 名称 | g6_oom |
| 分类 | 内核时代测试程序（散装源码） |
| 功能 | WP-10-AUDIT_P2-fix3 G6 regression (BUG-0279b): fork under address-space |

**实际执行示例**：

```
fstest
vfs: cannot unmount the root filesystem
VFS self-test:
  mkdir /tmp/vfstest: OK
  open(O_CREAT): OK
  write: OK
  read: OK -> "hello from ramfs"
  ls /tmp/vfstest:
    hello.txt
  ramfs: 13 nodes, 3740 bytes
  p2 regressions:
    BUG-0178 dual-sided: PASS
    BUG-0179 dual-sided: PASS
    BUG-0180 hole is zeros: PASS
    BUG-0182 dual-sided: PASS
    BUG-0184 umount / refused: PASS
    p2 regressions: ALL PASS
  p1fix4 regressions:
    BUG-0061 dual-sided: PASS
    p1fix4 regressions: ALL PASS
ush> 
```

### g6_test

| 字段 | 值 |
|---|---|
| 名称 | g6_test |
| 分类 | 内核时代测试程序（散装源码） |
| 功能 | WP-10-AUDIT_P2-fix3 G6 regression test (BUG-0274/0275/0276/0277/0278/0280). |

**实际执行示例**：

```
g6_exec_a
g6_exec_a: start
g6_exec_a: child execing g6_exec_b
g6_exec_b: alive, raising SIGUSR1 with no handler
g6_exec_a: child status = 138g6_exec_a: PASS
ush> 
```

### g7mmap

| 字段 | 值 |
|---|---|
| 名称 | g7mmap |
| 分类 | 内核时代测试程序（散装源码） |
| 功能 | WP-10-AUDIT_P2-fix3 G7 mmap regression (BUG-0281 / A2-9). |

**实际执行示例**：

```
g6_exec_b
g6_exec_b: alive, raising SIGUSR1 with no handler
ush> 
```

### hello

| 字段 | 值 |
|---|---|
| 名称 | hello |
| 分类 | 内核时代测试程序（散装源码） |
| 功能 | Open Cube OS WP-08a - hello world |

**实际执行示例**：

```
heap
Kernel heap:
  size:  3477504 bytes
  alloc: 1551568 bytes (132 blocks)
  free:  1915440 bytes (196 blocks)
  overhead: 10496 bytes
  total allocs: 350  total frees: 218

ush> 
```

### int3_user

| 字段 | 值 |
|---|---|
| 名称 | int3_user |
| 分类 | 内核时代测试程序（散装源码） |

**实际执行示例**：

```
ime
pinyin IME status:
  syllables : 396
  level-1   : 3755 chars (GB2312 zones 16-55)
  phrases   : 622
  usage     : "ime -i" opens the interactive compose line (the SAME
              engine the nano/vi editors use - F2 inside an editor);
              type pinyin; digits 1-9 pick; space = first; PageUp/PageDown
              (or - / =) page; Backspace drops a letter; Enter commits the
  encoding: gb2312+utf8+ascii | font: embedded-unifont-16x16 | hanzi 6763 | symbols 682 | decode 9336 | encode 3 | convert 0 | detect 0 | miss 3
ush> 
```

### ld_so

| 字段 | 值 |
|---|---|
| 名称 | ld_so |
| 分类 | 内核时代测试程序（散装源码） |
| 功能 | SPDX-License-Identifier: Apache-2 |
| 返回码 | -1, 0, 1 |

**实际执行示例**：

```
kill
usage: kill <pid>
ush> 
```

### loop

| 字段 | 值 |
|---|---|
| 名称 | loop |
| 分类 | 内核时代测试程序（散装源码） |

**实际执行示例**：

```
run ush
started pid=4

Open Cube OS User-space Shell (3.0.0-NAK)
Type 'help' for built-in commands.

ush> 
```

(备注: run loop (async) + kill - the by-design spinner)

### main_dyn

| 字段 | 值 |
|---|---|
| 名称 | main_dyn |
| 分类 | 内核时代测试程序（散装源码） |
| 功能 | SPDX-License-Identifier: Apache-2 |
| 返回码 | 1 |

**实际执行示例**：

```
main_dyn
main: foo_add(2,3)=5
ush> 
```

### mmap_multi

| 字段 | 值 |
|---|---|
| 名称 | mmap_multi |
| 分类 | 内核时代测试程序（散装源码） |

**实际执行示例**：

```
mmap_multi
parent: mmap=1006632960child: mmap=1006637056mmap_multi: BAD child aparent: readback=PAAAmmap_multi: PASS
mmush> 
```

### mmap_test

| 字段 | 值 |
|---|---|
| 名称 | mmap_test |
| 分类 | 内核时代测试程序（散装源码） |
| 功能 | WP-08a mmap test |

**实际执行示例**：

```
mmap_test
mmap_test: startmmap_test: wrote to mmaMMAP_OK!
mmap_test: mprotect done

mmap_test: PASSush> 
```

### mprotect_test

| 字段 | 值 |
|---|---|
| 名称 | mprotect_test |
| 分类 | 内核时代测试程序（散装源码） |

**实际执行示例**：

```
mprotect_test
MPROTECT_KERNEL_REJECTED
MMPROTECT_USER_OKPASS
ush> 
```

### p0leak

| 字段 | 值 |
|---|---|
| 名称 | p0leak |
| 分类 | 内核时代测试程序（散装源码） |

**实际执行示例**：

```
p0leak
p0leak: BUG-AUD-2a-001 user-pointer fence test
p0leak: brk grown to 0x700000 (2MiB split forced)
p0leak: kernel-image write REJECTED (-1) OK
p0leak: split-sibling write REJECTED (-1) OK
p0leak: kernel-addr read REJECTED (-1) OK
p0leak: PASS - all kernel-address probes fenced
ush> 
```

### p3_test

| 字段 | 值 |
|---|---|
| 名称 | p3_test |
| 分类 | 内核时代测试程序（散装源码） |
| 功能 | WP-10-AUDIT_P2-fix3 G7 (BUG-0281): prot is now honoured, so the |

**实际执行示例**：

```
p3_test
p3_test: startp3_test: PASS_P3123
push> 
```

### pf_test

| 字段 | 值 |
|---|---|
| 名称 | pf_test |
| 分类 | 内核时代测试程序（散装源码） |

**实际执行示例**：

```
pf_test
PF-A LEAKED (P=1 fault grew stack: data lost + frames leaked)
[user] process 11 exited (code=0)

*** EXCEPTION 14: #PF Page Fault ***
  error_code=0x0000000000000007  rip=0x00000000004000A7  cs=0x0000000000000033  rflags=0x0000000000010202
  rsp=0x000000003FFEFEE0  ss=0x000000000000003B
  cr2=0x000000003FFEFEE0  (faulting address)
*** KILLING user process (pid=12) due to exception 14, kernel continues ***
[user] process killed by exception (vector=14), kernel continues
PF-T DONE
[user] process 10 exited (code=0)
ush> 
```

### pie_test

| 字段 | 值 |
|---|---|
| 名称 | pie_test |
| 分类 | 内核时代测试程序（散装源码） |
| 功能 | SPDX-License-Identifier: Apache-2 |
| 返回码 | 1 |

**实际执行示例**：

```
pie_test
pie_test: loaded at 0x20000000
ush> 
```

### pipe_test

| 字段 | 值 |
|---|---|
| 名称 | pipe_test |
| 分类 | 内核时代测试程序（散装源码） |
| 功能 | WP-08a pipe test: fork, child writes pipe, parent reads |

**实际执行示例**：

```
pipe_test
pipe_test: startpipe_test: parent readinpipe_test: got 'HHELLO from child'
ppipe_test: PASSush> 
```

### priv_test

| 字段 | 值 |
|---|---|
| 名称 | priv_test |
| 分类 | 内核时代测试程序（散装源码） |

**实际执行示例**：

```
priv_test
priv_test: attempting out 0x70 from ring 3

*** EXCEPTION 13: #GP General Protection ***
  error_code=0x0000000000000000  rip=0x000000000040001A  cs=0x0000000000000033  rflags=0x0000000000010202
  rsp=0x000000003FFFFEE0  ss=0x000000000000003B
*** KILLING user process (pid=2) due to exception 13, kernel continues ***
[user] process killed by exception (vector=13), kernel continues
ush> 
```

### reloc_test

| 字段 | 值 |
|---|---|
| 名称 | reloc_test |
| 分类 | 内核时代测试程序（散装源码） |
| 功能 | SPDX-License-Identifier: Apache-2 |
| 返回码 | 1 |

**实际执行示例**：

```
reloc_test
reloc_test: foo_add=30 foo_global=42 fp=300 msg=r
ush> 
```

### select_test

| 字段 | 值 |
|---|---|
| 名称 | select_test |
| 分类 | 内核时代测试程序（散装源码） |
| 功能 | WP-08a select test: fork, child writes pipe, parent selects |

**实际执行示例**：

```
select_test
select_test: startselect_test: parent selectingselect_test: pipe ready (PASS)
selselect_test: PASSush> 
```

### select_zero_test

| 字段 | 值 |
|---|---|
| 名称 | select_zero_test |
| 分类 | 内核时代测试程序（散装源码） |

**实际执行示例**：

```
select_zero_test
SZ1
S1 select(fd0,timeout=0)...
S1B returned (rax in r15)
S2B select(pipe,timeout=0) returned 0
SZ-PASS
[user] process 32 exited (code=0)
ush> 
```

### signal_test

| 字段 | 值 |
|---|---|
| 名称 | signal_test |
| 分类 | 内核时代测试程序（散装源码） |
| 功能 | WP-08a signal test: fork, child sends SIGUSR1 to parent |

**实际执行示例**：

```
signal_test
signal_test: startsignal_test: handler registered
signal_test: parent waiting
ssignal_test: child sending SIGUSR1
signal_test: SIGNAL CAUGHT
ssignal_test: PASSush> 
```

### sigsegv_test

| 字段 | 值 |
|---|---|
| 名称 | sigsegv_test |
| 分类 | 内核时代测试程序（散装源码） |
| 功能 | WP-11-crash_recovery 4.5: SIGSEGV-on-fault test. |

**实际执行示例**：

```
sigsegv_test
sigsegv_test: SIGSEGV handler armed
sigsegv_test: touching 0xFD000000 (kernel page)...

*** EXCEPTION 14: #PF Page Fault ***
  error_code=0x0000000000000005  rip=0x000000000040004D  cs=0x0000000000000033  rflags=0x0000000000010202
  rsp=0x000000003FFFFEE0  ss=0x000000000000003B
  cr2=0x00000000FD000000  (faulting address)
*** SIGSEGV delivered to pid=36 (handler will run, delivery #1) - kernel continues ***
SIGSEGV-TEST CAUGHT (signal 11 delivered, exiting cleanly)
[user] process 36 exited (code=0)
ush> 
```

### so_test

| 字段 | 值 |
|---|---|
| 名称 | so_test |
| 分类 | 内核时代测试程序（散装源码） |
| 功能 | SPDX-License-Identifier: Apache-2 |
| 返回码 | 1 |

**实际执行示例**：

```
so_test
main: foo_add(2,3)=5
ush> 
```

### sse_test

| 字段 | 值 |
|---|---|
| 名称 | sse_test |
| 分类 | 内核时代测试程序（散装源码） |

**实际执行示例**：

```
  [test thread] iteration 2
  [test thread] iteration 3
sse_test
  [test thread] iteration 4
SSE child  OK
[user] process 46 exited (code=0)
SSE parent OK
[user] process 45 exited (code=0)
ush> 
```

### stackovf

| 字段 | 值 |
|---|---|
| 名称 | stackovf |
| 分类 | 内核时代测试程序（散装源码） |
| 功能 | WP-11-crash_recovery 4.4: ring-3 stack-overflow interception test. |

**实际执行示例**：

```
stackovf
stackovf: fork+recursion (4 KiB frames, no handler)
stackovf: parent waiting for child
stackovf: child recursing - expect kill at the boundary

*** EXCEPTION 14: #PF Page Fault ***
  error_code=0x0000000000000007  rip=0x000000000040001A  cs=0x0000000000000033  rflags=0x0000000000010206
  rsp=0x000000003FFEFE60  ss=0x000000000000003B
  cr2=0x000000003FFEFE58  (faulting address)
*** KILLING user process (pid=3) due to exception 14, kernel continues ***
[user] process killed by exception (vector=14), kernel continues
STACKOVF-TEST DONE (child killed, kernel alive)
[user] process 2 exited (code=0)
ush> 
```
