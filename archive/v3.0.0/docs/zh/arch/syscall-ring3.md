<!-- SPDX-License-Identifier: Apache-2.0 -->
# Ring 3：系统调用、ELF 加载与进程

## 系统调用 ABI

`kernel/core/core_syscall.h`：`rax` = 系统调用号，`rdi`/`rsi`/
`rdx`/`r10` = 参数，`rax` = 返回值（>= 0 成功，< 0 错误）。
定义了 53 个系统调用号（验证：
`grep -c '#define SYS_' kernel/core/core_syscall.h`）。

## 用户程序

`kernel/core/core_usermode.c` + 汇编跳板：ring-3 入口，
fork (`arch_enter_ring3_fork.S`)、ELF 加载器、进程表、
信号。`userprogs/` 提供测试程序集 - hello、fork、exec、pipe、
signal、select、mmap 变体，以及动态链接系列。

## 动态链接 - 一个真正的树内 ld.so

`libs/ld_so.c` 是一个共享对象加载器：`run so_test`、`run
dlsym_test`、`run pie_test`、`run reloc_test`、`run dyn_hello`、
`run g7mmap` 都在使用它。L1 模块以相同方式在运行时加载 -
见 [../l1/solib-loader.md](../l1/solib-loader.md)。

## 用户程序如何运行

`run <name>` 查找嵌入式程序头（通过 `tools/embed_userprog.py` /
`tools/build_c_userprog.py` 在内核构建时从 `userprogs/` 构建）
或挂载文件系统上的文件，加载 ELF，并进入 ring 3。程序通过系统调用进行 I/O；
当它退出时，其槽位返回到进程表。

## 来自 Ring 3 的故障

用户模式的 #PF 如果不是增长则会升级为按进程终止（不是内核崩溃）；
用户模式的 #DE/#UD 同样如此。E2E 套件覆盖了所有这些内容
（`int3_user` 从 shell 作为测试运行）。
