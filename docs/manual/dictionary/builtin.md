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
ush> pwd
/
ush> 
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
PATH=/bin:/
HOME=/
PS1=ush> 
ush> 
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
1 help
2 help_a_test
3 help_default_test
4 help_wp_test
5 history
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
