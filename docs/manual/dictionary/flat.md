<!-- SPDX-License-Identifier: Apache-2.0 -->
# Application dictionary - Kernel-era test programs (flat sources) (33)

### badapp

| Field | Value |
|---|---|
| Name | badapp |
| Category | Kernel-era test programs (flat sources) |

**Executed example**:

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

| Field | Value |
|---|---|
| Name | dlsym_test |
| Category | Kernel-era test programs (flat sources) |
| Function | SPDX-License-Identifier: Apache-2 |
| Return codes | 1 |

**Executed example**:

```
dlsym_test
dlsym_test: foo_add(7,8)=15
ush> 
```

### dyn_hello

| Field | Value |
|---|---|
| Name | dyn_hello |
| Category | Kernel-era test programs (flat sources) |
| Function | SPDX-License-Identifier: Apache-2 |
| Return codes | 1 |

**Executed example**:

```
dyn_hello
hello from dynamic program
ush> 
```

### exec_test

| Field | Value |
|---|---|
| Name | exec_test |
| Category | Kernel-era test programs (flat sources) |
| Function | WP-08a exec test: fork, child execs hello, parent waits |

**Executed example**:

```
exc
Running exception self-test (#DE, #UD, #PF)...
passed 3 exceptions, kernel alive
ush> 
```

### fdref_test

| Field | Value |
|---|---|
| Name | fdref_test |
| Category | Kernel-era test programs (flat sources) |

**Executed example**:

```
DICTVAR=1
ush> 
```

### fork_test

| Field | Value |
|---|---|
| Name | fork_test |
| Category | Kernel-era test programs (flat sources) |
| Function | WP-08a fork test: TRUE POSIX fork |

**Executed example**:

```
firewall
Firewall rules:
  (no rules)
policies: input=ACCEPT output=ACCEPT forward=ACCEPT
conntrack: 0 flows (firewall ct for details)
ush> 
```

### g6_exec_a

| Field | Value |
|---|---|
| Name | g6_exec_a |
| Category | Kernel-era test programs (flat sources) |
| Function | WP-10-AUDIT_P2-fix3 G6 regression (BUG-0277a): exec must reset the |

**Executed example**:

```
fs_mount_test
usage: fs_mount_test <device>  (formats the device!)
  explicit argument required - never touches a device implicitly
ush> 
```

### g6_exec_b

| Field | Value |
|---|---|
| Name | g6_exec_b |
| Category | Kernel-era test programs (flat sources) |
| Function | WP-10-AUDIT_P2-fix3 G6 regression (BUG-0277a): exec must reset the |

**Executed example**:

```
fsck
fsck: checking mounted FAT32 filesystems
usage: fsck <device>
  e.g. fsck hda
ush> 
```

### g6_oom

| Field | Value |
|---|---|
| Name | g6_oom |
| Category | Kernel-era test programs (flat sources) |
| Function | WP-10-AUDIT_P2-fix3 G6 regression (BUG-0279b): fork under address-space |

**Executed example**:

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

| Field | Value |
|---|---|
| Name | g6_test |
| Category | Kernel-era test programs (flat sources) |
| Function | WP-10-AUDIT_P2-fix3 G6 regression test (BUG-0274/0275/0276/0277/0278/0280). |

**Executed example**:

```
g6_exec_a
g6_exec_a: start
g6_exec_a: child execing g6_exec_b
g6_exec_b: alive, raising SIGUSR1 with no handler
g6_exec_a: child status = 138g6_exec_a: PASS
ush> 
```

### g7mmap

| Field | Value |
|---|---|
| Name | g7mmap |
| Category | Kernel-era test programs (flat sources) |
| Function | WP-10-AUDIT_P2-fix3 G7 mmap regression (BUG-0281 / A2-9). |

**Executed example**:

```
g6_exec_b
g6_exec_b: alive, raising SIGUSR1 with no handler
ush> 
```

### hello

| Field | Value |
|---|---|
| Name | hello |
| Category | Kernel-era test programs (flat sources) |
| Function | Open Cube OS WP-08a - hello world |

**Executed example**:

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

| Field | Value |
|---|---|
| Name | int3_user |
| Category | Kernel-era test programs (flat sources) |

**Executed example**:

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

| Field | Value |
|---|---|
| Name | ld_so |
| Category | Kernel-era test programs (flat sources) |
| Function | SPDX-License-Identifier: Apache-2 |
| Return codes | -1, 0, 1 |

**Executed example**:

```
kill
usage: kill <pid>
ush> 
```

### loop

| Field | Value |
|---|---|
| Name | loop |
| Category | Kernel-era test programs (flat sources) |

**Executed example**:

```
run ush
started pid=4

Open Cube OS User-space Shell (3.0.0-NAK)
Type 'help' for built-in commands.

ush> 
```

(Note: run loop (async) + kill - the by-design spinner)

### main_dyn

