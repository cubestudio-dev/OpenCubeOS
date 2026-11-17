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
*** KILLING user process (pid=35) due to exception 14, kernel continues ***
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
```

### exec_test

| Field | Value |
|---|---|
| Name | exec_test |
| Category | Kernel-era test programs (flat sources) |
| Function | WP-08a exec test: fork, child execs hello, parent waits |

**Executed example**:

```
exec_test
```

### fdref_test

| Field | Value |
|---|---|
| Name | fdref_test |
| Category | Kernel-era test programs (flat sources) |

**Executed example**:

```
fdref_test
FDREF PASS
[user] process 7 exited (code=0)
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
fork_test
fork_test: start
parent: pid=17 ppid=1 child=18child: pid=18 ppid=17fork_test: PASS
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
g6_exec_a
g6_exec_a: start
g6_exec_a: child execing g6_exec_b
g6_exec_b: alive, raising SIGUSR1 with no handler
g6_exec_a: child status = 138g6_exec_a: PASS
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
g6_exec_b
g6_exec_b: alive, raising SIGUSR1 with no handler
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
g6_oom
g6_oom: start

*** EXCEPTION 14: #PF Page Fault ***
  error_code=0x0000000000000007  rip=0x00000000004000D4  cs=0x0000000000000033  rflags=0x0000000000010246
  rsp=0x000000003FFFFEE0  ss=0x000000000000003B
  cr2=0x000000003C000000  (faulting address)
*** KILLING user process (pid=35) due to exception 14, kernel continues ***
[user] process killed by exception (vector=14), kernel continues
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
g6_test
g6_test: start
g6_test: T2 fork-mmap-inherit start
S0
S1
S2
SP
SC
g6_test: =PASS=
g6_test: T5 per-proc-cwd start
g6_test: =PASS=
g6_test: T1 exit0-reap start
[user] process 41 exited (code=42)
g6_test: =PASS=
g6_test: T3 write2-no-reader start
g6_test: =PASS=
g6_test: T4 sig-restorer start
g6_test: T4 handler entered
g6_test: T4 back from handler
g6_test: =PASS=
g6_test: T7 solib-reject start
g6_test: =PASS=
g6_test: T6 solib-window start
g6_test: T6 dlopen count = 3839g6_test: =PASS=
g6_test: failed tests = 0g6_test: PASS
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
g7mmap
G7MMAP: start
G7MMAP PASS T1 (bump RW map + round-trip)
G7MMAP PASS T2 (addr hint honoured)
G7MMAP PASS T3 (kernel-addr hint rejected)
G7MMAP PASS T4 (prot 0x8 rejected)
G7MMAP PASS T5 (over-window length rejected)
G7MMAP PASS T6 (munmap kernel addr rejected)
G7MMAP PASS T7 (munmap legal range)
G7MMAP PASS T8 (mprotect RW toggle + write)
G7MMAP PASS T9 (mprotect prot 0x9 rejected)
G7MMAP PASS T10 (munmap beyond window rejected)
G7MMAP T11: PF probe armed (write to R-only page)

*** EXCEPTION 14: #PF Page Fault ***
  error_code=0x0000000000000007  rip=0x000000000040031C  cs=0x0000000000000033  rflags=0x0000000000010206
  rsp=0x000000003FFFFEE0  ss=0x000000000000003B
  cr2=0x000000003C012000  (faulting address)
*** KILLING user process (pid=47) due to exception 14, kernel continues ***
[user] process killed by exception (vector=14), kernel continues
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
hello
ush> 
```

### int3_user

| Field | Value |
|---|---|
| Name | int3_user |
| Category | Kernel-era test programs (flat sources) |

**Executed example**:

```
int3_user
int3_user: about to int3

*** EXCEPTION 3: #BP Breakpoint ***
  error_code=0x0000000000000000  rip=0x0000000000400019  cs=0x0000000000000033  rflags=0x0000000000010202
  rsp=0x000000003FFFFEE0  ss=0x000000000000003B
*** KILLING user process (pid=97) due to exception 3, kernel continues ***
[user] process killed by exception (vector=3), kernel continues
*** IST-exit: vector=3 exit-stack rsp=0x00000000003E1FD0 [off-IST2 OK]
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
ld_so

*** EXCEPTION 14: #PF Page Fault ***
  error_code=0x0000000000000005  rip=0x000000001000192D  cs=0x0000000000000033  rflags=0x0000000000010212
  rsp=0x000000003FFFFEA8  ss=0x000000000000003B
  cr2=0x0000000008000018  (faulting address)
*** KILLING user process (pid=119) due to exception 14, kernel continues ***
[user] process killed by exception (vector=14), kernel continues
ush> 
```

### loop

| Field | Value |
|---|---|
| Name | loop |
| Category | Kernel-era test programs (flat sources) |

**Executed example**:

```
run loop
started pid=3
loop: running
```

(Note: captured before reset; session reset (Timeout exceeded.
<pexpect.pty_spawn.spawn object at 0x7fd38))

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
[user] process 79 exited (code=0)

*** EXCEPTION 14: #PF Page Fault ***
  error_code=0x0000000000000007  rip=0x00000000004000A7  cs=0x0000000000000033  rflags=0x0000000000010202
  rsp=0x000000003FFEFEE0  ss=0x000000000000003B
  cr2=0x000000003FFEFEE0  (faulting address)
*** KILLING user process (pid=80) due to exception 14, kernel continues ***
[user] process killed by exception (vector=14), kernel continues
PF-T DONE
[user] process 78 exited (code=0)
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
[user] process 43 exited (code=0)
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
*** SIGSEGV delivered to pid=51 (handler will run, delivery #1) - kernel continues ***
SIGSEGV-TEST CAUGHT (signal 11 delivered, exiting cleanly)
[user] process 51 exited (code=0)
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
sse_test
SSE parent OK
SSE child  OK
[user] process 9 exited (code=0)
[user] process 8 exited (code=0)
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
