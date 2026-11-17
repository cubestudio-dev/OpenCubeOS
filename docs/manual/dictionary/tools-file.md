<!-- SPDX-License-Identifier: Apache-2.0 -->
# Application dictionary - File and text tools (21)

### awk

| Field | Value |
|---|---|
| Name | awk |
| Category | File and text tools |
| Function | pattern scanning and processing |
| Usage | `awk '{print $N|NF|NR}' [/pat/] [file]` |
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
```

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
grep
usage: grep <pattern>
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
head
```

### ln

| Field | Value |
|---|---|
| Name | ln |
| Category | File and text tools |
| Function | create links |
| Usage | `ln <old> <new> | ln -s <abs-target> <linkpath>` |
| Parameters | `old`, `new`, `linkpath` |
| Return codes | 0 |

**Executed example**:

```
ln
usage: ln <old> <new> | ln -s <abs-target> <linkpath>
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

(Note: retry OK; plain retry)

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

(Note: retry OK; reads standard input; closed with Ctrl+D)

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

(Note: retry OK; reads standard input; closed with Ctrl+D)

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

(Note: retry OK; reads standard input; closed with Ctrl+D)
