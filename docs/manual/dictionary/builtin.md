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

(Note: exit drops to the L0 recovery shell; 'run ush' returns)

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
exit
ush: 'exit' is not available; use 'shutdown' to power off, 'reboot' to restart
ush> run ush
started pid=10

Open Cube OS User-space Shell (3.0.0-NAK)
Type 'help' for built-in commands.

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
help_a_test
help_a_test: 'help -a' == default A-Z view, ordering verified: PASS
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
