<!-- SPDX-License-Identifier: Apache-2.0 -->
# Application dictionary - File and text tools (21)

### awk

| Field | Value |
|---|---|
| Name | awk |
| Category | File and text tools |
| Function | pattern scanning and processing |
| Usage | `awk '{print $N\|NF\|NR}' [/pat/] [file]` |
| Return codes | 0 |

**Executed example**:

```
awk
usage: awk '{print $N|NF|NR}' [/pat/] [file]
ush> 
```

### cat

| Field | Value |
|---|---|
| Name | cat |
| Category | File and text tools |
| Function | print file contents |
| Return codes | 0 |

**Executed example**:

```
cat
ush> 
```

(Note: reads standard input; closed with Ctrl+D)

### chmod

| Field | Value |
|---|---|
| Name | chmod |
| Category | File and text tools |
| Function | change file permissions |
| Usage | `chmod <mode> <path>   (mode in octal, e.g. 644` |
| Parameters | `mode`, `path` |
| Return codes | 0 |

**Executed example**:

```
chmod
usage: chmod <mode> <path>   (mode in octal, e.g. 644)
ush> 
```

### chown

| Field | Value |
|---|---|
| Name | chown |
| Category | File and text tools |
| Function | change file owner |
| Usage | `chown <uid>[:<gid>] <path>` |
| Parameters | `uid`, `gid`, `path` |
| Return codes | 0 |

**Executed example**:

```
chown
usage: chown <uid>[:<gid>] <path>
ush> 
```

### cp

| Field | Value |
|---|---|
| Name | cp |
| Category | File and text tools |
| Function | copy files |
| Usage | `cp <src> <dst>` |
| Parameters | `src`, `dst` |
| Return codes | 0, 1 |

**Executed example**:

```
cp
usage: cp <src> <dst>
ush> 
```

### df

| Field | Value |
|---|---|
| Name | df |
| Category | File and text tools |
| Function | report filesystem free space |
| Return codes | 0 |

**Executed example**:

```
df
Filesystem     Mount     Size     Used    Avail  Use%
ramfs          /         0        5 files (kernel df for full mount table)
ush> 
```

### du

| Field | Value |
|---|---|
| Name | du |
| Category | File and text tools |
| Function | report file space usage |
| Return codes | 0 |

**Executed example**:

```
du
3712 bytes  3 files  /
ush> 
```

### grep

| Field | Value |
|---|---|
| Name | grep |
| Category | File and text tools |
| Function | search text in files |
| Usage | `grep <pattern>` |
| Parameters | `pattern` |
| Return codes | 0 |

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
*** KILLING user process (pid=38) due to exception 14, kernel continues ***
[user] process killed by exception (vector=14), kernel continues
ush> 
```

### head

| Field | Value |
|---|---|
| Name | head |
| Category | File and text tools |
| Function | show the first lines of a file |
| Return codes | 0 |

**Executed example**:

```
halt_test
halt_test: power_halt() = 'System halted.' + cli;hlt loop (kernel/power.c)
halt_test: this test must return, so it does NOT halt; the live halt is verified by running 'halt' in a dedicated QEMU session
halt_test: live halt not observed (destructive) : SKIP - see the dedicated halt session
ush> 
```

### ln

| Field | Value |
|---|---|
| Name | ln |
| Category | File and text tools |
| Function | create links |
| Usage | `ln <old> <new> \| ln -s <abs-target> <linkpath>` |
| Parameters | `old`, `new`, `linkpath` |
| Return codes | 0 |

**Executed example**:

```
lang
Console output language: en
ush> 
```

### ls

| Field | Value |
|---|---|
| Name | ls |
| Category | File and text tools |
| Function | list directory contents |
| Return codes | 0 |

**Executed example**:

```
ls
d var
d bin
d dev
d tmp
d etc
ush> 
```

### mkdir

| Field | Value |
|---|---|
| Name | mkdir |
| Category | File and text tools |
| Function | create a directory |
| Usage | `mkdir <path>` |
| Parameters | `path` |
| Return codes | 0 |

**Executed example**:

```
mkdir
usage: mkdir <path>
ush> 
```

### mv

| Field | Value |
|---|---|
| Name | mv |
| Category | File and text tools |
| Function | move/rename files |
| Usage | `mv <src> <dst>` |
| Parameters | `src`, `dst` |
| Return codes | 0, 1 |

**Executed example**:

```
  [task 1] iter 2
  [task 2] iter 2
  [task 3] iter 2
mv
usage: mv <src> <dst>
ush> 
```

### rm

| Field | Value |
|---|---|
| Name | rm |
| Category | File and text tools |
| Function | remove files |
| Usage | `rm <file>` |
| Parameters | `file` |
| Return codes | 0 |

**Executed example**:

```
rm
usage: rm <file>
ush> 
```

### rmdir

| Field | Value |
|---|---|
| Name | rmdir |
| Category | File and text tools |
| Function | remove an empty directory |
| Usage | `rmdir <path>` |
| Parameters | `path` |
| Return codes | 0 |

**Executed example**:

```
rmdir
usage: rmdir <path>
ush> 
```

### sort

| Field | Value |
|---|---|
| Name | sort |
| Category | File and text tools |
| Function | sort lines |
| Return codes | 0 |

**Executed example**:

```
sort
ush> 
```

### stat

| Field | Value |
|---|---|
| Name | stat |
| Category | File and text tools |
| Function | show file metadata |
| Usage | `stat <path>` |
| Parameters | `path` |
| Return codes | 0 |

**Executed example**:

```
stat
usage: stat <path>
ush> 
```

### tail

| Field | Value |
|---|---|
| Name | tail |
| Category | File and text tools |
| Function | show the last lines of a file |
| Return codes | 0 |

**Executed example**:

```
tail
ush> 
```

### touch

| Field | Value |
|---|---|
| Name | touch |
| Category | File and text tools |
| Function | create an empty file / update mtime |
| Usage | `touch <file>` |
| Parameters | `file` |
| Return codes | 0 |

**Executed example**:

```
touch
usage: touch <file>
ush> 
```

### uniq

| Field | Value |
|---|---|
| Name | uniq |
| Category | File and text tools |
| Function | drop repeated adjacent lines |
| Return codes | 0 |

**Executed example**:

```
uniq
ush> 
```

### wc

| Field | Value |
|---|---|
| Name | wc |
| Category | File and text tools |
| Function | count lines/words/bytes |
| Return codes | 0 |

**Executed example**:

```
wc
0 0 0
ush> 
```