| Field | Value |
|---|---|
| Name | main_dyn |
| Category | Kernel-era test programs (flat sources) |
| Function | SPDX-License-Identifier: Apache-2 |
| Return codes | 1 |

**Executed example**:

```
main_dyn
main: foo_add(2,3)=5
ush> 
```

### mmap_multi

| Field | Value |
|---|---|
| Name | mmap_multi |
| Category | Kernel-era test programs (flat sources) |

**Executed example**:

```
mmap_multi
parent: mmap=1006632960child: mmap=1006637056mmap_multi: BAD child aparent: readback=PAAAmmap_multi: PASS
mmush> 
```

### mmap_test

| Field | Value |
|---|---|
| Name | mmap_test |
| Category | Kernel-era test programs (flat sources) |
| Function | WP-08a mmap test |

**Executed example**:

```
mmap_test
mmap_test: startmmap_test: wrote to mmaMMAP_OK!
mmap_test: mprotect done

mmap_test: PASSush> 
```

### mprotect_test

| Field | Value |
|---|---|
| Name | mprotect_test |
| Category | Kernel-era test programs (flat sources) |

**Executed example**:

```
mprotect_test
MPROTECT_KERNEL_REJECTED
MMPROTECT_USER_OKPASS
ush> 
```

### p0leak

| Field | Value |
|---|---|
| Name | p0leak |
| Category | Kernel-era test programs (flat sources) |

**Executed example**:

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

| Field | Value |
|---|---|
| Name | p3_test |
| Category | Kernel-era test programs (flat sources) |
| Function | WP-10-AUDIT_P2-fix3 G7 (BUG-0281): prot is now honoured, so the |

**Executed example**:

```
p3_test
p3_test: startp3_test: PASS_P3123
push> 
```

### pf_test

| Field | Value |
|---|---|
| Name | pf_test |
| Category | Kernel-era test programs (flat sources) |

**Executed example**:

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

| Field | Value |
|---|---|
| Name | pie_test |
| Category | Kernel-era test programs (flat sources) |
| Function | SPDX-License-Identifier: Apache-2 |
| Return codes | 1 |

**Executed example**:

```
pie_test
pie_test: loaded at 0x20000000
ush> 
```

### pipe_test

| Field | Value |
|---|---|
| Name | pipe_test |
| Category | Kernel-era test programs (flat sources) |
| Function | WP-08a pipe test: fork, child writes pipe, parent reads |

**Executed example**:

```
pipe_test
pipe_test: startpipe_test: parent readinpipe_test: got 'HHELLO from child'
ppipe_test: PASSush> 
```

### priv_test

| Field | Value |
|---|---|
| Name | priv_test |
| Category | Kernel-era test programs (flat sources) |

**Executed example**:

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

| Field | Value |
|---|---|
| Name | reloc_test |
| Category | Kernel-era test programs (flat sources) |
| Function | SPDX-License-Identifier: Apache-2 |
| Return codes | 1 |

**Executed example**:

```
reloc_test
reloc_test: foo_add=30 foo_global=42 fp=300 msg=r
ush> 
```

### select_test

| Field | Value |
|---|---|
| Name | select_test |
| Category | Kernel-era test programs (flat sources) |
| Function | WP-08a select test: fork, child writes pipe, parent selects |

**Executed example**:

```
select_test
select_test: startselect_test: parent selectingselect_test: pipe ready (PASS)
selselect_test: PASSush> 
```

### select_zero_test

| Field | Value |
|---|---|
| Name | select_zero_test |
| Category | Kernel-era test programs (flat sources) |

**Executed example**:

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

| Field | Value |
|---|---|
| Name | signal_test |
| Category | Kernel-era test programs (flat sources) |
| Function | WP-08a signal test: fork, child sends SIGUSR1 to parent |

**Executed example**:

```
signal_test
signal_test: startsignal_test: handler registered
signal_test: parent waiting
ssignal_test: child sending SIGUSR1
signal_test: SIGNAL CAUGHT
ssignal_test: PASSush> 
```

### sigsegv_test

| Field | Value |
|---|---|
| Name | sigsegv_test |
| Category | Kernel-era test programs (flat sources) |
| Function | WP-11-crash_recovery 4.5: SIGSEGV-on-fault test. |

**Executed example**:

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

| Field | Value |
|---|---|
| Name | so_test |
| Category | Kernel-era test programs (flat sources) |
| Function | SPDX-License-Identifier: Apache-2 |
| Return codes | 1 |

**Executed example**:

```
so_test
main: foo_add(2,3)=5
ush> 
```

### sse_test

| Field | Value |
|---|---|
| Name | sse_test |
| Category | Kernel-era test programs (flat sources) |

**Executed example**:

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

| Field | Value |
|---|---|
| Name | stackovf |
| Category | Kernel-era test programs (flat sources) |
| Function | WP-11-crash_recovery 4.4: ring-3 stack-overflow interception test. |

**Executed example**:

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
