<!-- SPDX-License-Identifier: Apache-2.0 -->
# Application dictionary - ush built-in commands (8)

### alias

| Field | Value |
|---|---|
| Name | alias |
| Category | ush built-in commands |
| Function | ush built-in command |
| Hidden parameters | hidden parameter, not a public interface; compatibility is not guaranteed: `alias` |
| Return codes | 1 |

**Executed example**:

```
alias ll=ls
ush> alias
ll='ls'
ush> 
```

### cd

| Field | Value |
|---|---|
| Name | cd |
| Category | ush built-in commands |
| Function | ush built-in command |
| Hidden parameters | hidden parameter, not a public interface; compatibility is not guaranteed: `cd` |
| Return codes | 1 |

**Executed example**:

```
cd /
pwd
```

### env

| Field | Value |
|---|---|
| Name | env |
| Category | ush built-in commands |
| Function | ush built-in command |
| Hidden parameters | hidden parameter, not a public interface; compatibility is not guaranteed: `env` |
| Return codes | 0, 1 |

**Executed example**:

```
env
```

### exit

| Field | Value |
|---|---|
| Name | exit |
| Category | ush built-in commands |
| Function | ush built-in command |
| Hidden parameters | hidden parameter, not a public interface; compatibility is not guaranteed: `--all`, `--wp`, `-a`, `-w`, `exit`, `help` |
| Return codes | 1 |

**Executed example**:

```
exit
ush: 'exit' is not available; use 'shutdown' to power off, 'reboot' to restart
ush> 
```

(Note: retry OK)

### export

| Field | Value |
|---|---|
| Name | export |
| Category | ush built-in commands |
| Function | ush built-in command |
| Hidden parameters | hidden parameter, not a public interface; compatibility is not guaranteed: `export` |
| Return codes | 1 |

**Executed example**:

```
export DICTVAR=1
ush> env
PATH=/bin:/
HOME=/
PS1=ush> 
DICTVAR=1
ush> 
```

### history

| Field | Value |
|---|---|
| Name | history |
| Category | ush built-in commands |
| Function | ush built-in command |
| Hidden parameters | hidden parameter, not a public interface; compatibility is not guaranteed: `history` |
| Return codes | 1 |

**Executed example**:

```
history
1 echo DICHECK_87
2 hda
3 echo DICHECK_88
4 hda_test
5 echo DICHECK_89
6 head
7 echo DICHECK_95
8 help
9 echo DICHECK_96
10 help_a_test
11 echo DICHECK_97
12 help_default_test
13 echo DICHECK_98
14 help_wp_test
15 echo DICHECK_99
16 history
ush> 
```

### set

| Field | Value |
|---|---|
| Name | set |
| Category | ush built-in commands |
| Function | ush built-in command |
| Hidden parameters | hidden parameter, not a public interface; compatibility is not guaranteed: `set` |
| Return codes | 1 |

**Executed example**:

```
set
ush> 
```

### unalias

| Field | Value |
|---|---|
| Name | unalias |
| Category | ush built-in commands |
| Function | ush built-in command |
| Usage | `unalias name` |
| Return codes | 1 |

**Executed example**:

```
alias ll=ls
ush> unalias ll
ush> alias
='ls'
ush> 
```
